# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Readable shape without edge lines, through the real viewer redraw path."""
import os
import time
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "windows" if os.name == "nt" else "offscreen")

import numpy as np
import pyvista as pv
from PySide6 import QtGui, QtWidgets
from vtkmodules.vtkRenderingCore import vtkCoordinate

from tools.smlib_viewer.app import ViewerWindow


def luminance_at(window, image, point):
    coordinate = vtkCoordinate()
    coordinate.SetCoordinateSystemToWorld()
    coordinate.SetValue(*point)
    x, y = coordinate.GetComputedDoubleDisplayValue(window.plot.renderer)
    x, y = round(x), image.shape[0] - 1 - round(y)
    rgba = image[y-3:y+4, x-3:x+4]
    if rgba.shape[-1] == 4 and np.any(rgba[:, :, 3] < 240):
        raise AssertionError("Surface sample missed model coverage")
    patch = rgba[:, :, :3].astype(float)
    if patch.size != 7 * 7 * 3:
        raise AssertionError("Public surface sample outside framebuffer")
    return float((patch @ np.array([.2126, .7152, .0722])).mean())


class ShadingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.window = ViewerWindow()
        self.window.show()

    def tearDown(self):
        self.window.close()

    def load_mesh(self, mesh):
        # Match the worker's geometry-only mesh: no inherited material/normals
        # may accidentally conceal the viewer's lighting regression.
        mesh.clear_data()
        self.window.load_started = time.perf_counter()
        self.window._loaded("public-synthetic.vtk", mesh, None, {"role": "Mesh", "units": "unknown"})
        self.assertIs(mesh, self.window.mesh)
        self.assertFalse(self.window.edges_button.isChecked())
        self.assertFalse(self.window.wire_button.isChecked())

    def test_box_faces_remain_distinct_without_lines_after_redraw(self):
        mesh = pv.Cube().triangulate().clean()
        self.load_mesh(mesh)
        points, faces = mesh.points.copy(), mesh.faces.copy()
        lights = tuple(self.window.plot.renderer.lights)
        for _ in range(2):
            self.window._redraw()
            self.window.named_view("isometric")
            self.assertEqual(tuple(self.window.plot.renderer.lights), lights)
            image = self.window.plot.screenshot(return_img=True, transparent_background=True)
            values = [luminance_at(self.window, image, p) for p in [(0.5, 0, 0), (0, -0.5, 0), (0, 0, 0.5)]]
            self.assertGreater(np.ptp(values), 30, f"Unlit/flat box face luminances: {values}")
            self.assertGreater(min(values), 25)
            self.assertLess(max(values), 245)
        np.testing.assert_array_equal(mesh.points, points)
        np.testing.assert_array_equal(mesh.faces, faces)

    def test_cylinder_has_curved_shading_without_lines(self):
        mesh = pv.Cylinder(direction=(0, 0, 1), radius=1, height=2, resolution=96).triangulate().clean()
        self.load_mesh(mesh)
        image = self.window.plot.screenshot(return_img=True, transparent_background=True)
        points = [(np.cos(a), np.sin(a), 0) for a in np.deg2rad([-100, -60, -20, 20])]
        values = [luminance_at(self.window, image, p) for p in points]
        self.assertGreater(np.ptp(values), 25, f"Flat cylinder side luminances: {values}")

    def test_hud_repaint_is_opaque_after_resize_and_reload(self):
        mesh = pv.Cube().triangulate().clean()
        self.load_mesh(mesh)
        for width, height in [(520, 360), (900, 500), (520, 360)]:
            self.window.resize(width, height)
            self.application.processEvents()
            self.load_mesh(mesh)
            image = self.window.hud.grab().toImage().convertToFormat(QtGui.QImage.Format_RGBA8888)
            pixels = np.frombuffer(image.constBits(), dtype=np.uint8).reshape(image.height(), image.bytesPerLine())
            rgba = pixels[:, :image.width() * 4].reshape(image.height(), image.width(), 4)
            # Exclude rounded corners. Every interior pixel, including the
            # background between glyphs, must overwrite the previous Qt frame.
            # A native VTK framebuffer cannot provide Qt's alpha-blend backdrop.
            self.assertTrue(np.all(rgba[12:-12, 12:-12, 3] == 255), "HUD retains translucent repaint pixels")
