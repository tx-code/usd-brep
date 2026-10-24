# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Original-bound schema choice crosses the real worker without changing source."""
import contextlib
import hashlib
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from PySide6 import QtCore, QtWidgets

from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker
from tools.smlib_viewer.cli import main
from tools.smlib_viewer.interpretation import source_identity_arguments, validate_source_identity_receipt
from tools.smlib_viewer.scan import run_scan
from tools.smlib_viewer.tests import test_topology
from tools.smlib_viewer.tests.test_scan import options
from tools.step_semantic_validation.tests.test_source_bound_interpretation import malformed_box

POLICY = 'SourceBoundAp214As2010'


def settings(data):
    return ImportSettings(schema_interpretation=POLICY, schema_source_sha256=hashlib.sha256(data).hexdigest())


class SourceIdentityOptionsTests(unittest.TestCase):
    def test_identity_is_explicit_canonical_and_only_used_with_its_policy(self):
        self.assertEqual([], source_identity_arguments(ImportSettings()))
        self.assertEqual(['--schema-source-sha256', 'a' * 64], source_identity_arguments(
            ImportSettings(schema_interpretation=POLICY, schema_source_sha256='a' * 64)))
        for identity in ('', 'a' * 63, 'A' * 64, 'g' * 64, None, 64):
            with self.subTest(identity=identity), self.assertRaises(ValueError):
                ImportSettings(schema_interpretation=POLICY, schema_source_sha256=identity)
        for policy in ('DeclaredOnly', 'Ap214NameOnlyAs2010', 'UnregisteredAp214As2010'):
            with self.subTest(policy=policy), self.assertRaises(ValueError):
                ImportSettings(schema_interpretation=policy, schema_source_sha256='a' * 64)
        with self.assertRaises(ValueError):
            ImportSettings(strict_source=True, schema_interpretation=POLICY, schema_source_sha256='a' * 64)

    def test_cli_rejects_missing_or_non_step_file_and_scan_before_startup(self):
        flags = ['--schema-interpretation', POLICY, '--schema-source-sha256', 'a' * 64]
        for args in (flags, ['public.stl', *flags], ['--scan', '.', *flags],
                     ['public.step', '--schema-interpretation', POLICY],
                     ['public.step', *flags, '--strict-source'],
                     ['public.step', '--schema-source-sha256', 'a' * 64]):
            with (self.subTest(args=args), contextlib.redirect_stderr(io.StringIO()),
                  self.assertRaises(SystemExit) as error):
                main(args)
            self.assertEqual(2, error.exception.code)

    def test_scan_api_refuses_before_creating_output(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'not-created'
            with self.assertRaisesRegex(ValueError, 'SourceBoundSchemaRequiresOneExplicitStepFile'):
                run_scan(options(scan=[Path(folder)], output=output,
                                 schema_interpretation=POLICY, schema_source_sha256='a' * 64))
            self.assertFalse(output.exists())

    def test_scene_receipt_cannot_substitute_policy_identity_or_qualification(self):
        request = settings(b'public original')
        scene = dict(requested_schema_interpretation=POLICY, applied_schema_interpretation=POLICY,
                     source_sha256=request.schema_source_sha256, native_import_status='AcceptedUnderInterpretation')
        validate_source_identity_receipt(scene, request)
        for key, value in (('requested_schema_interpretation', 'DeclaredOnly'),
                           ('applied_schema_interpretation', 'DeclaredOnly'),
                           ('source_sha256', '0' * 64), ('native_import_status', 'Accepted')):
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'SourceBoundSchemaReceiptMismatch'):
                validate_source_identity_receipt({**scene, key: value}, request)
        with self.assertRaisesRegex(ValueError, 'UnexpectedSourceBoundSchemaReceipt'):
            validate_source_identity_receipt(scene, ImportSettings())
        validate_source_identity_receipt({}, ImportSettings())


