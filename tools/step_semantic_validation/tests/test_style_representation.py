# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""A presentation-only type error requires the exact selected representation."""
import unittest

from tools.step_semantic_validation.tests import test_interoperability
from tools.step_semantic_validation.tests.test_ap214 import CORPUS, ap214_source
from tools.step_semantic_validation.tests.test_ap214_entities import append_records, presentation_source


def styled_representation_box():
    source = ap214_source((CORPUS/'S203-P04/input.step').read_bytes())
    presentation = b'#900=' + presentation_source().split(b'#900=')[1].split(b'ENDSEC;')[0]
    return append_records(source, presentation).replace(b"STYLED_ITEM('source style',(#902),#21)",
                                                       b"STYLED_ITEM('source style',(#902),#20)")


class StyleRepresentationTests(unittest.TestCase):
    invoke = test_interoperability.InteroperabilityTests.invoke

    def test_strict_diagnostic_and_original_reference_are_preserved(self):
        source = styled_representation_box()
        strict, rejected = self.invoke(source, '--general-brep-source')
        self.assertEqual(1, strict.returncode)
        self.assertEqual('ReferenceTypeMismatch', rejected['diagnostics'][0]['key'])
        run, report = self.invoke(source, '--general-brep-source', '--interop-nongeometric')
        self.assertEqual(0, run.returncode, run.stderr or report)
        self.assertEqual('CheckedWithDiagnostics', report['status'])
        self.assertEqual(rejected['diagnostics'], report['preserved_diagnostics'])
        self.assertEqual(source, report['source']['input']['raw'].encode('latin1'))
        record = next(r for r in report['source']['records'] if r['id'] == '901')
        self.assertEqual('20', record['components'][0]['parameters'][2]['text'])

    def test_detached_foreign_physical_and_bad_geometry_do_not_gain_an_exception(self):
        source = styled_representation_box()
        foreign = append_records(source, b"#990=SHAPE_REPRESENTATION('foreign',(#21),#12);\n")
        variants = [
            source.replace(b"#900=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('display',(#901),#12);\n", b''),
            foreign.replace(b"STYLED_ITEM('source style',(#902),#20)", b"STYLED_ITEM('source style',(#902),#990)"),
            source.replace(b"'body',(#21),#12", b"'body',(#21,#901),#12"),
            source.replace(b'(2.,-3.,5.)', b"('invalid',-3.,5.)"),
        ]
        for data in variants:
            run, report = self.invoke(data, '--general-brep-source', '--interop-nongeometric')
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(report['snapshot'])

    def test_profile_and_preserved_diagnostic_budget_remain_enforced(self):
        for flags in [('--interop-nongeometric',),
                      ('--general-brep-source', '--interop-nongeometric', '--max-preserved-diagnostics', '0')]:
            run, report = self.invoke(styled_representation_box(), *flags)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(report['snapshot'])
