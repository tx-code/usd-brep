# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Resource transport does not select quality, retry, or publish partial scenes."""
import contextlib
import copy
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from PySide6 import QtCore, QtWidgets
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker, scene_file
from tools.smlib_viewer.cli import main
from tools.smlib_viewer.execution import arguments, validate_receipt, SIDECAR
from tools.smlib_viewer.tests import test_topology

FIXTURE = test_topology.REPO / 'source/STEP_IMPORT/tests/fixtures/source_line_pcurve_converted_boundary.step'


def receipt():
    return dict(format='brep-view-execution.v1', scope='ApplicationResourcesOnly', source_sha256='a' * 64,
                max_exact_work_units=4000000, deadline_ms=180000, deadline_configured=True,
                exact_geometry_work=dict(maximum_units=4000000, used_units=123))


class ExecutionRequests(unittest.TestCase):
    def test_explicit_resources_are_immutable_positive_bounded_and_not_inferred(self):
        self.assertEqual([], arguments(ImportSettings()))
        settings = ImportSettings(max_exact_work_units=512000000, deadline_ms=600000, timeout_seconds=660)
        self.assertEqual(['--max-exact-work-units', '512000000', '--deadline-ms', '600000'], arguments(settings))
        self.assertEqual('development', settings.profile)
        for key in ('max_exact_work_units', 'deadline_ms', 'timeout_seconds'):
            for invalid in (True, 0, -1, '10', float('inf'), 3.5, 1 << 64):
                with self.subTest(key=key, value=invalid), self.assertRaises(ValueError):
                    ImportSettings(**{key: invalid})
        with self.assertRaisesRegex(ValueError, 'LongerWorkerTimeout'):
            ImportSettings(deadline_ms=120000)

    def test_invalid_cli_fails_before_application_start_and_never_adjusts_timeout(self):
        for flags in (('--max-exact-work-units', '0'), ('--deadline-ms', '600000'),
                      ('--worker-timeout-seconds', '0'), ('--deadline-ms', 'NaN')):
            with (self.subTest(flags=flags), contextlib.redirect_stderr(io.StringIO()),
                  self.assertRaises(SystemExit) as error):
                main(['public.step', *flags])
            self.assertEqual(2, error.exception.code)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            main(['public.stl', '--max-exact-work-units', '10'])

    def test_bounded_applied_receipt_hash_work_and_default_compatibility(self):
        settings = ImportSettings(max_exact_work_units=4000000, deadline_ms=180000, timeout_seconds=210)
        scene = dict(source_sha256='a' * 64)
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            self.assertIsNone(validate_receipt(directory, scene, ImportSettings(), scene_file))
            path = directory / SIDECAR
            expected = receipt()
            path.write_text(json.dumps(expected), encoding='utf-8')
            self.assertEqual(expected, validate_receipt(directory, scene, settings, scene_file))
            for key, bad in (('source_sha256', 'b' * 64), ('max_exact_work_units', True),
                             ('deadline_ms', 90000), ('deadline_configured', False), ('extra', 1)):
                path.write_text(json.dumps(dict(expected, **{key: bad})), encoding='utf-8')
                with self.subTest(key=key), self.assertRaises(ValueError):
                    validate_receipt(directory, scene, settings, scene_file)
            bad = copy.deepcopy(expected)
            bad['exact_geometry_work']['used_units'] = 4000001
            path.write_text(json.dumps(bad), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'WorkEvidence'):
                validate_receipt(directory, scene, settings, scene_file)
            path.write_text('{"format":1,"format":2}', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'Duplicate'):
                validate_receipt(directory, scene, settings, scene_file)
            path.write_bytes(b' ' * 4097)
            with self.assertRaisesRegex(ValueError, 'ReceiptLimit'):
                validate_receipt(directory, scene, settings, scene_file)
            with self.assertRaisesRegex(ValueError, 'Unexpected'):
                validate_receipt(directory, scene, ImportSettings(), scene_file)


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native worker')
class ExecutionWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def invoke(self, *flags):
        with tempfile.TemporaryDirectory(prefix='public-execution-') as name:
            output = Path(name)
            run = subprocess.run([str(native_worker()), str(FIXTURE), str(output), 'development', '0', '0', '0',
                                  'DeclaredOnly', 'DeclaredOnly', '16', '0', '0', 'DeclaredOnly', *flags],
                                 input=b'start\n', capture_output=True, timeout=60)
            def read(filename):
                return json.loads((output / filename).read_text()) if (output / filename).is_file() else None
            return run, read('scene.json'), read('import-report.json'), read(SIDECAR)

    def test_actual_core_limits_and_legacy_payload_are_preserved(self):
        run, baseline, original_report, original_resource = self.invoke()
        self.assertEqual(0, run.returncode, run.stderr)
        self.assertIsNone(original_resource)
        run, scene, report, resources = self.invoke('--max-exact-work-units', '4000000', '--deadline-ms', '180000')
        self.assertEqual(0, run.returncode, run.stderr)
        self.assertEqual(baseline, scene)
        self.assertEqual(original_report['quality'], report['quality'])
        self.assertEqual(report['exact_geometry_work'], resources['exact_geometry_work'])
        self.assertGreater(resources['exact_geometry_work']['used_units'], 0)
        self.assertEqual(4000000, resources['exact_geometry_work']['maximum_units'])
        self.assertEqual(180000, resources['deadline_ms'])
        self.assertTrue(resources['deadline_configured'])

    def test_tiny_budget_refuses_whole_request_and_does_not_retry(self):
        run, scene, report, resources = self.invoke('--max-exact-work-units', '1')
        self.assertNotEqual(0, run.returncode)
        self.assertEqual('ResourceLimit', report['status'])
        self.assertTrue(report['candidate_discarded'])
        self.assertEqual(1, resources['max_exact_work_units'])
        self.assertNotEqual('Ready', scene['status'])

    def test_invalid_duplicate_native_arguments_never_start_capture(self):
        for flags in (('--deadline-ms', '0'), ('--deadline-ms', ''), ('--max-exact-work-units', '+3'),
                      ('--deadline-ms', '2147483648'), ('--max-exact-work-units', '9223372036854775808'),
                      ('--deadline-ms', '90000', '--deadline-ms', '90000')):
            with self.subTest(flags=flags):
                run, scene, report, resources = self.invoke(*flags)
                self.assertEqual(2, run.returncode)
                self.assertIsNone(scene)
                self.assertIsNone(report)
                self.assertIsNone(resources)

    def test_loader_owns_applied_resources_after_worker_cleanup(self):
        self.loader = SceneLoader(ImportSettings(max_exact_work_units=4000000, deadline_ms=180000, timeout_seconds=210))
        self.loader.started.connect(lambda _: setattr(self.loader, 'settings', ImportSettings(timeout_seconds=1)))
        try:
            self.load(FIXTURE)
            observed = self.loader.worker_result['execution_resources']
            self.assertEqual(4000000, observed['max_exact_work_units'])
            self.assertEqual(210, self.loader.worker_result['worker_timeout_seconds'])
            self.assertFalse(self.loader.jobs)
        finally:
            self.loader.close()
            QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)

    def test_scan_child_forwards_resources_and_keeps_independent_file_timeout(self):
        from tools.smlib_viewer.scan import run_case
        from tools.smlib_viewer.tests.test_scan import options
        with tempfile.TemporaryDirectory(prefix='public-execution-scan-') as name:
            observed = run_case(test_topology.BOX.resolve(), Path(name),
                                options(max_exact_work_units=4000000, deadline_ms=180000,
                                        worker_timeout_seconds=210, timeout=240), lambda _: None)
        self.assertEqual('Rendered', observed['outcome'])
        self.assertEqual(180000, observed['worker']['execution_resources']['deadline_ms'])
        self.assertEqual(210, observed['worker']['worker_timeout_seconds'])
        self.assertTrue(observed['temporary_payloads_cleaned'])


