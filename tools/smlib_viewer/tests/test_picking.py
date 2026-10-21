# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Real offscreen VTK picking over a public labelled cube, with no CAD input."""
import os
import unittest
from dataclasses import replace

os.environ.setdefault("PYVISTA_OFF_SCREEN", "true")

import numpy as np
import pyvista as pv

from tools.smlib_viewer.picking import Hit, TopologyPicker
from tools.smlib_viewer.topology import EdgeInfo, FaceInfo, TopologyData, VertexInfo


POINTS = np.array([
    [-1., -1., 1.], [1., -1., 1.], [1., 1., 1.], [-1., 1., 1.],
    [-1., -1., -1.], [1., -1., -1.], [1., 1., -1.], [-1., 1., -1.],
])


def labelled_cube():
    quads = np.array([
        [4, 0, 1, 2, 3], [4, 4, 7, 6, 5], [4, 0, 4, 5, 1],
        [4, 3, 2, 6, 7], [4, 0, 3, 7, 4], [4, 1, 5, 6, 2],
    ])
    mesh = pv.PolyData(POINTS.copy(), quads.ravel())
    mesh.cell_data["TopoFace"] = np.arange(6, dtype=np.int64)
    return mesh.triangulate()


def native_payload():
    # A synthetic native point/edge imprint on the rear face has no front-face
    # counterpart. It tests real depth rejection, not just front-first tie sorting.
    vertices = tuple(VertexInfo(index, tuple(point)) for index, point in enumerate(POINTS))
    return TopologyData(
        faces=tuple(FaceInfo(index, 4., "Available") for index in range(6)),
        edges=(EdgeInfo(0, None, "NativeQueryFailed"), EdgeInfo(1, .8, "Available")),
        vertices=vertices + (VertexInfo(8, (0., 0., -1.)),),
        face_labels_complete=True,
        face_area_relative_accuracy=1e-6,
        edge_length_absolute_accuracy_mm=1e-6,
    )


def native_boundaries():
    # Front upper cube edge and an independent rear-plane imprint. These are
    # display polylines carrying IDs; their lengths are never measurement inputs.
    points = np.array([[-1., 1., 1.], [1., 1., 1.], [-.4, 0., -1.], [.4, 0., -1.]])
    lines = pv.PolyData(points, lines=np.array([2, 0, 1, 2, 2, 3]))
    lines.cell_data["TopoEdge"] = np.array([0, 1], dtype=np.int64)
    return lines


