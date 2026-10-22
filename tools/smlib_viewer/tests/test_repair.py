# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Single-file repair intent transport and visible repaired qualification."""
from dataclasses import replace
import hashlib
from pathlib import Path
import tempfile
import unittest

from . import test_topology
from tools.step_import_native_validation.tests.test_orientation_repair import broken_roof
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker
from tools.smlib_viewer.repair import arguments
from PySide6 import QtCore, QtWidgets


def settings(data):
    return ImportSettings(repair_source_sha256=hashlib.sha256(data).hexdigest(), maximum_face_orientation_edits=1,
                          maximum_repair_proof_controls=10000, maximum_repair_proof_products=1000000,
                          face_orientation_edits=('7:21:517:F:T',))


class RepairIntentTests(unittest.TestCase):
    def test_no_default_allowance_and_closed_identity_budget_contract(self):
        self.assertEqual([], arguments(ImportSettings()))
        good = settings(broken_roof())
        self.assertIn('7:21:517:F:T', arguments(good))
        for edit in ('7:21:517:F:F', '07:21:517:F:T', '7:21:517:T', '7:21:517:F:T:extra'):
            with self.subTest(edit=edit), self.assertRaises(ValueError):
                replace(good, face_orientation_edits=(edit,))
        for fields in (dict(maximum_face_orientation_edits=0), dict(repair_source_sha256=''),
                       dict(normalize_periodic=True), dict(restrict_domains=True),
                       dict(face_orientation_edits=good.face_orientation_edits * 2)):
            with self.subTest(fields=tuple(fields)), self.assertRaises(ValueError):
                replace(good, **fields)


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native display worker')
class RepairWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load
    close_loader = test_topology.NativeTopologyTests.close_loader

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings())
        self.addCleanup(self.close_loader)

    def test_worker_requires_plan_and_displays_repaired_status_after_complete_adoption(self):
        from tools.smlib_viewer.app import ViewerWindow
        data = broken_roof()
        self.loader.settings = settings(data)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'public.step'
            path.write_bytes(data)
            _, mesh, boundary, metadata = self.load(path)
            self.assertEqual(data, path.read_bytes())
        self.assertEqual('AcceptedWithRepairs', metadata['native_import_status'])
        self.assertTrue(metadata['repair_applied'])
        self.assertEqual(1, metadata['repaired_face_count'])
        window = ViewerWindow(self.loader.settings)
        try:
            window._loaded('public.step', mesh, boundary, metadata)
            self.assertIn('已修复 1 处面朝向', window.hud.text())
            self.assertNotIn('源解释模式', window.hud.text())
            self.assertIsNotNone(window.picker.resolve('face', 0))
        finally:
            window.close()
