# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit missing carrier declaration assumptions never weaken geometry checks."""
import unittest
from tools.step_import_native_validation.tests import test_general


def blank_box(declaration=b'   '):
    return (test_general.CORPUS/'S203-P04/input.step').read_bytes().replace(b"'2;1'", b"'"+declaration+b"'")


class BlankCarrierTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes
    flags = ('--interop-nongeometric', '--interpret-blank-level-as-corrected')

    def test_strict_and_level1_assumption_still_refuse_blank_declaration(self):
        for flags in ((), ('--interop-nongeometric', '--interpret-level1-as-corrected')):
            run, report = self.invoke_bytes(blank_box(), *flags)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(report['model'])
            self.assertEqual(0, report['native_calls'])
            self.assertEqual('UnsupportedImplementationLevel', report['source_inspection']['source']['diagnostics'][0]['key'])

    def test_explicit_assumption_retains_blank_source_and_analytic_model(self):
        for token in (b'', b' ', b'   '):
            source = blank_box(token)
            run, report = self.invoke_bytes(source, *self.flags)
            self.assertEqual(11, run.returncode, run.stderr or report['diagnostics'])
            self.assertEqual('AcceptedUnderInterpretation', report['status'])
            self.assertEqual('BlankLevelAsCorrectedEncoding', report['source_carrier_interpretation'])
            self.assertFalse(report['source_carrier_verified'])
            raw = report['source_inspection']['source']
            self.assertEqual(source, raw['input']['raw'].encode('latin1'))
            self.assertEqual(token.decode(), raw['carrier_interpretation']['declared_level'])
            self.assertAlmostEqual(6000, report['model']['material_volume']['value_mm3'], delta=1e-6)

    def test_nonblank_unknown_claims_are_not_trimmed_or_guessed(self):
        for token in (b'1', b'1;1', b'2;2', b'3;1', b' 2;1 ', b'unknown'):
            run, report = self.invoke_bytes(blank_box(token), *self.flags)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(report['model'])
            self.assertEqual(0, report['native_calls'])

    def test_valid_declaration_does_not_acquire_unused_assumption(self):
        run, report = self.invoke_bytes(blank_box(b'2;1'), *self.flags)
        self.assertEqual(0, run.returncode)
        self.assertEqual('Accepted', report['status'])
        self.assertTrue(report['source_carrier_verified'])
        self.assertNotIn('source_carrier_interpretation', report)

    def test_bad_required_geometry_still_rejects_before_native_calls(self):
        source = blank_box().replace(b"#100=CARTESIAN_POINT('',(2.,-3.,5.))", b"#100=CARTESIAN_POINT('',(2.,-3.,5.,9.))")
        run, report = self.invoke_bytes(source, *self.flags)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report['model'])
        self.assertEqual(0, report['native_calls'])

    def test_policy_needs_interoperability_and_cannot_be_combined_with_level1(self):
        for flags in (('--interpret-blank-level-as-corrected',),
                      ('--interop-nongeometric', '--interpret-level1-as-corrected', '--interpret-blank-level-as-corrected'),
                      ('--interop-nongeometric', '--interpret-blank-level-as-corrected', '--interpret-level1-as-corrected')):
            run, report = self.invoke_bytes(blank_box(), *flags)
            self.assertEqual(2, run.returncode)
            self.assertIsNone(report)
