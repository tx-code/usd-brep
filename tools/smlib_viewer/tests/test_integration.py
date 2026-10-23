# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit stopping controls traverse the real worker, not just its caption."""
import contextlib
import copy
from fractions import Fraction
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

os.environ.setdefault('PYVISTA_OFF_SCREEN', 'true')

from PySide6 import QtCore, QtWidgets

from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker
from tools.smlib_viewer.cli import main
from tools.smlib_viewer.tests import test_topology
from tools.step_import_native_validation.tests.test_integration import bernstein_roof_source

BOX = test_topology.BOX


class IntegrationRequests(unittest.TestCase):
    def test_settings_reject_invalid_controls_without_clamping(self):
        self.assertIsNone(ImportSettings().integration_relative_accuracy)
        for value in (True, '1e-8', float('nan'), float('inf'), 0, -1, 1e-9, 1e-3):
            with self.subTest(value=value), self.assertRaises(ValueError):
                ImportSettings(integration_relative_accuracy=value)
        for value in (1e-8, 1e-6, 1e-4):
            self.assertEqual(value, ImportSettings(integration_relative_accuracy=value).integration_relative_accuracy)

    def test_cli_invalid_controls_fail_before_gui_startup(self):
        for value in ('nan', 'inf', '0', '1e-9', '1e-3', '1e-6garbage'):
            with (self.subTest(value=value), contextlib.redirect_stderr(io.StringIO()),
                  self.assertRaises(SystemExit) as error):
                main(['--integration-relative-accuracy', value])
            self.assertEqual(2, error.exception.code)

    def test_receipt_binds_request_and_actual_observations(self):
        from tools.smlib_viewer.integration import validate_receipt
        settings = ImportSettings(integration_relative_accuracy=1e-8)
        manifest = dict(role='MaterialSolid', numerical_integration=dict(
            requested_relative_accuracy=1e-8, applied_relative_accuracy=1e-8, completed=True,
            continuous_error_bound_established=False, control_scope='DimensionlessNumericalStoppingOnly'))
        validate_receipt(manifest, settings)
        for field, value in (('requested_relative_accuracy', 1e-4), ('applied_relative_accuracy', 1e-4),
                             ('completed', False), ('continuous_error_bound_established', True),
                             ('completed', 1), ('control_scope', 'GeometricTolerance')):
            changed = copy.deepcopy(manifest)
            changed['numerical_integration'][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                validate_receipt(changed, settings)
        with self.assertRaises(ValueError):
            validate_receipt({}, settings)
        with self.assertRaises(ValueError):
            validate_receipt(manifest, ImportSettings())
        # Open surfaces have no fused material integral. A request may be
        # retained, but must not acquire a fabricated applied/completed value.
        manifest['role'] = 'OpenSurfaceModel'
        manifest['numerical_integration'].update(applied_relative_accuracy=None, completed=False)
        validate_receipt(manifest, settings)


@unittest.skipUnless(native_worker().is_file(), 'matching native viewer worker required')
class IntegrationWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix='viewer-integration-') as name:
            root = Path(name)
            source, output = root / 'public.step', root / 'output'
            source.write_bytes(data)
            output.mkdir()
            process = subprocess.run([str(native_worker()), str(source), str(output), 'development', '0', '0', '1',
                                      'DeclaredOnly', 'DeclaredOnly', '16', '0', '0', 'DeclaredOnly', *flags],
                                     input=b'start\n', capture_output=True, timeout=60)
            manifest = json.loads((output / 'scene.json').read_text()) if (output / 'scene.json').is_file() else None
            report_file = output / 'import-report.json'
            report = json.loads(report_file.read_text()) if report_file.is_file() else None
            self.assertEqual(data, source.read_bytes())
            return process, manifest, report

    def test_worker_roundtrip_and_bernstein_truth(self):
        run, manifest, report = self.invoke(bernstein_roof_source(), '--integration-relative-accuracy', '1e-8')
        self.assertEqual(0, run.returncode, run.stderr)
        self.assertEqual(1e-8, manifest['numerical_integration']['applied_relative_accuracy'])
        self.assertTrue(manifest['numerical_integration']['completed'])
        self.assertEqual(1e-8, report['numerical_integration']['applied_relative_accuracy'])
        # Integral B_i^10 = 1/11 in each parameter; exact affine XY Jacobian=200.
        truth = float(Fraction(6000) + Fraction(200, 121))
        self.assertAlmostEqual(truth, report['model']['material_volume']['value_mm3'], delta=truth * 1e-8)

    def test_default_payload_and_explicit_legacy_control(self):
        run, default, baseline = self.invoke(BOX.read_bytes())
        explicit_run, explicit, report = self.invoke(BOX.read_bytes(), '--integration-relative-accuracy', '1e-4')
        self.assertEqual((0, 0), (run.returncode, explicit_run.returncode))
        self.assertNotIn('numerical_integration', default)
        self.assertTrue(explicit.pop('numerical_integration')['completed'])
        self.assertEqual(default, explicit)
        report.pop('numerical_integration')
        self.assertEqual(baseline, report)

    def test_invalid_duplicate_worker_controls_and_source_failure(self):
        for flags in (('--integration-relative-accuracy', 'nan'), ('--integration-relative-accuracy', '1e-9'),
                      ('--integration-relative-accuracy', '1e-8', '--integration-relative-accuracy', '1e-8')):
            with self.subTest(flags=flags):
                run, manifest, _ = self.invoke(BOX.read_bytes(), *flags)
                self.assertEqual(2, run.returncode)
                self.assertIsNone(manifest)
        run, manifest, _ = self.invoke(b'not STEP', '--integration-relative-accuracy', '1e-8')
        self.assertNotEqual(0, run.returncode)
        self.assertEqual(
            dict(requested_relative_accuracy=1e-8, applied_relative_accuracy=None, completed=False,
                 continuous_error_bound_established=False, control_scope='DimensionlessNumericalStoppingOnly'),
            manifest['numerical_integration'])

    def test_loader_adopts_explicit_assembly_and_keeps_pick_query_policy_separate(self):
        fixture = (Path(__file__).resolve().parents[3]
                   / 'source/STEP_SEMANTICS/tests/fixtures/assembly_repeated_box.step')
        self.loader = SceneLoader(ImportSettings(assembly=True, integration_relative_accuracy=1e-8))
        try:
            _, _, _, metadata = self.load(fixture)
            self.assertTrue(metadata['numerical_integration']['completed'])
            self.assertEqual(1e-8, metadata['numerical_integration']['applied_relative_accuracy'])
            # Picking uses its own documented face query; do not relabel its
            # 1e-4 stopping setting with the whole-model fused-integral request.
            self.assertEqual(1e-4, metadata['_topology'].face_area_relative_accuracy)
        finally:
            self.loader.close()
            QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)

    def test_scan_child_forwards_explicit_stopping_control(self):
        from tools.smlib_viewer.scan import run_case
        from tools.smlib_viewer.tests.test_scan import options
        with tempfile.TemporaryDirectory(prefix='integration-scan-') as name:
            result = run_case(BOX.resolve(), Path(name), options(integration_relative_accuracy=1e-7), lambda _: None)
        self.assertEqual('Rendered', result['outcome'])
        self.assertEqual(1e-7, result['worker']['numerical_integration']['applied_relative_accuracy'])
        self.assertTrue(result['temporary_payloads_cleaned'])


