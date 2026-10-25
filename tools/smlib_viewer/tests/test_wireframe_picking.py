# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent source-wire selection using public analytic display snapshots only."""
import math
import os
import unittest
from dataclasses import replace
from unittest.mock import patch

os.environ.setdefault('QT_QPA_PLATFORM', 'windows' if os.name == 'nt' else 'offscreen')
os.environ.setdefault('PYVISTA_OFF_SCREEN', 'true')

import numpy as np
import pyvista as pv

from tools.smlib_gui.runtime import QtWidgets
from tools.smlib_viewer.app import ViewerWindow
from tools.smlib_viewer.picking import Hit, TopologyPicker
from tools.smlib_viewer.topology import TopologyData
from tools.smlib_viewer.wireframe import WireCurveInfo, WirePointInfo, WireframeScene
from .test_picking import labelled_cube, native_boundaries, native_payload


def curve(id=0, *, instance=0, offset=0., length=math.pi, status='Available'):
    # A unit semicircle has native length pi. Its three display samples have a
    # different chord sum; deriving the HUD length from them would be incorrect.
    return WireCurveInfo(
        id=id, source_id='101', basis_id='102', instance_index=instance, definition_index=0, set_id='100',
        parameter_domain=(0., math.pi), source_forward_in_native=True,
        parameters=(0., math.pi / 2, math.pi),
        polyline_mm=((-1. + offset, 0., 0.), (offset, 1., 0.), (1. + offset, 0., 0.)),
        length_mm=length, measurement_status=status, length_absolute_accuracy_mm=1e-5,
    )


def wire_scene(curves=None, points=None):
    if curves is None:
        curves = (curve(),)
    if points is None:
        points = (WirePointInfo(0, '103', 0, 0, '100', (0., -1., 0.)),)
    instances = {item.instance_index for item in (*curves, *points)}
    return WireframeScene(tuple(curves), tuple(points), len(instances), len(instances), 'a' * 64)


def wire_topology(scene):
    return TopologyData((), (), (), True, 1e-4, 1e-5, wireframe=scene)


def assembly_metadata(topology):
    wire = topology.wireframe
    return dict(role='Assembly', units='mm', instance_count=wire.instance_count + 1,
                body_count=len(topology.bodies), wireframe_instance_count=wire.instance_count,
                faces=len(topology.faces), edges=len(topology.edges), vertices=len(topology.vertices),
                _topology=topology)


