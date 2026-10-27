# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from dataclasses import replace

from . import test_topology
from tools.smlib_viewer.apex_repair import arguments, validate_receipt
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker, scene_file

from PySide6 import QtWidgets


class ApexRepairIntentTests(unittest.TestCase):
    def settings(self):
        return ImportSettings(apex_repair_source_sha256='a' * 64, maximum_apex_repair_vertices=1,
                              maximum_apex_repair_controls=4096, maximum_apex_repair_proof_work=1000000,
                              maximum_apex_repair_move_mm='.001', maximum_apex_repair_total_move_mm='.001',
                              conical_apex_uses=('7:21:182:174:164:122:142',))

    def test_default_has_no_repair(self):
        self.assertEqual([], arguments(ImportSettings()))

    def test_explicit_budget_text_and_source_uses_survive(self):
        settings = self.settings()
        args = arguments(settings)
        self.assertIn('.001', args)
        self.assertEqual(settings.conical_apex_uses[0], args[-1])
        values = [settings.conical_apex_uses[0]]
        captured = replace(settings, conical_apex_uses=values)
        values.clear()
        self.assertEqual(1, len(captured.conical_apex_uses))

    def test_missing_ambiguous_and_nonfinite_requests_refuse(self):
        good = self.settings()
        for changes in (dict(apex_repair_source_sha256=''), dict(maximum_apex_repair_vertices=0),
                        dict(maximum_apex_repair_controls=True), dict(maximum_apex_repair_move_mm='NaN'),
                        dict(maximum_apex_repair_total_move_mm='0'), dict(maximum_apex_repair_proof_work=100000001),
                        dict(conical_apex_uses=good.conical_apex_uses * 2), dict(normalize_periodic=True),
                        dict(restrict_domains=True), dict(assembly=True)):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                replace(good, **changes)

    def test_other_repair_policies_cannot_be_mixed(self):
        with self.assertRaises(ValueError):
            replace(self.settings(), repair_source_sha256='b' * 64, maximum_face_orientation_edits=1,
                    maximum_repair_proof_controls=4096, maximum_repair_proof_products=1000,
                    face_orientation_edits=('7:21:182:F:T',))


class ApexRepairReceiptTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='viewer-public-apex-receipt-')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.settings = ApexRepairIntentTests().settings()
        self.manifest = dict(
            native_import_status='AcceptedWithRepairs', repair_kind='ConicalApexVertices', repair_applied=True,
            repaired_vertex_count=1, repaired_face_count=0, conical_apex_repair=dict(
                source_sha256='a' * 64, request_complete=True, uses=list(self.settings.conical_apex_uses),
                maximum_vertices=1, maximum_proof_controls=4096, maximum_proof_work=1000000,
                maximum_displacement_mm='1/1000', maximum_total_displacement_mm='1/1000'))
        use = dict(zip(('definition_id', 'shape_id', 'face_id', 'bound_id', 'loop_id', 'vertex_id', 'surface_id'),
                       self.settings.conical_apex_uses[0].split(':')))
        rational = dict(kind='rational', num=1, den=1000)
        # This is an independently authored transport control, not geometric
        # proof. The real worker below must produce its own native evidence.
        self.report = dict(
            status='AcceptedWithRepairs', source_sha256='a' * 64,
            quality=dict(maximum_work_tolerance_mm=rational),
            original_attempt=dict(status='PostcheckFailure', repair='None', diagnostics=[dict(key='strict-refusal')]),
            conical_apex_repair=dict(
                configuration='ConicalVertexLoopApexCorrection-Study-0.1', source_bytes_unchanged=True,
                tolerance_growth_allowance_mm=0, source_sha256='a' * 64,
                maximum_vertices=1, maximum_proof_controls=4096, maximum_proof_work=1000000,
                requested_uses=[use], maximum_displacement_mm=rational, maximum_total_displacement_mm=rational,
                budget=dict(request_complete=True, corrected_vertices=1, proof_work=50,
                            cumulative_displacement_upper_mm=.00002, minimum_local_protection_lower_mm=1),
                evidence=dict(curves_and_surfaces_identical=True, unrequested_vertices_identical=True,
                              working_tolerances_identical=True, original_diagnostics_retained=True,
                              actions=[dict(use=use, source_point_id='102', target_vertex=1,
                                            original_mm=[.00001, 0, 3], corrected_mm=[0, 0, 3],
                                            displacement_upper_mm=.00002, local_protection_lower_mm=1)])))

    def validate(self, report=None, manifest=None):
        (self.directory / 'import-report.json').write_text(json.dumps(report or self.report), encoding='utf-8')
        return validate_receipt(self.directory, manifest or self.manifest, self.settings, scene_file)

    def test_single_and_grouped_evidence_are_owning_and_complete(self):
        for container in (None, 'bodies', 'definitions'):
            report = copy.deepcopy(self.report)
            if container:
                leaf = dict(quality=report.pop('quality'), original_attempt=report.pop('original_attempt'),
                            conical_apex_repair=dict(evidence=report['conical_apex_repair'].pop('evidence')))
                report[container] = [dict(checks=leaf)]
            with self.subTest(container=container):
                receipt = self.validate(report)
                self.assertEqual(1, len(receipt['actions']))
                self.assertTrue(receipt['original_source_retained'])
                (self.directory / 'import-report.json').unlink()
                self.assertEqual([0, 0, 3], receipt['actions'][0]['corrected_mm'])

    def test_native_intent_budgets_strict_failure_and_proof_are_mandatory(self):
        mutations = (
            lambda r: r.update(status='Accepted'),
            lambda r: r['conical_apex_repair'].update(source_sha256='b' * 64),
            lambda r: r['conical_apex_repair'].update(maximum_proof_controls=5000),
            lambda r: r['conical_apex_repair'].update(source_bytes_unchanged=False),
            lambda r: r['conical_apex_repair'].update(tolerance_growth_allowance_mm=.001),
            lambda r: r['conical_apex_repair'].update(maximum_vertices=True),
            lambda r: r['conical_apex_repair']['maximum_displacement_mm'].update(num=1.1),
            lambda r: r['conical_apex_repair']['budget'].update(proof_work=1000001),
            lambda r: r['conical_apex_repair']['budget'].update(cumulative_displacement_upper_mm=.000001),
            lambda r: r['conical_apex_repair']['budget'].update(minimum_local_protection_lower_mm=2),
            lambda r: r['original_attempt'].update(repair='SomeRepair'),
            lambda r: r['original_attempt'].update(diagnostics=[]),
            lambda r: r['conical_apex_repair']['evidence'].update(curves_and_surfaces_identical=False),
            lambda r: r['conical_apex_repair']['evidence']['actions'][0].update(corrected_mm=[1, 0, 3]),
            lambda r: r['conical_apex_repair']['evidence']['actions'][0].update(target_vertex=-1),
            lambda r: r['conical_apex_repair']['evidence']['actions'][0]['use'].update(vertex_id='999'),
            lambda r: r['conical_apex_repair']['evidence']['actions'][0].update(local_protection_lower_mm=.00001),
            lambda r: r['conical_apex_repair']['evidence']['actions'].append(
                copy.deepcopy(r['conical_apex_repair']['evidence']['actions'][0])),
            lambda r: r.update(bodies=[dict(checks=copy.deepcopy(r))]),
        )
        for index, change in enumerate(mutations):
            report = copy.deepcopy(self.report)
            change(report)
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.validate(report)

    def test_bounded_parser_rejects_duplicate_fields_and_oversized_report(self):
        path = self.directory / 'import-report.json'
        for payload in ('{"status":1,"status":2}', ' ' * (self.settings.report_budget_mib * 1024 * 1024 + 1)):
            path.write_text(payload, encoding='utf-8')
            with self.assertRaises(ValueError):
                validate_receipt(self.directory, self.manifest, self.settings, scene_file)
        with self.assertRaisesRegex(ValueError, 'UnexpectedConicalApexRepair'):
            validate_receipt(self.directory, self.manifest, ImportSettings(), scene_file)


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native display worker')
class ApexRepairWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load
    close_loader = test_topology.NativeTopologyTests.close_loader

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings())
        self.addCleanup(self.close_loader)

    def test_native_repair_receipt_and_hud_distinguish_vertex_correction(self):
        from tools.smlib_viewer.app import ViewerWindow
        fixture = test_topology.REPO / 'source/STEP_IMPORT/tests/fixtures/analytic_cone_vertex_loop.step'
        data = fixture.read_bytes().replace(b'(0.,0.,3.)', b'(0.00001,0.,3.)')
        self.loader.settings = replace(ApexRepairIntentTests().settings(),
                                       apex_repair_source_sha256=hashlib.sha256(data).hexdigest())
        with tempfile.TemporaryDirectory(prefix='viewer-public-repaired-cone-') as folder:
            path = Path(folder) / 'public.step'
            path.write_bytes(data)
            _, mesh, boundary, metadata = self.load(path)
            self.assertEqual(data, path.read_bytes())
        receipt = self.loader.worker_result['conical_apex_repair_receipt']
        self.assertEqual(1, len(receipt['actions']))
        self.assertEqual('AcceptedWithRepairs', metadata['native_import_status'])
        self.assertEqual(0, metadata['repaired_face_count'])
        window = ViewerWindow(self.loader.settings)
        try:
            window._loaded('public.step', mesh, boundary, metadata)
            self.assertIn('已修复 1 个圆锥尖点顶点', window.hud.text())
            self.assertNotIn('处面朝向', window.hud.text())
            self.assertIsNotNone(window.picker.resolve('face', 0))
        finally:
            window.close()


if __name__ == '__main__':
    unittest.main()
