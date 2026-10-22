# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Whole-request material collections retain all source/native body identities."""
import unittest
from pathlib import Path
from tools.step_import_native_validation.tests import test_general
from tools.step_import_native_validation.tests.test_material_collection import two_material_boxes


class NativeCollectionTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes

    def test_cli_publishes_all_checked_bodies_with_one_shared_source_report(self):
        source = two_material_boxes()
        run, report = self.invoke_bytes(source, '--material-collection', '--compact-source-report')
        self.assertEqual(0, run.returncode, run.stderr or report['diagnostics'])
        self.assertEqual('Accepted', report['status'])
        self.assertIsNone(report['model'])
        collection = report['material_collection']
        self.assertTrue(collection['complete'])
        self.assertEqual(2, collection['selected_body_count'])
        self.assertEqual(['21', '10021'], [b['source_shape_id'] for b in collection['bodies']])
        self.assertTrue(all(b['checks_passed'] for b in collection['bodies']))
        self.assertEqual(report['native_calls'], sum(b['checks']['native_calls'] for b in collection['bodies']))
        for body in collection['bodies']:
            self.assertIsNone(body['checks']['source_inspection'])
            self.assertEqual('CollectionRoot', body['checks']['source_evidence'])
            self.assertAlmostEqual(6000, body['checks']['model']['material_volume']['value_mm3'], delta=1e-6)
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('ascii'))
        self.assertEqual(1, run.stdout.count(b'"format_version":"step-source.'))

    def test_shared_shell_keeps_two_distinct_bodies_without_union(self):
        source = two_material_boxes()
        source = source[:source.index(b'#10021=')] + b"#10021=MANIFOLD_SOLID_BREP('',#22);\nENDSEC;\nEND-ISO-10303-21;\n"
        run, report = self.invoke_bytes(source, '--material-collection')
        self.assertEqual(0, run.returncode)
        bodies = report['material_collection']['bodies']
        self.assertEqual(['21', '10021'], [b['source_shape_id'] for b in bodies])
        self.assertEqual(['22', '22'], [b['source_shell_id'] for b in bodies])
        for body in bodies:
            self.assertAlmostEqual(6000, body['checks']['model']['material_volume']['value_mm3'], delta=1e-6)

    def test_interpreted_source_qualification_applies_to_entire_collection(self):
        source = two_material_boxes().replace(b"#10021=MANIFOLD_SOLID_BREP('',", b"#10021=MANIFOLD_SOLID_BREP('\x80\xfe',")
        run, report = self.invoke_bytes(source, '--material-collection', '--interop-nongeometric', '--assume-ascii-invariant-data')
        self.assertEqual(11, run.returncode)
        self.assertEqual('AcceptedUnderInterpretation', report['status'])
        self.assertTrue(report['material_collection']['complete'])
        self.assertEqual('CheckedWithDiagnostics', report['source_inspection']['status'])
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('latin1'))

    def test_failure_in_second_body_cannot_publish_first_model(self):
        source = two_material_boxes().replace(b"#10100=CARTESIAN_POINT('',(102.0,-3.,5.))", b"#10100=CARTESIAN_POINT('',(103.0,-3.,5.))")
        run, report = self.invoke_bytes(source, '--material-collection')
        self.assertNotEqual(0, run.returncode)
        self.assertFalse(report['material_collection']['complete'])
        self.assertIsNone(report['model'])
        self.assertTrue(report['candidate_discarded'])
        self.assertTrue(report['material_collection']['bodies'][0]['checks_passed'])
        self.assertTrue(all(b['checks']['model'] is None for b in report['material_collection']['bodies']))

    def test_surface_role_and_linked_geometry_are_not_silently_accepted(self):
        source = (test_general.CORPUS/'S203-P01/input.step').read_bytes()
        run, report = self.invoke_bytes(source, '--material-collection')
        self.assertNotEqual(0, run.returncode)
        self.assertEqual(0, report['native_calls'])
        self.assertFalse(report['material_collection']['complete'])

    def test_associated_geometry_request_fails_before_body_allocation(self):
        path = Path(__file__).resolve().parents[3]/'source/STEP_IMPORT/tests/fixtures/linked_wireframe.step'
        run, report = self.invoke_bytes(path.read_bytes(), '--material-collection')
        self.assertNotEqual(0, run.returncode)
        self.assertEqual(0, report['native_calls'])
        self.assertIsNone(report['model'])
        self.assertFalse(report['material_collection']['complete'])
        self.assertEqual('MaterialCollectionAssociatedGeometryUnimplemented', report['diagnostics'][-1]['key'])
