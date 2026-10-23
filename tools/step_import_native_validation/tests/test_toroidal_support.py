# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""A regular spindle cap keeps its original analytic definition and native UV."""
import hashlib
import math
import unittest

from . import test_general
from .test_ring_segmentation import replace_once
from .test_toroidal import toroidal_source

FLAG = '--restrict-unused-support-domains'


def spindle_cap_source(*, sixty_degrees=False):
    # Public R=1,r=2 outer profile, 0<=z<=2, radius=1+sqrt(4-z*z).
    # Its meridian is a quarter of the original radius-2 circle, shared in
    # opposite directions by the side face. End caps keep the whole source rings.
    data = test_general.cylinder_source()
    for old, new in [
        (b"#101=CARTESIAN_POINT('',(0.,0.,7.))", b"#101=CARTESIAN_POINT('',(0.,0.,2.))"),
        (b"#103=CARTESIAN_POINT('',(0.,3.,7.))", b"#103=CARTESIAN_POINT('',(0.,1.,2.))"),
        (b"#111=CIRCLE('',#107,3.)", b"#111=CIRCLE('',#107,1.)"),
        (b"#142=CYLINDRICAL_SURFACE('',#106,3.)", b"#142=TOROIDAL_SURFACE('',#106,1.,2.)"),
        (b"#112=LINE('',#102,#113)", b"#112=CIRCLE('',#113,2.)"),
        (b"#113=VECTOR('',#104,7.)", b"#113=AXIS2_PLACEMENT_3D('',#114,#105,#116)"),
    ]:
        data = replace_once(data, old, new)
    data = replace_once(data, b'ENDSEC;\nEND-ISO',
                        b"#114=CARTESIAN_POINT('',(0.,1.,0.));\n"
                        b"#116=DIRECTION('',(0.,1.,0.));\nENDSEC;\nEND-ISO")
    if sixty_degrees:
        data = replace_once(data, b"#101=CARTESIAN_POINT('',(0.,0.,2.))",
                            b"#101=CARTESIAN_POINT('',(0.,0.,1.7320508075688772935))")
        data = replace_once(data, b"#103=CARTESIAN_POINT('',(0.,1.,2.))",
                            b"#103=CARTESIAN_POINT('',(0.,2.,1.7320508075688772935))")
        data = replace_once(data, b"#111=CIRCLE('',#107,1.)", b"#111=CIRCLE('',#107,2.)")
    return data


def inner_spindle_source():
    # The other regular sheet has signed analytic radius <0. Its physical
    # radius is sqrt(4-z*z)-1 on 0<=z<=1. The source face sense is reversed
    # because the analytic U/V normal points toward the material interior.
    data = spindle_cap_source()
    for old, new in [
        (b"#101=CARTESIAN_POINT('',(0.,0.,2.))", b"#101=CARTESIAN_POINT('',(0.,0.,1.))"),
        (b"#102=CARTESIAN_POINT('',(0.,3.,0.))", b"#102=CARTESIAN_POINT('',(0.,1.,0.))"),
        (b"#103=CARTESIAN_POINT('',(0.,1.,2.))", b"#103=CARTESIAN_POINT('',(0.,0.7320508075688772935,1.))"),
        (b"#110=CIRCLE('',#106,3.)", b"#110=CIRCLE('',#106,1.)"),
        (b"#111=CIRCLE('',#107,1.)", b"#111=CIRCLE('',#107,0.7320508075688772935)"),
        (b"#114=CARTESIAN_POINT('',(0.,1.,0.))", b"#114=CARTESIAN_POINT('',(0.,-1.,0.))"),
        (b"#182=ADVANCED_FACE('',(#172),#142,.T.)", b"#182=ADVANCED_FACE('',(#172),#142,.F.)"),
    ]:
        data = replace_once(data, old, new)
    return data