class IntegrationAdoptionTests(unittest.TestCase):
    setUpClass = classmethod(test_topology.TopologyAdoptionTests.setUpClass.__func__)
    setUp = test_topology.TopologyAdoptionTests.setUp
    request = test_topology.TopologyAdoptionTests.request
    write_manifest = staticmethod(test_topology.TopologyAdoptionTests.write_manifest)

    def test_wrong_or_missing_request_evidence_keeps_previous_scene(self):
        self.loader.settings = ImportSettings(integration_relative_accuracy=1e-8)
        observation = dict(requested_relative_accuracy=1e-8, applied_relative_accuracy=1e-8, completed=True,
                           continuous_error_bound_established=False, control_scope='DimensionlessNumericalStoppingOnly')
        generation, path, manifest = self.request()
        manifest['numerical_integration'] = observation
        self.write_manifest(path, manifest)
        self.loader._adopt(generation, 0)
        self.assertEqual(1, len(self.loaded))
        previous = self.loaded[-1]
        for changed in (None, observation | {'applied_relative_accuracy': 1e-4},
                        observation | {'completed': False}, observation | {'requested_relative_accuracy': 1e-4}):
            generation, path, manifest = self.request()
            manifest['role'] = 'MaterialSolid'
            if changed:
                manifest['numerical_integration'] = changed
            self.write_manifest(path, manifest)
            self.loader._adopt(generation, 0)
            self.assertFalse(path.exists())
            self.assertEqual(1, len(self.loaded))
            self.assertIs(previous, self.loaded[-1])
        self.assertEqual(4, len(self.failed))