class ExecutionAdoptionTests(unittest.TestCase):
    setUpClass = classmethod(test_topology.TopologyAdoptionTests.setUpClass.__func__)
    setUp = test_topology.TopologyAdoptionTests.setUp
    request = test_topology.TopologyAdoptionTests.request
    write_manifest = staticmethod(test_topology.TopologyAdoptionTests.write_manifest)

    def test_missing_or_mismatched_resources_refuse_before_mesh_decode(self):
        self.loader.settings = ImportSettings(max_exact_work_units=4000000, deadline_ms=180000, timeout_seconds=210)
        for missing in (True, False):
            generation, directory, manifest = self.request()
            manifest['source_sha256'] = 'a' * 64
            self.write_manifest(directory, manifest)
            if not missing:
                bad = receipt()
                bad['deadline_ms'] = 90000
                (directory / SIDECAR).write_text(json.dumps(bad), encoding='utf-8')
            with patch('pyvista.read', side_effect=AssertionError('must validate before mesh decode')) as decode:
                self.loader._adopt(generation, 0)
            decode.assert_not_called()
            self.assertFalse(self.loaded)
            self.assertTrue(self.failed)

    def test_failure_owns_resource_observations_without_masking_primary(self):
        self.loader.settings = ImportSettings(max_exact_work_units=4000000, deadline_ms=180000, timeout_seconds=210)
        for damaged in (False, True):
            generation, directory, manifest = self.request()
            manifest.update(status='DisplayFailure', native_import_status='Accepted',
                            key='PublicDisplayFailure', source_sha256='a' * 64)
            self.write_manifest(directory, manifest)
            (directory / SIDECAR).write_text('{}' if damaged else json.dumps(receipt()), encoding='utf-8')
            self.loader._adopt(generation, 1)
            self.assertFalse(self.loaded)
            self.assertEqual('PublicDisplayFailure', self.loader.worker_result['key'])
            self.assertEqual(damaged, 'execution_resource_report_error' in self.loader.worker_result)
            if not damaged:
                self.assertEqual(receipt(), self.loader.worker_result['execution_resources'])
