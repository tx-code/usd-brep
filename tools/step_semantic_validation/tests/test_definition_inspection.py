# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent observations survive sibling failures without source/model qualification."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from .test_product_structure import graph_source, append_record


FIXTURE = (Path(__file__).resolve().parents[3]
           / 'source/STEP_IMPORT/tests/fixtures/independent_distinct_parts.step')


def break_face(data, second=False):
    prefix, bound = (b'10', b'990001') if second else (b'', b'990000')
    data = data.replace(b"ADVANCED_FACE('',(#" + prefix + b"506)",
                        b"ADVANCED_FACE('',(#" + prefix + b'506,#' + bound + b')', 1)
    return append_record(data, b'#' + bound + b"=FACE_OUTER_BOUND('',#" + prefix + b'505,.T.);')


class DefinitionInspectionTests(unittest.TestCase):
    def invoke(self, data, *flags, inspect=True):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'public.step'
            source.write_bytes(data)
            command = [os.environ['STEP_SEMANTIC_BINARY'], '--fixture-limits', str(source),
                       '--diagnose-independent-definitions' if inspect else '--independent-products', *flags]
            process = subprocess.run(command, capture_output=True, timeout=30)
            self.assertEqual(data, source.read_bytes())
        return process, json.loads(process.stdout) if process.stdout else None

    def observations(self, result):
        self.assertIsNone(result['snapshot'])
        self.assertNotIn('document', result)
        evidence = result['definition_inspection']
        self.assertEqual('DiagnosticObservationsOnly', evidence['qualification'])
        self.assertEqual('NotRun', evidence['whole_document_ownership'])
        self.assertEqual('NotRun', evidence['interoperability_approval'])
        self.assertEqual('NotEstablished', evidence['source_isolation'])
        return evidence

    def test_bad_first_definition_does_not_hide_later_part_and_unit_observations(self):
        data = break_face(FIXTURE.read_bytes())
        process, result = self.invoke(data)
        self.assertEqual(1, process.returncode)
        evidence = self.observations(result)
        self.assertTrue(evidence['all_definitions_visited'])
        self.assertEqual(['7', '902'], [x['definition'] for x in evidence['definitions']])
        self.assertEqual(['Rejected', 'CheckedPrerequisites'],
                         [x['part_prerequisites'] for x in evidence['definitions']])
        self.assertEqual(['Checked', 'Checked'], [x['unit_context'] for x in evidence['definitions']])
        self.assertEqual('SourceRuleViolation:FACE.WR2', evidence['definitions'][0]['diagnostics'][0]['key'])
        self.assertEqual(data.decode('ascii'), result['source']['input']['raw'])

    def test_single_definition_diagnostic_does_not_use_the_normal_fast_path(self):
        path = FIXTURE.parent / 'cylindrical_band_duplicate_outer.step'
        process, result = self.invoke(path.read_bytes())
        self.assertEqual(1, process.returncode)
        evidence = self.observations(result)
        self.assertTrue(evidence['all_definitions_visited'])
        self.assertEqual(1, len(evidence['definitions']))
        self.assertEqual('Rejected', evidence['definitions'][0]['part_prerequisites'])
        self.assertEqual('Checked', evidence['definitions'][0]['unit_context'])

    def test_empty_definition_inventory_is_not_a_green_inspection(self):
        data = FIXTURE.read_bytes()
        start = data.index(b'DATA;') + len(b'DATA;')
        data = data[:start] + b'\n' + data[data.rindex(b'ENDSEC;'):]
        process, result = self.invoke(data)
        self.assertEqual(2, process.returncode)
        self.assertFalse(self.observations(result)['all_definitions_visited'])

    def test_shared_invalid_face_is_reported_for_every_definition_use(self):
        _, result = self.invoke(break_face(graph_source()))
        evidence = self.observations(result)
        self.assertEqual(['Rejected', 'Rejected'], [x['part_prerequisites'] for x in evidence['definitions']])
        self.assertEqual(['507', '507'], [x['diagnostics'][0]['record_id'] for x in evidence['definitions']])

    def test_green_observations_do_not_approve_orphan_content_or_a_document(self):
        data = append_record(FIXTURE.read_bytes(), b"#990000=MANIFOLD_SOLID_BREP('unowned',#22);")
        process, result = self.invoke(data)
        self.assertEqual(7, process.returncode)
        evidence = self.observations(result)
        self.assertTrue(evidence['all_definitions_visited'])
        self.assertEqual(['CheckedPrerequisites'] * 2, [x['part_prerequisites'] for x in evidence['definitions']])
        ordinary, checked = self.invoke(data, inspect=False)
        self.assertNotEqual(0, ordinary.returncode)
        self.assertNotIn('definition_inspection', checked)
        self.assertNotIn('document', checked)

    def test_provisional_metadata_stays_unapproved_and_visible(self):
        data = FIXTURE.read_bytes().replace(b"PRODUCT_DEFINITION_CONTEXT('part definition'",
                                            b"PRODUCT_DEFINITION_CONTEXT('unapproved caption'")
        _, result = self.invoke(data, '--interop-nongeometric')
        evidence = self.observations(result)
        self.assertIn('UnsupportedDefinitionContextRole', [x['key'] for x in evidence['unapproved_diagnostics']])
        process, limited = self.invoke(data, '--interop-nongeometric', '--max-preserved-diagnostics', '1')
        self.assertEqual(3, process.returncode)
        bounded = self.observations(limited)
        self.assertFalse(bounded['all_definitions_visited'])
        self.assertLessEqual(len(bounded['unapproved_diagnostics'])
                             + sum(len(x['diagnostics']) for x in bounded['definitions']), 1)

    def test_unknown_types_and_untrustworthy_raw_index_prevent_inspection(self):
        data = FIXTURE.read_bytes()
        for bad in (append_record(data, b'#990000=UNKNOWN_INFLUENCE(#507);'),
                    append_record(data, b'#507=CARTESIAN_POINT(\'duplicate\',(0.,0.,0.));'),
                    data.replace(b'#507=ADVANCED_FACE', b'#507=INVALID(', 1)):
            with self.subTest():
                _, result = self.invoke(bad)
                evidence = self.observations(result)
                self.assertFalse(evidence['all_definitions_visited'])
                self.assertEqual([], evidence['definitions'])

    def test_early_pending_fact_survives_a_later_global_unit_failure(self):
        data = FIXTURE.read_bytes().replace(b"#100=CARTESIAN_POINT(''", b'#100=CARTESIAN_POINT($', 1)
        data = data.replace(b'SI_UNIT(.MILLI.,.METRE.)', b'SI_UNIT(.MILLI.,.SECOND.)', 1)
        _, result = self.invoke(data, '--interop-nongeometric')
        evidence = self.observations(result)
        self.assertEqual([], evidence['definitions'])
        self.assertIn('RequiredAttributeMissing', [x['key'] for x in evidence['unapproved_diagnostics']])

    def test_global_pending_facts_share_the_cap_with_schema_interpretation(self):
        data = FIXTURE.read_bytes().replace(b'AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}', b'AUTOMOTIVE_DESIGN', 1)
        data = append_record(data, b"#990010=VALUE_REPRESENTATION_ITEM('ratio',POSITIVE_RATIO_MEASURE(-1.));")
        process, result = self.invoke(data, '--interop-nongeometric', '--interpret-ap214-name-only-as-2010',
                                      '--max-preserved-diagnostics', '1')
        self.assertEqual(3, process.returncode)
        evidence = self.observations(result)
        self.assertFalse(evidence['all_definitions_visited'])
        self.assertEqual([], evidence['unapproved_diagnostics'])

    def test_diagnostic_and_report_limits_cannot_publish_incomplete_success(self):
        data = break_face(break_face(FIXTURE.read_bytes()), second=True)
        process, result = self.invoke(data, '--max-preserved-diagnostics', '1')
        self.assertEqual(3, process.returncode)
        self.assertEqual('ResourceLimit', result['status'])
        self.assertFalse(self.observations(result)['all_definitions_visited'])
        process, result = self.invoke(data, '--max-report-bytes', '64')
        self.assertEqual(5, process.returncode)
        self.assertIsNone(result)
        self.assertEqual(b'', process.stdout)