class WireframePickingTests(unittest.TestCase):
    def setUp(self):
        self.plot = pv.Plotter(off_screen=True, window_size=(480, 400))
        self.picker = TopologyPicker(self.plot)
        self.scene_actors = []

    def tearDown(self):
        self.picker.clear()
        self.plot.close()

    def adopt(self, scene, *, body=False):
        mesh = labelled_cube() if body else pv.PolyData()
        boundary = native_boundaries() if body else None
        actor = self.plot.add_mesh(mesh) if body else None
        topology = replace(native_payload(), wireframe=scene) if body else wire_topology(scene)
        self.scene_actors = scene.render(self.plot)
        bounds = scene.combined_bounds(mesh.bounds if body else None)
        center = (np.asarray(bounds)[::2] + np.asarray(bounds)[1::2]) / 2
        self.plot.camera_position = [tuple(center + (0., 0., 12.)), tuple(center), (0., 1., 0.)]
        self.plot.camera.parallel_projection = True
        self.plot.camera.parallel_scale = max(bounds[3] - bounds[2], bounds[1] - bounds[0], 2.) * .7
        self.plot.reset_camera_clipping_range()
        self.plot.show(auto_close=False, interactive=False)
        self.picker.set_scene(mesh, boundary, topology, actor)
        return mesh, boundary, topology, actor

    def display(self, point):
        renderer = self.plot.renderer
        renderer.SetWorldPoint(*point, 1.)
        renderer.WorldToDisplay()
        return tuple(renderer.GetDisplayPoint())[:2]

    def test_wire_only_empty_mesh_has_curve_and_point_ids_without_brep_topology(self):
        scene = wire_scene()
        self.adopt(scene)
        self.assertEqual(Hit('curve', 0), self.picker.pick(*self.display((-.5, .5, 0.))))
        self.assertEqual(Hit('point', 0), self.picker.pick(*self.display((0., -1., 0.))))
        for kind in ('face', 'edge', 'vertex'):
            self.assertIsNone(self.picker.resolve(kind, 0))
        # A source curve end is still a curve, not a manufactured source point.
        self.assertEqual(Hit('curve', 0), self.picker.pick(*self.display((-1., 0., 0.))))

    def test_native_length_and_unavailable_status_never_use_display_chord_sum(self):
        scene = wire_scene((curve(), curve(1, offset=4., length=None, status='NativeQueryFailed')))
        self.adopt(scene)
        hit = self.picker.pick(*self.display((-.5, .5, 0.)))
        info = self.picker.info(hit)
        self.assertEqual(math.pi, info.length_mm)
        self.assertEqual(1e-5, info.length_absolute_accuracy_mm)
        self.assertEqual('102', info.basis_id)
        self.assertEqual((0., math.pi), info.parameter_domain)
        self.assertTrue(info.source_forward_in_native)
        self.assertNotAlmostEqual(info.length_mm, np.linalg.norm(np.diff(info.polyline_mm, axis=0), axis=1).sum())
        unavailable = self.picker.info(self.picker.pick(*self.display((3.5, .5, 0.))))
        self.assertIsNone(unavailable.length_mm)
        self.assertEqual('NativeQueryFailed', unavailable.measurement_status)

    def test_rear_source_curve_and_point_are_occluded_by_actual_body(self):
        rear_curve = replace(curve(), polyline_mm=((-0.6, .4, -2.), (.6, .4, -2.)),
                             parameters=(0., math.pi), length_mm=1.2)
        scene = wire_scene((rear_curve,), (WirePointInfo(0, '103', 0, 0, '100', (0., 0., -2.)),))
        _, _, topology, actor = self.adopt(scene, body=True)
        self.assertEqual(Hit('face', 0), self.picker.pick(*self.display((0., 0., -2.))))
        self.assertEqual(Hit('face', 0), self.picker.pick(*self.display((0., .4, -2.))))
        # Unlabelled front geometry still blocks source features; labels never
        # change the physical actor used for the display visibility check.
        actor.GetMapper().GetInput().GetCellData().GetArray('TopoFace').FillComponent(0, -1)
        self.picker.set_scene(labelled_cube(), native_boundaries(), topology, actor)
        self.assertIsNone(self.picker.pick(*self.display((0., .4, -2.))))

    def test_body_actor_transform_is_not_applied_again_to_root_space_source_points(self):
        scene = wire_scene((), (WirePointInfo(0, '103', 0, 0, '100', (0., 0., 0.)),))
        _, _, _, actor = self.adopt(scene, body=True)
        actor.SetPosition(3., 0., 0.)
        self.plot.render()
        hit = self.picker.pick(*self.display((0., 0., 0.)))
        self.assertEqual(Hit('point', 0), hit)
        self.picker.highlight(hit)
        overlay = self.picker._highlights['hover'][0]
        np.testing.assert_array_equal(pv.wrap(overlay.GetMapper().GetInput()).points, [[0., 0., 0.]])
        np.testing.assert_array_equal(TopologyPicker._matrix(overlay.GetMatrix()), np.eye(4))

    def test_instances_keep_same_source_identity_and_distinct_root_coordinates(self):
        scene = wire_scene((curve(), curve(1, instance=2, offset=8.)), ())
        self.adopt(scene)
        first = self.picker.pick(*self.display((-.5, .5, 0.)))
        second = self.picker.pick(*self.display((7.5, .5, 0.)))
        self.assertEqual(Hit('curve', 0), first)
        self.assertEqual(Hit('curve', 1), second)
        self.assertEqual(self.picker.info(first).source_id, self.picker.info(second).source_id)
        self.assertEqual(2, self.picker.info(second).instance_index)
        self.assertEqual(math.pi, self.picker.info(second).length_mm)
        self.picker.highlight(second)
        points = pv.wrap(self.picker._highlights['hover'][0].GetMapper().GetInput()).points
        np.testing.assert_array_equal(points, scene.curves[1].polyline_mm)

    def test_stale_hits_and_highlights_never_cross_a_new_scene_token(self):
        scene = wire_scene()
        mesh, boundary, topology, actor = self.adopt(scene)
        curve_hit, point_hit = self.picker.resolve('curve', 0), self.picker.resolve('point', 0)
        camera = self.plot.camera_position
        self.picker.highlight_many((curve_hit, point_hit, curve_hit))
        self.assertEqual(2, len(self.picker._highlights['selection']))
        self.assertTrue(all(not actor.GetPickable() for actor in self.picker._highlights['selection']))
        self.assertEqual(curve_hit, self.picker.pick(*self.display((-.5, .5, 0.))))
        self.assertEqual(camera, self.plot.camera_position)
        self.picker.set_scene(mesh, boundary, topology, actor)
        for old in (curve_hit, point_hit):
            self.assertFalse(self.picker.is_current(old))
            self.assertIsNone(self.picker.info(old))
        self.picker.highlight_many((curve_hit, point_hit))
        self.assertEqual([], self.picker._highlights['selection'])
        self.assertTrue(all(self.plot.renderer.HasViewProp(actor) for actor in self.scene_actors))
        self.assertTrue(self.picker.is_current(self.picker.resolve('curve', 0)))
        self.assertIsNone(self.picker.resolve('curve', True))
        self.assertIsNone(self.picker.resolve('point', 5))


class WireframeWindowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.window = ViewerWindow()
        self.window.show()

    def tearDown(self):
        self.window.close()

    def adopt(self, scene=None, *, body=False):
        scene = wire_scene() if scene is None else scene
        mesh = labelled_cube() if body else pv.PolyData()
        boundary = native_boundaries() if body else None
        topology = replace(native_payload(), wireframe=scene) if body else wire_topology(scene)
        self.window._loaded('public-analytic-wire.step', mesh, boundary, assembly_metadata(topology))
        self.application.processEvents()
        self.assertIs(mesh, self.window.mesh)
        return topology

    def test_wire_only_render_fit_and_hud_preserve_zero_brep_counts(self):
        self.adopt()
        window = self.window
        self.assertEqual(0, window.mesh.n_cells)
        self.assertIsNone(window.picker._body_actor)
        self.assertIsNone(window.boundary_actor)
        self.assertEqual(2, len(window.wireframe_actors))
        self.assertIn('BRep 0 面 / 0 边 / 0 点', window.hud.text())
        self.assertIn('独立线框 1 条源曲线 / 1 个源点', window.hud.text())
        self.assertIn('1 个线框实例', window.hud.text())
        self.assertEqual((-1., 1., -1., 1., 0., 0.), window._scene_bounds)
        for view in ('front', 'top', 'right', 'isometric'):
            window.named_view(view)
            window.fit()
            self.assertTrue(np.isfinite(window.plot.camera.position).all())
            self.assertGreater(window.plot.camera.clipping_range[0], 0.)

    def test_source_curves_survive_brep_edges_toggle_and_mesh_wireframe_redraw(self):
        self.adopt()
        window = self.window
        window.selected = [('curve', 0)]
        window._restore_selection()
        old_hit = window.picker.resolve('curve', 0)
        window.edges_button.toggle()
        self.assertTrue(all(actor.GetVisibility() for actor in window.wireframe_actors))
        window.wire_button.toggle()
        self.assertFalse(window.picker.is_current(old_hit))
        self.assertTrue(all(actor.GetVisibility() for actor in window.wireframe_actors))
        self.assertEqual([('curve', 0)], window.selected)
        self.assertIn('源曲线 101：原生长度 ≈ 3.141593 mm', window.hud.text())
        self.assertEqual(1, len(window.picker._highlights['selection']))

    def test_single_source_point_has_finite_fit_without_a_dummy_curve_or_face(self):
        scene = wire_scene((), (WirePointInfo(0, '103', 0, 0, '100', (7., 2., -3.)),))
        self.adopt(scene)
        window = self.window
        window.fit()
        self.assertEqual((7., 7., 2., 2., -3., -3.), window._scene_bounds)
        self.assertEqual(1, len(window.wireframe_actors))
        self.assertEqual(0, window.mesh.n_points)
        self.assertEqual(0, window.mesh.n_cells)
        self.assertTrue(np.isfinite(window.plot.camera.position).all())
        self.assertGreater(window.plot.camera.clipping_range[0], 0.)

    def test_source_instance_hud_uses_native_length_and_unavailable_status(self):
        scene = wire_scene((curve(instance=2, offset=8.),), ())
        self.adopt(scene)
        window = self.window
        window.selected = [('curve', 0)]
        window._restore_selection()
        self.assertIn('选中 实例 3 · 源曲线 101：原生长度 ≈ 3.141593 mm', window.hud.text())
        self.assertNotIn('选中 边', window.hud.text())
        unavailable = wire_scene((curve(length=None, status='MeasurementBudget'),), ())
        self.adopt(unavailable)
        window.selected = [('curve', 0)]
        window._restore_selection()
        self.assertIn('原生长度 不可用 (MeasurementBudget)', window.hud.text())

    def test_two_point_distance_accepts_source_and_native_points_in_root_mm(self):
        scene = wire_scene((), (WirePointInfo(0, '103', 2, 0, '100', (2., -1., 1.)),
                               WirePointInfo(1, '104', 2, 0, '100', (2., 3., 1.))))
        self.adopt(scene, body=True)
        window = self.window
        window.selected = [('vertex', 0), ('point', 0)]
        window._restore_selection()
        self.assertIn('两点距离：3 mm', window.hud.text())
        self.assertIn('源点 103：(2 · -1 · 1) mm', window.hud.text())
        window.selected = [('point', 0), ('point', 1)]
        window._restore_selection()
        self.assertIn('两点距离：4 mm', window.hud.text())
        window._redraw()
        self.assertIn('两点距离：4 mm', window.hud.text())

    def test_source_hover_reuses_current_highlight_and_clear_removes_it(self):
        self.adopt()
        window = self.window
        from tools.smlib_gui.runtime import QtCore
        window._mouse_position = QtCore.QPointF(100, 100)
        hit = window.picker.resolve('point', 0)
        with patch.object(window.picker, 'pick', return_value=hit), \
                patch.object(window.picker, 'highlight', wraps=window.picker.highlight) as highlight:
            window._update_hover()
            window._update_hover()
            self.assertEqual(1, highlight.call_count)
            self.assertIn('悬停 实例 1 · 源点', window.hud.text())
            window.clear_selection()
            self.assertNotIn('悬停', window.hud.text())
            self.assertNotIn('hover', window.picker._highlights)


if __name__ == '__main__':
    unittest.main()
