# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Actual native extrusion worker, surface roles and selectable native topology."""
import math
import unittest

from . import test_topology
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker

from PySide6 import QtWidgets


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native display worker')
class LinearExtrusionWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load
    close_loader = test_topology.NativeTopologyTests.close_loader

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings())
        self.addCleanup(self.close_loader)

    def test_analytic_extrusions_preserve_role_measurements_and_native_selection(self):
        from tools.smlib_viewer.app import ViewerWindow
        folder = test_topology.REPO / 'source/STEP_IMPORT/tests/fixtures'
        cases = [('patch', 160, 'OpenSurfaceModel', (1, 4, 4)),
                 ('parabolic', math.sqrt(5) + math.asinh(2) / 2, 'OpenSurfaceModel', (1, 4, 4)),
                 ('rational', 6 * math.pi, 'OpenSurfaceModel', (1, 4, 4)),
                 ('box', 2200, 'MaterialSolid', (6, 12, 8))]
        for name, area, role, counts in cases:
            with self.subTest(name=name):
                source = folder / f'linear_extrusion_{name}.step'
                original = source.read_bytes()
                _, mesh, boundary, metadata = self.load(source)
                self.assertEqual(original, source.read_bytes())
                self.assertEqual('Accepted', metadata['native_import_status'])
                self.assertEqual(role, metadata['role'])
                topology = metadata['_topology']
                self.assertEqual(counts, (len(topology.faces), len(topology.edges), len(topology.vertices)))
                self.assertAlmostEqual(area, sum(face.area_mm2 for face in topology.faces), delta=area * 2e-4)
                self.assertFalse(metadata.get('repair_applied', False))
                window = ViewerWindow(self.loader.settings)
                try:
                    window._loaded(str(source), mesh, boundary, metadata)
                    for kind in ('face', 'edge', 'vertex'):
                        self.assertIsNotNone(window.picker.resolve(kind, 0))
                    self.assertNotIn('已修复', window.hud.text())
                finally:
                    window.close()


if __name__ == '__main__':
    unittest.main()
