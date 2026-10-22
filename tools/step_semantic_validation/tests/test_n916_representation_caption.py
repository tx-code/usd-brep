# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Modified N916 labels do not acquire AP214's name-dependent purpose rules."""
import unittest

from tools.step_semantic_validation.tests.test_ap203_n916 import legacy_source
from tools.step_semantic_validation.tests import test_opaque_identity as opaque
from tools.step_semantic_validation.tests.test_ap214 import AP214


def caption_source(name):
    source = legacy_source()
    assert b"'body',(#21)" in source
    return source.replace(b"'body',(#21)", b"'" + name + b"',(#21)")


class N916RepresentationCaptionTests(unittest.TestCase):
    invoke = opaque.OpaqueIdentityTests.invoke

    def test_explicit_interop_keeps_raw_label_and_original_diagnostic(self):
        for name in (b'\x90', b'centre of mass', b'treatment result'):
            with self.subTest(name=name):
                source = caption_source(name)
                run, report = self.invoke(source)
                self.assertEqual(0, run.returncode, report['diagnostics'])
                self.assertEqual('CheckedWithDiagnostics', report['status'])
                key = 'OpaqueDataTextUnestablished' if name == b'\x90' else 'UnsupportedRepresentationPurpose'
                self.assertIn(key, [d['key'] for d in report['preserved_diagnostics']])
                self.assertEqual(source, report['source']['input']['raw'].encode('latin1'))

    def test_strict_ap214_or_budget_cannot_inherit_legacy_exception(self):
        for name in (b'\x90', b'centre of mass', b'treatment result'):
            data = caption_source(name)
            for candidate, interop, flags in (
                (data, False, ()),
                (data.replace(b'CONFIG_CONTROL_DESIGN', AP214), True, ()),
                (data, True, ('--max-preserved-diagnostics', 0)),
            ):
                with self.subTest(name=name, interop=interop, flags=flags):
                    run, report = self.invoke(candidate, interop, *flags)
                    self.assertNotEqual(0, run.returncode)
                    self.assertIsNone(report['snapshot'])
                    expected = 'PreservedDiagnosticLimit' if flags else ('OpaqueDataTextUnestablished' if name == b'\x90' else 'UnsupportedRepresentationPurpose')
                    self.assertEqual(expected, report['diagnostics'][0]['key'])

    def test_bad_geometry_and_detached_representation_still_reject(self):
        data = caption_source(b'\x90')
        for candidate in (
            data.replace(b'(2.,-3.,5.)', b"('invalid',-3.,5.)"),
            data.replace(b'ENDSEC;\nEND-ISO', b"#990=ADVANCED_BREP_SHAPE_REPRESENTATION('\x91',(#21),#12);\nENDSEC;\nEND-ISO"),
        ):
            run, report = self.invoke(candidate)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(report['snapshot'])
