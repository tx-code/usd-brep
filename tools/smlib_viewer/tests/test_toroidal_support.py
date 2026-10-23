# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Original-basis toroidal supports retain live selection and native measurements."""
import math
from pathlib import Path
import tempfile
import unittest

from PySide6 import QtCore, QtWidgets
from . import test_topology
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker
from tools.step_import_native_validation.tests.test_toroidal_support import inner_spindle_source, spindle_cap_source


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native worker')
class ToroidalSupportWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load
    close_loader = test_topology.NativeTopologyTests.close_loader

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings(restrict_domains=True))
        self.addCleanup(self.close_loader)

    def check_cap(self, data, area, length):
        from tools.smlib_viewer.app import ViewerWindow
        with tempfile.TemporaryDirectory(prefix='viewer-public-torus-') as folder:
            source = Path(folder) / 'public.step'
            source.write_bytes(data)
            _, mesh, boundary, metadata = self.load(source)
            self.assertEqual(data, source.read_bytes())
        self.assertEqual('Accepted', metadata['native_import_status'])
        topology = metadata['_topology']
        self.assertTrue(topology.face_labels_complete)
        self.assertEqual((3, 3, 2), (len(topology.faces), len(topology.edges), len(topology.vertices)))
        self.assertEqual(set(range(3)), set(mesh.cell_data['TopoFace']))
        self.assertEqual(set(range(3)), set(boundary.cell_data['TopoEdge']))
        self.assertTrue(all(item.measurement_status == 'Available' for item in (*topology.faces, *topology.edges)))
        self.assertAlmostEqual(area, sum(f.area_mm2 for f in topology.faces), delta=0.03)
        self.assertAlmostEqual(length, sum(e.length_mm for e in topology.edges), delta=0.002)
        window = ViewerWindow(self.loader.settings)
        try:
            window._loaded('public.step', mesh, boundary, metadata)
            self.assertNotIn('已修复', window.hud.text())
            for kind, count in (('face', 3), ('edge', 3), ('vertex', 2)):
                for index in range(count):
                    self.assertIsNotNone(window.picker.resolve(kind, index))
        finally:
            window.close()
            QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)

    def test_outer_sheet_keeps_complete_topology_and_measurements(self):
        self.check_cap(spindle_cap_source(), 2 * math.pi**2 + 18 * math.pi, 9 * math.pi)

    def test_inner_sheet_keeps_source_orientation_and_measurements(self):
        self.check_cap(inner_spindle_source(), math.pi * (9 - 2 * math.sqrt(3)) - 2 * math.pi**2 / 3,
                       2 * math.pi * math.sqrt(3) + math.pi / 3)
