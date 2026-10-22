# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Shared ring children remain native display objects with separate measurable IDs."""
import math
from pathlib import Path
import tempfile
import unittest

from PySide6 import QtWidgets
from . import test_topology
from .test_band_repair import settings
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker
from tools.step_import_native_validation.tests.test_ring_segmentation import cone_frustum_source, phase_source


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native worker')
class RingSegmentationWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load
    close_loader = test_topology.NativeTopologyTests.close_loader

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings())
        self.addCleanup(self.close_loader)

    def adopt(self, data, intent):
        self.loader.settings = intent
        with tempfile.TemporaryDirectory(prefix='viewer-public-ring-') as folder:
            source = Path(folder) / 'public.step'
            source.write_bytes(data)
            _, mesh, boundary, metadata = self.load(source)
            self.assertEqual(data, source.read_bytes())
        return mesh, boundary, metadata

    def test_repaired_phase_cut_keeps_distinct_child_edges_and_native_measurements(self):
        from tools.smlib_viewer.app import ViewerWindow
        data = phase_source()
        mesh, boundary, metadata = self.adopt(data, settings(data))
        self.assertEqual('AcceptedWithRepairs', metadata['native_import_status'])
        topology = metadata['_topology']
        self.assertEqual((3, 4, 3), (len(topology.faces), len(topology.edges), len(topology.vertices)))
        self.assertEqual({(0., 3., 0.), (3., 0., 7.), (0., 3., 7.)},
                         {v.point_mm for v in topology.vertices})
        self.assertAlmostEqual(60 * math.pi, sum(f.area_mm2 for f in topology.faces), delta=0.04)
        self.assertAlmostEqual(12 * math.pi + 7, sum(e.length_mm for e in topology.edges), delta=0.001)
        window = ViewerWindow(self.loader.settings)
        try:
            window._loaded('public.step', mesh, boundary, metadata)
            self.assertEqual(1, window.hud.text().count('已修复 1 处周期边界角色'))
            for kind, count in (('face', 3), ('edge', 4), ('vertex', 3)):
                for index in range(count):
                    self.assertIsNotNone(window.picker.resolve(kind, index))
        finally:
            window.close()

    def test_legal_cylinder_cone_has_no_repair_and_keeps_all_source_and_derived_vertices(self):
        data = cone_frustum_source(legal=True)
        _, _, metadata = self.adopt(data, ImportSettings())
        self.assertEqual('Accepted', metadata['native_import_status'])
        self.assertNotEqual('CylindricalBandRoles', metadata.get('repair_kind'))
        topology = metadata['_topology']
        self.assertEqual((4, 6, 4), (len(topology.faces), len(topology.edges), len(topology.vertices)))
        self.assertEqual({(3., 0., 0.), (0., 3., 7.), (0., 10., 14.), (3., 0., 7.)},
                         {v.point_mm for v in topology.vertices})
        self.assertAlmostEqual((151 + 91 * math.sqrt(2)) * math.pi,
                               sum(f.area_mm2 for f in topology.faces), delta=0.2)
        self.assertAlmostEqual(32 * math.pi + 7 + 7 * math.sqrt(2),
                               sum(e.length_mm for e in topology.edges), delta=0.001)