def regular_ring_cap_source():
    data = spindle_cap_source(sixty_degrees=True)
    for old, new in [
        (b"#102=CARTESIAN_POINT('',(0.,3.,0.))", b"#102=CARTESIAN_POINT('',(5.,0.,0.))"),
        (b"#103=CARTESIAN_POINT('',(0.,2.,1.7320508075688772935))", b"#103=CARTESIAN_POINT('',(4.,0.,1.7320508075688772935))"),
        (b"#110=CIRCLE('',#106,3.)", b"#110=CIRCLE('',#106,5.)"),
        (b"#111=CIRCLE('',#107,2.)", b"#111=CIRCLE('',#107,4.)"),
        (b"#142=TOROIDAL_SURFACE('',#106,1.,2.)", b"#142=TOROIDAL_SURFACE('',#106,3.,2.)"),
        (b"#114=CARTESIAN_POINT('',(0.,1.,0.))", b"#114=CARTESIAN_POINT('',(3.,0.,0.))"),
        (b"#113=AXIS2_PLACEMENT_3D('',#114,#105,#116)", b"#113=AXIS2_PLACEMENT_3D('',#114,#117,#105)"),
        (b"#116=DIRECTION('',(0.,1.,0.))", b"#117=DIRECTION('',(0.,-1.,0.))"),
    ]:
        data = replace_once(data, old, new)
    return data


class ToroidalSupportTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes

    def test_regular_spindle_cap_uses_original_basis_and_analytic_truth(self):
        data = spindle_cap_source()
        process, report = self.invoke_bytes(data, FLAG)
        self.assertEqual(0, process.returncode, report.get('diagnostics'))
        self.assertEqual('Accepted', report['status'])
        self.assertEqual('None', report['repair'])
        self.assertEqual(hashlib.sha256(data).hexdigest(), report['source_sha256'])
        self.assertEqual(data.decode('ascii'), report['source_inspection']['source']['input']['raw'])
        model = report['model']
        self.assertEqual((3, 3, 2), tuple(model[k] for k in ('faces', 'edges', 'vertices')))
        # Integrate the profile independently: A=2*pi*int_0^(pi/2)2*(1+2cos(v))dv
        # plus the two planar caps; V=pi*int_0^2(1+sqrt(4-z*z))^2 dz.
        self.assertAlmostEqual(2 * math.pi**2 + 18 * math.pi, model['area_mm2'], delta=0.02)
        self.assertAlmostEqual(22 * math.pi / 3 + 2 * math.pi**2,
                               model['material_volume']['value_mm3'], delta=0.01)
        for a, b in zip(model['bounds_mm'], ((-3., -3., 0.), (3., 3., 2.))):
            for x, y in zip(a, b):
                self.assertAlmostEqual(x, y, delta=1e-6)
        normalization = report['surface_normalizations']
        self.assertEqual(1, len(normalization))
        self.assertEqual(('182', '142'), (normalization[0]['source_face_id'], normalization[0]['source_surface_id']))
        self.assertLessEqual(normalization[0]['continuous_difference_upper_mm'], 1e-9)
        self.assertGreater(normalization[0]['minimum_absolute_generator_radius_mm'], 0)

    def test_regular_spindle_still_requires_explicit_normalization(self):
        process, report = self.invoke_bytes(spindle_cap_source())
        self.assertEqual(2, process.returncode)
        self.assertEqual('GeneralToroidalRingRequired', report['diagnostics'][-1]['key'])
        self.assertIsNone(report['model'])

    def test_regular_horn_cap_excludes_the_unused_axis_contact(self):
        data = spindle_cap_source()
        for old, new in [
            (b"#102=CARTESIAN_POINT('',(0.,3.,0.))", b"#102=CARTESIAN_POINT('',(0.,4.,0.))"),
            (b"#103=CARTESIAN_POINT('',(0.,1.,2.))", b"#103=CARTESIAN_POINT('',(0.,2.,2.))"),
            (b"#114=CARTESIAN_POINT('',(0.,1.,0.))", b"#114=CARTESIAN_POINT('',(0.,2.,0.))"),
            (b"#110=CIRCLE('',#106,3.)", b"#110=CIRCLE('',#106,4.)"),
            (b"#111=CIRCLE('',#107,1.)", b"#111=CIRCLE('',#107,2.)"),
            (b"#142=TOROIDAL_SURFACE('',#106,1.,2.)", b"#142=TOROIDAL_SURFACE('',#106,2.,2.)"),
        ]:
            data = replace_once(data, old, new)
        process, report = self.invoke_bytes(data, FLAG)
        self.assertEqual(0, process.returncode, process.stderr or report)
        self.assertEqual('None', report['repair'])
        self.assertGreater(report['surface_normalizations'][0]['minimum_absolute_generator_radius_mm'], 0)
        # Independent radius profile 2+sqrt(4-z*z), on 0<=z<=2.
        self.assertAlmostEqual(4 * math.pi**2 + 28 * math.pi, report['model']['area_mm2'], delta=0.03)
        self.assertAlmostEqual(40 * math.pi / 3 + 4 * math.pi**2,
                               report['model']['material_volume']['value_mm3'], delta=0.02)

    def test_unproved_late_periodic_composition_cannot_publish(self):
        process, report = self.invoke_bytes(spindle_cap_source(), FLAG, '--normalize-periodic-faces')
        self.assertNotEqual(0, process.returncode)
        self.assertEqual('ToroidalSupportNormalizationCompositionUnestablished', report['diagnostics'][-1]['key'])
        self.assertIsNone(report['model'])

    def test_non_quadrant_meridian_keeps_its_source_parameter(self):
        data = spindle_cap_source(sixty_degrees=True)
        process, report = self.invoke_bytes(data, FLAG)
        self.assertEqual(0, process.returncode, process.stderr or report)
        self.assertAlmostEqual(4 * math.pi**2 / 3 + 4 * math.pi * math.sqrt(3) + 13 * math.pi,
                               report['model']['area_mm2'], delta=0.03)
        self.assertAlmostEqual(5 * math.pi * math.sqrt(3) + 4 * math.pi**2 / 3,
                               report['model']['material_volume']['value_mm3'], delta=0.02)

    def test_other_regular_sheet_retains_negative_generator_radius_and_source_face_sense(self):
        data = inner_spindle_source()
        process, report = self.invoke_bytes(data, FLAG)
        self.assertEqual(0, process.returncode, process.stderr or report)
        self.assertLess(report['surface_normalizations'][0]['generator_radius_bounds_mm'][1], 0)
        self.assertAlmostEqual(math.pi * (9 - 2 * math.sqrt(3)) - 2 * math.pi**2 / 3,
                               report['model']['area_mm2'], delta=0.02)
        self.assertAlmostEqual(math.pi * (14 / 3 - math.sqrt(3) - 2 * math.pi / 3),
                               report['model']['material_volume']['value_mm3'], delta=0.01)

    def test_rotated_translated_source_retains_physical_measures_and_frame(self):
        data = spindle_cap_source()
        for old, new in [
            (b"#100=CARTESIAN_POINT('',(0.,0.,0.))", b"#100=CARTESIAN_POINT('',(11.,-4.,2.))"),
            (b"#101=CARTESIAN_POINT('',(0.,0.,2.))", b"#101=CARTESIAN_POINT('',(11.,-6.,2.))"),
            (b"#102=CARTESIAN_POINT('',(0.,3.,0.))", b"#102=CARTESIAN_POINT('',(11.,-4.,5.))"),
            (b"#103=CARTESIAN_POINT('',(0.,1.,2.))", b"#103=CARTESIAN_POINT('',(11.,-6.,3.))"),
            (b"#114=CARTESIAN_POINT('',(0.,1.,0.))", b"#114=CARTESIAN_POINT('',(11.,-4.,3.))"),
            (b"#104=DIRECTION('',(0.,0.,1.))", b"#104=DIRECTION('',(0.,-1.,0.))"),
            (b"#116=DIRECTION('',(0.,1.,0.))", b"#116=DIRECTION('',(0.,0.,1.))"),
        ]:
            data = replace_once(data, old, new)
        process, report = self.invoke_bytes(data, FLAG)
        self.assertEqual(0, process.returncode, process.stderr or report)
        for a, b in zip(report['model']['bounds_mm'], ((8., -6., -1.), (14., -4., 5.))):
            for x, y in zip(a, b):
                self.assertAlmostEqual(x, y, delta=1e-6)
        self.assertAlmostEqual(22 * math.pi / 3 + 2 * math.pi**2,
                               report['model']['material_volume']['value_mm3'], delta=0.01)

    def test_equivalent_metre_source_scales_lengths_once(self):
        data = spindle_cap_source().replace(b'.MILLI.,.METRE.', b'$,.METRE.')
        for old, new in [
            (b"(0.,0.,2.)", b"(0.,0.,0.002)"),
            (b"(0.,3.,0.)", b"(0.,0.003,0.)"),
            (b"(0.,1.,2.)", b"(0.,0.001,0.002)"),
            (b"#114=CARTESIAN_POINT('',(0.,1.,0.))", b"#114=CARTESIAN_POINT('',(0.,0.001,0.))"),
            (b"#110=CIRCLE('',#106,3.)", b"#110=CIRCLE('',#106,0.003)"),
            (b"#111=CIRCLE('',#107,1.)", b"#111=CIRCLE('',#107,0.001)"),
            (b"#112=CIRCLE('',#113,2.)", b"#112=CIRCLE('',#113,0.002)"),
            (b"#142=TOROIDAL_SURFACE('',#106,1.,2.)", b"#142=TOROIDAL_SURFACE('',#106,0.001,0.002)"),
        ]:
            data = replace_once(data, old, new)
        process, report = self.invoke_bytes(data, FLAG)
        self.assertEqual(0, process.returncode, process.stderr or report)
        self.assertAlmostEqual(2 * math.pi**2 + 18 * math.pi, report['model']['area_mm2'], delta=0.02)
        self.assertAlmostEqual(22 * math.pi / 3 + 2 * math.pi**2,
                               report['model']['material_volume']['value_mm3'], delta=0.01)
        self.assertEqual(data.decode('ascii'), report['source_inspection']['source']['input']['raw'])

    def test_full_spindle_and_unestablished_region_never_publish(self):
        full = toroidal_source()
        for old, new in [(b"(5.,0.,0.)", b"(1.25,0.,0.)"), (b"(7.5,0.,0.)", b"(3.75,0.,0.)"),
                         (b"CIRCLE('',#106,7.5)", b"CIRCLE('',#106,3.75)"),
                         (b"TOROIDAL_SURFACE('',#106,5.,2.5)", b"TOROIDAL_SURFACE('',#106,1.25,2.5)")]:
            full = replace_once(full, old, new)
        unmarked = replace_once(spindle_cap_source(), b'#172=FACE_OUTER_BOUND', b'#172=FACE_BOUND')
        for data in (full, unmarked):
            process, report = self.invoke_bytes(data, FLAG)
            self.assertNotEqual(0, process.returncode)
            self.assertIsNone(report['model'])
            self.assertEqual('None', report['repair'])

    def test_ring_cap_projection_keeps_analytic_support_and_paired_meridian(self):
        process, report = self.invoke_bytes(regular_ring_cap_source(), FLAG)
        self.assertEqual(0, process.returncode, process.stderr or report)
        self.assertNotIn('surface_normalizations', report)
        self.assertAlmostEqual(4 * math.pi**2 + 4 * math.pi * math.sqrt(3) + 41 * math.pi,
                               report['model']['area_mm2'], delta=0.04)
        self.assertAlmostEqual(15 * math.pi * math.sqrt(3) + 4 * math.pi**2,
                               report['model']['material_volume']['value_mm3'], delta=0.03)

    def test_existing_complete_ring_has_no_support_conversion(self):
        process, report = self.invoke_bytes(toroidal_source(), FLAG)
        self.assertEqual(0, process.returncode, process.stderr or report)
        self.assertNotIn('surface_normalizations', report)
        self.assertAlmostEqual(62.5 * math.pi**2, report['model']['material_volume']['value_mm3'], delta=0.2)
