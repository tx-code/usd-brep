# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""A finer native work zone must be explicitly requested and bound to the scene."""
from pathlib import Path
import tempfile
import unittest

from PySide6 import QtWidgets
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker
from tools.smlib_viewer.quality import FINE_WORK_QUALITY_ID, validate_receipt
from tools.step_import_native_validation.tests.test_fine_work_quality import thin_box_source
from . import test_topology


class FineWorkReceiptTests(unittest.TestCase):
    def test_ready_scene_cannot_borrow_another_profile(self):
        settings = ImportSettings(profile='fine-work-tolerance-study')
        validate_receipt(dict(quality_id=FINE_WORK_QUALITY_ID), settings)
        for manifest in ({}, dict(quality_id='Q-AP214-development-mm-01')):
            with self.assertRaises(ValueError):
                validate_receipt(manifest, settings)
        with self.assertRaises(ValueError):
            validate_receipt(dict(quality_id=FINE_WORK_QUALITY_ID), ImportSettings())
        validate_receipt({}, ImportSettings())


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native worker')
class FineWorkWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load
    close_loader = test_topology.NativeTopologyTests.close_loader

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def test_explicit_fine_profile_renders_complete_thin_solid_and_retains_native_measurements(self):
        from tools.smlib_viewer.app import ViewerWindow
        data = thin_box_source()
        self.loader = SceneLoader(ImportSettings(profile='fine-work-tolerance-study'))
        self.addCleanup(self.close_loader)
        with tempfile.TemporaryDirectory(prefix='public-fine-quality-') as folder:
            source = Path(folder) / 'thin.step'
            source.write_bytes(data)
            _, mesh, boundary, metadata = self.load(source)
            self.assertEqual(data, source.read_bytes())
        self.assertEqual(FINE_WORK_QUALITY_ID, metadata['quality_id'])
        self.assertEqual('Accepted', metadata['native_import_status'])
        self.assertEqual('MaterialSolid', metadata['role'])
        topology = metadata['_topology']
        self.assertEqual((6, 12, 8), (len(topology.faces), len(topology.edges), len(topology.vertices)))
        self.assertAlmostEqual(0.0005, min(edge.length_mm for edge in topology.edges), delta=1e-10)
        window = ViewerWindow(self.loader.settings)
        try:
            window._loaded('public.step', mesh, boundary, metadata)
            self.assertIn('研究配置', window.hud.text())
            self.assertNotIn('已修复', window.hud.text())
            for kind in ('face', 'edge', 'vertex'):
                self.assertIsNotNone(window.picker.resolve(kind, 0))
        finally:
            window.close()
