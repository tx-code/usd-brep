# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Physical unit spelling interpretation requires exact dimensional evidence."""
import re
import unittest
from tools.step_import_native_validation.tests import test_general
from tools.step_import_native_validation.tests import test_conversion_units
conversion_source = test_conversion_units.conversion_source


def explicit_dimensions(*, nested=False, factor='0.0254', exponents=b'1.,0.,0.,0.,0.,0.,0.'):
    data = conversion_source(ap214=True, nested=nested, factor=factor)
    data = data.replace(b'LENGTH_UNIT() NAMED_UNIT(*));', b'LENGTH_UNIT() NAMED_UNIT(#990000));')
    return data.replace(b'ENDSEC;\nEND-ISO', b'#990000=DIMENSIONAL_EXPONENTS('+exponents+b');\nENDSEC;\nEND-ISO')


class VerifiedDimensionsTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes
    flags = ('--interop-nongeometric', '--interpret-explicit-unit-dimensions')
    assert_inch_box = test_conversion_units.ConversionUnitTests.assert_inch_box

    def test_strict_default_still_rejects_physical_derived_slot_reference(self):
        run, report = self.invoke_bytes(explicit_dimensions(), '--interop-nongeometric')
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report['model'])
        self.assertEqual(0, report['native_calls'])
        self.assertEqual('DerivedSlotMarkerRequired', report['source_inspection']['diagnostics'][0]['key'])

    def test_exact_unit_interpretation_preserves_source_and_physical_inch_box(self):
        for nested in (False, True):
            source = explicit_dimensions(nested=nested)
            run, report = self.invoke_bytes(source, *self.flags)
            self.assertEqual(11, run.returncode, run.stderr or report['diagnostics'])
            self.assertEqual('AcceptedUnderInterpretation', report['status'])
            self.assertEqual('VerifiedExplicitUnitDimensions', report['source_unit_interpretation'])
            self.assert_inch_box(report)
            self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('ascii'))
            interpretation = report['source_inspection']['unit_interpretation']
            self.assertFalse(interpretation['source_declaration_conforming'])
            self.assertEqual(2 if nested else 1, len(interpretation['units']))
            self.assertTrue(all(d['key'] == 'DerivedSlotMarkerRequired' for d in interpretation['original_diagnostics']))
            self.assertFalse(any(d['key'] == 'DerivedSlotMarkerRequired'
                                 for d in report['source_inspection']['preserved_diagnostics']))

    def test_conflicting_dimensions_factor_quantity_and_nonpositive_scale_reject(self):
        cases = [explicit_dimensions(exponents=b'0.,0.,0.,0.,0.,0.,0.'),
                 explicit_dimensions(factor='0.'), explicit_dimensions(factor='-1.'),
                 explicit_dimensions().replace(b'SI_UNIT($,.METRE.)', b'SI_UNIT($,.RADIAN.)')]
        for source in cases:
            run, report = self.invoke_bytes(source, *self.flags)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(report['model'])
            self.assertEqual(0, report['native_calls'])

    def test_factor_cycle_cannot_acquire_a_unit_receipt(self):
        source = explicit_dimensions()
        source = re.sub(rb'(MEASURE_WITH_UNIT\(LENGTH_MEASURE\(0.0254\),)#\d+', rb'\g<1>#10', source)
        run, report = self.invoke_bytes(source, *self.flags)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report['model'])
        self.assertEqual(0, report['native_calls'])
        self.assertEqual('DeclaredOnly', report['source_inspection']['unit_interpretation']['applied'])

    def test_unused_choice_does_not_interpret_valid_dimensions_marker(self):
        run, report = self.invoke_bytes(conversion_source(ap214=True), *self.flags)
        self.assertEqual(0, run.returncode)
        self.assertEqual('Accepted', report['status'])
        self.assertNotIn('source_unit_interpretation', report)
        self.assertEqual('DeclaredOnly', report['source_inspection']['unit_interpretation']['applied'])

    def test_option_requires_explicit_general_interoperability(self):
        run, report = self.invoke_bytes(explicit_dimensions(), '--interpret-explicit-unit-dimensions')
        self.assertEqual(2, run.returncode)
        self.assertIsNone(report)

    def test_si_angle_explicit_zero_dimensions_are_verified_not_assumed(self):
        source = (test_general.CORPUS/'S203-P04/input.step').read_bytes()
        source = source.replace(
            b'AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}',
            b'AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}').replace(
            b"'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011", b"'automotive_design',2010")
        source = source.replace(b'#11=(NAMED_UNIT(*) PLANE_ANGLE_UNIT()', b'#11=(NAMED_UNIT(#990000) PLANE_ANGLE_UNIT()')
        source = source.replace(b'ENDSEC;\nEND-ISO', b'#990000=DIMENSIONAL_EXPONENTS(0.,0.,0.,0.,0.,0.,0.);\nENDSEC;\nEND-ISO')
        run, report = self.invoke_bytes(source, *self.flags)
        self.assertEqual(11, run.returncode)
        unit = report['source_inspection']['unit_interpretation']['units'][0]
        self.assertEqual('11', unit['unit'])
        self.assertIsNone(unit['conversion_factor'])
        self.assertAlmostEqual(6000, report['model']['material_volume']['value_mm3'], delta=1e-6)
        bad = source.replace(b'DIMENSIONAL_EXPONENTS(0.,0.,0.,0.,0.,0.,0.)', b'DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.)')
        run, rejected = self.invoke_bytes(bad, *self.flags)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(rejected['model'])
        self.assertEqual(0, rejected['native_calls'])
        self.assertEqual('ExplicitUnitDimensionsConflict', rejected['source_inspection']['diagnostics'][0]['key'])
