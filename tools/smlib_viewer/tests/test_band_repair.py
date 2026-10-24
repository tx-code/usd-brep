# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""The band repair is a one-file intent with a distinct, nonoverlapping HUD label."""
from dataclasses import replace
import hashlib
from pathlib import Path
import tempfile
import unittest

from . import test_topology
from tools.step_import_native_validation.tests.test_band_repair import FIXTURE
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker
from tools.smlib_viewer.repair import arguments
from PySide6 import QtCore, QtWidgets


def settings(data):
    return ImportSettings(band_repair_source_sha256=hashlib.sha256(data).hexdigest(), maximum_band_repair_faces=1,
                          maximum_band_bound_edits=2, maximum_band_proof_operations=100000,
                          cylindrical_band_repairs=('7:21:182:172:173',))


class BandRepairIntentTests(unittest.TestCase):
    def test_no_implicit_budget_or_scope_extension(self):
        good = settings(FIXTURE.read_bytes())
        self.assertIn('7:21:182:172:173', arguments(good))
        for fields in (dict(maximum_band_bound_edits=1), dict(band_repair_source_sha256=''),
                       dict(cylindrical_band_repairs=('7:21:182:172:172',)), dict(normalize_periodic=True),
                       dict(selected_definitions=('7',)),
                       dict(cylindrical_band_repairs=good.cylindrical_band_repairs * 2)):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                replace(good, **fields)


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native worker')
class BandRepairWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load
    close_loader = test_topology.NativeTopologyTests.close_loader

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(settings(FIXTURE.read_bytes()))
        self.addCleanup(self.close_loader)

    def test_complete_adoption_retains_distinct_repair_kind_and_native_picking(self):
        from tools.smlib_viewer.app import ViewerWindow
        original = FIXTURE.read_bytes()
        _, mesh, boundary, metadata = self.load(FIXTURE)
        self.assertEqual(original, FIXTURE.read_bytes())
        self.assertEqual('AcceptedWithRepairs', metadata['native_import_status'])
        self.assertEqual('CylindricalBandRoles', metadata['repair_kind'])
        self.assertEqual(1, metadata['repaired_face_count'])
        window = ViewerWindow(self.loader.settings)
        try:
            window._loaded('public.step', mesh, boundary, metadata)
            self.assertIn('已修复 1 处周期边界角色', window.hud.text())
            self.assertNotIn('面朝向', window.hud.text())
            self.assertIsNotNone(window.picker.resolve('face', 0))
        finally:
            window.close()

    def test_source_vertex_near_miss_is_visible_without_adopting_a_partial_scene(self):
        # This source defect is distinct from the already requested FACE.WR2
        # marker correction. The worker must preserve its ordinary refusal.
        data = FIXTURE.read_bytes().replace(b"#102=CARTESIAN_POINT('',(0.,3.,0.))",
                                            b"#102=CARTESIAN_POINT('',(0.,3.0001,0.))", 1)
        self.loader.settings = settings(data)
        errors, loaded = [], []
        loop = QtCore.QEventLoop()
        self.loader.failed.connect(lambda *args: (errors.append(args), loop.quit()))
        self.loader.loaded.connect(lambda *args: (loaded.append(args), loop.quit()))
        timer = QtCore.QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(loop.quit)
        with tempfile.TemporaryDirectory(prefix='public-source-vertex-') as folder:
            path = Path(folder) / 'source.step'
            path.write_bytes(data)
            timer.start(30000)
            try:
                self.loader.load(str(path))
                if not errors and not loaded:
                    loop.exec()
            finally:
                timer.stop()
            self.assertEqual(data, path.read_bytes())
        self.assertFalse(loaded)
        self.assertTrue(errors, 'expected a complete structured refusal')
        self.assertEqual('源顶点周期连接检查未通过', errors[0][0])
        self.assertEqual('PostcheckFailure', self.loader.worker_result['native_import_status'])
        self.assertIn('GeneralRegionSeamUnestablished', errors[0][1])