class PickingTests(unittest.TestCase):
    def setUp(self):
        self.plot = pv.Plotter(off_screen=True, window_size=(400, 400))
        self.mesh = labelled_cube()
        self.boundary = native_boundaries()
        self.topology = native_payload()
        # Deliberately reverse actual render-cell numbering: using the original
        # mesh's index with CellPicker.GetCellId would identify the wrong face.
        triangles = self.mesh.faces.reshape(-1, 4)
        display = pv.PolyData(self.mesh.points.copy(), triangles[::-1].ravel())
        display.cell_data["TopoFace"] = self.mesh.cell_data["TopoFace"][::-1].copy()
        self.body = self.plot.add_mesh(display, smooth_shading=True, split_sharp_edges=True)
        self.plot.camera_position = [(0., 0., 6.), (0., 0., 0.), (0., 1., 0.)]
        self.plot.camera.parallel_projection = True
        self.plot.camera.parallel_scale = 1.5
        self.plot.reset_camera_clipping_range()
        self.plot.show(auto_close=False, interactive=False)
        self.picker = TopologyPicker(self.plot)
        self.picker.set_scene(self.mesh, self.boundary, self.topology, self.body)

    def tearDown(self):
        self.picker.clear()
        self.plot.close()

    def display(self, point):
        renderer = self.plot.renderer
        renderer.SetWorldPoint(*point, 1.)
        renderer.WorldToDisplay()
        return tuple(renderer.GetDisplayPoint())

    def test_actual_mapper_face_labels_and_rear_feature_occlusion(self):
        x, y, _ = self.display((0., 0., 1.))
        # Rear native vertex 8 and rear edge 1 project to this same pixel. Both
        # must be blocked by the front body, leaving its ACTUAL mapped face 0.
        hit = self.picker.pick(x, y)
        self.assertEqual(Hit("face", 0), hit)
        self.assertEqual(self.topology.faces[0], self.picker.info(hit))
        self.assertTrue(self.picker.is_current(hit))

    def test_front_vertex_then_front_boundary_have_priority_without_line_actor(self):
        hit = self.picker.pick(*self.display(POINTS[0])[:2])
        self.assertEqual(Hit("vertex", 0), hit)
        self.assertEqual(tuple(POINTS[0]), self.picker.info(hit).point_mm)
        hit = self.picker.pick(*self.display((0., 1., 1.))[:2])
        self.assertEqual(Hit("edge", 0), hit)
        # A failed native length remains unavailable despite a nonzero polyline.
        self.assertIsNone(self.picker.info(hit).length_mm)
        self.assertEqual("NativeQueryFailed", self.picker.info(hit).measurement_status)

    def test_unknown_front_face_still_occludes_without_inventing_a_face_id(self):
        self.body.GetMapper().GetInput().GetCellData().GetArray("TopoFace").FillComponent(0, -1)
        self.picker.set_scene(self.mesh, self.boundary,
                              replace(self.topology, face_labels_complete=False), self.body)
        self.assertIsNone(self.picker.pick(*self.display((0., 0., 1.))[:2]))

    def test_unavailable_native_area_is_not_replaced_by_mesh_area(self):
        faces = (FaceInfo(0, None, "MeasurementBudget"),) + self.topology.faces[1:]
        self.picker.set_scene(self.mesh, self.boundary, replace(self.topology, faces=faces), self.body)
        hit = self.picker.pick(*self.display((0., 0., 1.))[:2])
        self.assertEqual(Hit("face", 0), hit)
        self.assertIsNone(self.picker.info(hit).area_mm2)
        self.assertEqual("MeasurementBudget", self.picker.info(hit).measurement_status)

    def test_scene_reset_invalidates_old_hits_and_resolve_is_current_snapshot_only(self):
        hit = self.picker.pick(*self.display(POINTS[0])[:2])
        self.picker.highlight(hit)
        self.picker.clear()
        self.assertFalse(self.picker.is_current(hit))
        self.assertIsNone(self.picker.info(hit))
        self.assertIsNone(self.picker.resolve(hit.kind, hit.id))
        self.assertIsNone(self.picker.pick(*self.display(POINTS[0])[:2]))
        # A same-snapshot redraw obtains a new token; old Hit objects never revive.
        self.picker.set_scene(self.mesh, self.boundary, self.topology, self.body)
        restored = self.picker.resolve(hit.kind, hit.id)
        self.assertEqual(Hit("vertex", 0), restored)
        self.assertTrue(self.picker.is_current(restored))
        self.assertFalse(self.picker.is_current(hit))
        self.assertIsNone(self.picker.resolve("triangle", 0))
        self.assertIsNone(self.picker.resolve("vertex", 99))
        self.assertIsNone(self.picker.resolve("vertex", True))

    def test_mesh_only_scene_has_no_cad_features_even_with_cell_labels(self):
        # Both legacy mesh.v1/v2 transport paths supply no _topology snapshot.
        self.picker.set_scene(self.mesh, self.boundary, None, self.body)
        self.assertIsNone(self.picker.pick(*self.display(POINTS[0])[:2]))
        self.assertIsNone(self.picker.pick(*self.display((0., 0., 1.))[:2]))
        self.assertIsNone(self.picker.resolve("face", 0))

    def test_perspective_non_square_window_uses_actual_vtk_device_projection(self):
        self.plot.window_size = (640, 360)
        self.plot.camera.parallel_projection = False
        self.plot.render()
        self.picker._project()
        np.testing.assert_allclose(self.picker._vertex_display[0], self.display(POINTS[0]), atol=1e-7, rtol=0)
        self.assertEqual(Hit("vertex", 0), self.picker.pick(*self.display(POINTS[0])[:2]))
        self.assertIsNone(self.picker.pick(-1., 0.))
        self.assertIsNone(self.picker.pick(640., 0.))
        self.assertIsNone(self.picker.pick(float("nan"), 100.))

    def test_highlights_do_not_intercept_picking_or_change_body_and_camera(self):
        center = self.display((0., 0., 1.))[:2]
        face = self.picker.pick(*center)
        vertex = self.picker.pick(*self.display(POINTS[0])[:2])
        camera = self.plot.camera_position
        points = self.mesh.points.copy()
        style = self.plot.iren.interactor.GetInteractorStyle()
        self.picker.highlight(face, "#00aaff")
        self.picker.highlight_many([vertex], "#ffcc00")
        self.plot.render()
        self.assertEqual(face, self.picker.pick(*center))
        self.assertEqual(camera, self.plot.camera_position)
        self.assertIs(style, self.plot.iren.interactor.GetInteractorStyle())
        np.testing.assert_array_equal(points, self.mesh.points)
        self.assertTrue(all(not actor.GetPickable()
                            for actors in self.picker._highlights.values() for actor in actors))
        self.picker.clear_highlight()
        self.assertTrue(self.plot.renderer.HasViewProp(self.body))


if __name__ == "__main__":
    unittest.main()