class SourceIdentityAdoptionTests(unittest.TestCase):
    setUpClass = classmethod(test_topology.TopologyAdoptionTests.setUpClass.__func__)
    setUp = test_topology.TopologyAdoptionTests.setUp
    request = test_topology.TopologyAdoptionTests.request
    write_manifest = staticmethod(test_topology.TopologyAdoptionTests.write_manifest)

    def test_mismatched_receipt_keeps_previous_complete_scene_and_releases_payloads(self):
        generation, path, _ = self.request()
        self.loader._adopt(generation, 0)
        self.assertEqual(1, len(self.loaded))
        previous = self.loaded[0]
        self.loader.settings = settings(b'public original')
        generation, path, manifest = self.request()
        manifest.update(requested_schema_interpretation=POLICY, applied_schema_interpretation=POLICY,
                        source_sha256='0' * 64, native_import_status='AcceptedUnderInterpretation')
        self.write_manifest(path, manifest)
        with patch('pyvista.read', side_effect=AssertionError('Must reject before decoding meshes')):
            self.loader._adopt(generation, 0)
        self.assertEqual(1, len(self.loaded))
        self.assertIs(previous, self.loaded[0])
        self.assertIn('SourceBoundSchemaReceiptMismatch', self.failed[-1][1])
        self.assertFalse(path.exists())


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native display worker')
class SourceIdentityWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings())
        self.addCleanup(self.close_loader)

    def close_loader(self):
        self.loader.close()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)

    def test_original_bound_worker_retains_source_and_selectable_native_topology(self):
        source = malformed_box()
        self.loader.settings = settings(source)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'public.step'
            path.write_bytes(source)
            _, mesh, boundary, metadata = self.load(path)
            validate_source_identity_receipt(metadata, self.loader.settings)
            self.assertEqual('AcceptedUnderInterpretation', metadata['native_import_status'])
            self.assertEqual((6, 12, 8), tuple(metadata[key] for key in ('faces', 'edges', 'vertices')))
            self.assertEqual(set(range(6)), set(mesh.cell_data['TopoFace']))
            self.assertEqual(set(range(12)), set(boundary.cell_data['TopoEdge']))
            topology = metadata['_topology']
            self.assertEqual([10] * 4 + [20] * 4 + [30] * 4, sorted(edge.length_mm for edge in topology.edges))
            self.assertEqual(source, path.read_bytes())
            from tools.smlib_viewer.app import ViewerWindow
            window = ViewerWindow(self.loader.settings)
            try:
                window._loaded(str(path), mesh, boundary, metadata)
                self.assertIn('源解释模式', window.hud.text())
            finally:
                window.close()

    def test_wrong_original_and_registered_source_are_not_retried(self):
        registered = malformed_box().replace(b'{1 0 10303 214 -1 1 1 1}', b'{1 0 10303 214 3 1 1}')
        for source, request, diagnostic in (
                (malformed_box(), settings(b'another original'), 'SourceInterpretationInputMismatch'),
                (registered, settings(registered), 'SourceInterpretationNotApplicable')):
            with self.subTest(wrong_original=source == malformed_box()), tempfile.TemporaryDirectory() as folder:
                path = Path(folder) / 'public.step'
                path.write_bytes(source)
                self.loader.settings = request
                with self.assertRaises(AssertionError):
                    self.load(path)
                self.assertNotEqual('Ready', self.loader.worker_result['status'])
                self.assertIn(diagnostic, [d['key'] for d in self.loader.worker_result['diagnostics']])
                self.assertEqual(source, path.read_bytes())

    def test_assembly_receipt_binds_complete_repeated_instances(self):
        fixture = (test_topology.REPO / 'source/STEP_SEMANTICS/tests/fixtures/assembly_repeated_box.step')
        source = fixture.read_bytes().replace(b'{1 0 10303 214 3 1 1}', b'{1 0 10303 214 -1 1 1 1}')
        self.loader.settings = ImportSettings(assembly=True, schema_interpretation=POLICY,
                                             schema_source_sha256=hashlib.sha256(source).hexdigest())
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'public.step'
            path.write_bytes(source)
            _, mesh, _, metadata = self.load(path)
            validate_source_identity_receipt(metadata, self.loader.settings)
            self.assertEqual('brep-view-scene.v6', metadata['format'])
            self.assertEqual(2, len(metadata['_topology'].bodies))
            self.assertEqual(12, len(metadata['_topology'].faces))
            self.assertEqual(set(range(12)), set(mesh.cell_data['TopoFace']))
            self.assertEqual(source, path.read_bytes())

    def test_raw_worker_refuses_invalid_duplicate_or_unused_identity_before_capture(self):
        for schema, flags in ((POLICY, []), (POLICY, ['--schema-source-sha256', 'A' * 64]),
                              (POLICY, ['--schema-source-sha256', 'a' * 64] * 2),
                              ('DeclaredOnly', ['--schema-source-sha256', 'a' * 64])):
            with self.subTest(schema=schema, flags=flags), tempfile.TemporaryDirectory() as folder:
                output = Path(folder)
                # A nonexistent input also establishes that invalid transport
                # refuses before opening or interpreting any source bytes.
                run = subprocess.run([str(native_worker()), str(output / 'absent.step'), str(output),
                                      'development', '0', '0', '0', schema, 'DeclaredOnly', '16', '0', '0',
                                      'DeclaredOnly', *flags], input=b'start\n', capture_output=True, timeout=20)
                self.assertEqual(2, run.returncode, run.stderr)
                self.assertFalse((output / 'scene.json').exists())
