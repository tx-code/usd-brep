# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Window selection lifecycle using independently labelled synthetic geometry."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from .test_topology import box_transport
from tools.smlib_gui.runtime import QtCore, QtWidgets
from tools.smlib_viewer.app import ViewerWindow
from tools.smlib_viewer.topology import read_topology
from PySide6.QtTest import QTest


class SelectionWindowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])

    def setUp(self):
        mesh, boundary, raw, metadata = box_transport()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'topology.json'
            path.write_text(json.dumps(raw), encoding='utf-8')
            metadata['_topology'] = read_topology(path, mesh, boundary, metadata)
        self.scene = mesh, boundary, metadata
        self.window = ViewerWindow()
        self.window.show()
        self.window._loaded('synthetic.step', *self.scene)
        self.application.processEvents()

    def tearDown(self):
        self.window.close()

    def test_native_measurements_and_distance_survive_redraw(self):
        window = self.window
        window.selected = [('vertex', 0), ('vertex', 1)]
        window._restore_selection()
        self.assertIn('两点距离：10 mm', window.hud.text())
        window._redraw()
        self.assertIn('两点距离：10 mm', window.hud.text())
        window.selected = [('face', 0)]
        window._restore_selection()
        self.assertIn('≈ 200 mm²', window.hud.text())
        window.clear_selection()
        self.assertNotIn('选中', window.hud.text())

    def test_failed_replacement_retains_selection_success_clears_it(self):
        window = self.window
        window.selected = [('edge', 0)]
        window._restore_selection()
        redraw = window._redraw
        with patch.object(window, '_redraw', side_effect=[ValueError('SyntheticDisplayFailure'), None]):
            window._loaded('failed.step', *self.scene)
        redraw()
        self.assertEqual('synthetic.step', window.filename)
        self.assertEqual([('edge', 0)], window.selected)
        self.assertIn('≈ 10 mm', window.hud.text())
        window._loaded('replacement.step', *self.scene)
        self.assertEqual([], window.selected)
        self.assertNotIn('选中', window.hud.text())

    def test_device_coordinates_use_actual_framebuffer_size(self):
        window = self.window
        width, height = window.plot.render_window.GetSize()
        point = QtCore.QPointF(window.plot.interactor.width()/2, window.plot.interactor.height()/2)
        self.assertEqual((width/2, height/2-1), window._device_position(point))

    def test_mouse_click_and_control_click_select_native_vertices(self):
        window = self.window
        window.plot.camera_position = [(7, 7, 90), (7, 7, 20), (0, 1, 0)]
        window.plot.camera.parallel_scale = 18
        window._fit_depth()
        widget = window.plot.interactor

        def click(point, modifiers=QtCore.Qt.NoModifier):
            renderer = window.plot.renderer
            renderer.SetWorldPoint(*point, 1)
            renderer.WorldToDisplay()
            x, y, _ = renderer.GetDisplayPoint()
            width, height = window.plot.render_window.GetSize()
            position = QtCore.QPoint(round(x * widget.width()/width),
                                     round((height-1-y) * widget.height()/height))
            QTest.mouseClick(widget, QtCore.Qt.LeftButton, modifiers, position)

        click((2, -3, 35))
        click((12, -3, 35), QtCore.Qt.ControlModifier)
        self.assertEqual([('vertex', 4), ('vertex', 5)], window.selected)
        self.assertIn('两点距离：10 mm', window.hud.text())
        QTest.keyClick(widget, QtCore.Qt.Key_D, QtCore.Qt.ControlModifier)
        self.assertEqual([], window.selected)


if __name__ == '__main__':
    unittest.main()
