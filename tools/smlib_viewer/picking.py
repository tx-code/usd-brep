# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Display-copy topology nomination with native-value provenance and body occlusion.

Coordinates passed to pick() are VTK render-window DEVICE pixels, origin at the
bottom left. Qt logical coordinates/DPI conversion and mouse/drag handling belong
to the caller. This module installs no interactor observers or event filters.

The worker's native model is already released. The visible mesh/boundary nominate
display-copy IDs; all measurements come only from the owning TopologyData values.
No mesh area/length or live native-query claim is manufactured here.
"""
from __future__ import annotations

from dataclasses import dataclass, field
import math
from typing import Literal

import numpy as np
import pyvista as pv
from vtkmodules.vtkCommonDataModel import vtkStaticCellLocator
from vtkmodules.vtkRenderingCore import vtkCellPicker

from .topology import TopologyData


@dataclass(frozen=True, slots=True)
class Hit:
    """A feature ID in one owning display snapshot, not a live kernel handle."""

    kind: Literal["face", "edge", "vertex", "curve", "point"]
    id: int
    _scene_key: object | None = field(default=None, repr=False, compare=False)


class TopologyPicker:
    """Pick the body and independent wire geometry, prioritizing points/lines.

    Pixel tolerance is a display affordance in device pixels, separate from every
    native geometry/measurement tolerance. Occlusion uses the first intersection
    with this same body even if that triangle has no known native face label.
    """

    def __init__(self, plot, pixel_tolerance: float = 8.0):
        if not math.isfinite(pixel_tolerance) or pixel_tolerance <= 0:
            raise ValueError("InvalidPickPixelTolerance")
        self.plot = plot
        self.pixel_tolerance = float(pixel_tolerance)
        self._picker = vtkCellPicker()
        self._picker.PickFromListOn()
        # Small geometric-ray slack avoids losing an exact silhouette corner;
        # screen proximity is handled separately, never through the picker tol.
        self._picker.SetTolerance(1e-8)
        self._highlights = {}
        self.clear()

    def clear(self) -> None:
        """Release the old scene and its highlights, invalidating retained hits."""
        self.clear_highlight()
        self._scene_key = object()
        self._mesh = self._boundary = self._topology = self._body_actor = None
        self._wire_curves, self._wire_points = {}, {}
        self._dataset = self._locator = None
        self._picker.InitializePickList()
        self._picker.RemoveAllLocators()
        self._vertex_points = np.empty((0, 3), dtype=float)
        self._boundary_points = np.empty((0, 3), dtype=float)
        self._segments = np.empty((0, 2), dtype=np.int64)
        self._segment_ids = np.empty(0, dtype=np.int64)
        self._segment_kinds = np.empty(0, dtype=object)
        self._point_ids = np.empty(0, dtype=np.int64)
        self._point_kinds = np.empty(0, dtype=object)
        self._native_vertex_count = self._native_boundary_point_count = 0
        self._projection_key = None
        self._vertex_display = np.empty((0, 3), dtype=float)
        self._vertex_valid = np.empty(0, dtype=bool)
        self._segment_display = np.empty((0, 2, 3), dtype=float)
        self._visible_segment_ids = np.empty(0, dtype=np.int64)
        self._visible_segment_kinds = np.empty(0, dtype=object)

    def set_scene(self, mesh, boundary, topology: TopologyData | None, body_actor) -> None:
        """Adopt same-world-mm display geometry; mesh-only scenes have no CAD picks.

        Surface shading/normal splitting may change cell numbering. Face IDs are
        consequently read from the body's ACTUAL mapper dataset, not mesh's index.
        Native vertices never come from tessellation points.
        """
        self.clear()
        if topology is None:
            return
        if topology.units != "mm" or topology.identity_scope != "NativeDisplayCopy":
            raise ValueError("InvalidPickTopologyScope")
        self._mesh, self._boundary, self._topology = mesh, boundary, topology
        mapper = body_actor.GetMapper() if body_actor is not None else None
        if mapper is not None:
            mapper.Update()
            dataset = mapper.GetInput()
            if dataset is not None and dataset.GetNumberOfCells():
                self._body_actor, self._dataset = body_actor, dataset
                self._picker.AddPickList(body_actor)
                self._locator = vtkStaticCellLocator()
                self._locator.SetDataSet(dataset)
                self._locator.BuildLocator()
                self._picker.AddLocator(self._locator)
                self._vertex_points = np.asarray(
                    [vertex.point_mm for vertex in topology.vertices], dtype=float).reshape(-1, 3)
        self._native_vertex_count = len(self._vertex_points)
        self._point_ids = np.arange(self._native_vertex_count, dtype=np.int64)
        self._point_kinds = np.full(self._native_vertex_count, "vertex", dtype=object)
        segments, labels, kinds = [], [], []
        if self._dataset is not None and boundary is not None and boundary.n_cells:
            self._boundary_points = np.array(boundary.points, dtype=float, copy=True)
            for cell_index in range(boundary.n_cells):
                edge_id = self._label(boundary, "TopoEdge", cell_index, len(topology.edges))
                cell = boundary.GetCell(cell_index)
                if edge_id is None or cell.GetCellType() not in (3, 4):  # VTK_LINE / VTK_POLY_LINE
                    continue
                for index in range(cell.GetNumberOfPoints() - 1):
                    segments.append((cell.GetPointId(index), cell.GetPointId(index + 1)))
                    labels.append(edge_id)
                    kinds.append("edge")
        self._native_boundary_point_count = len(self._boundary_points)
        wireframe = topology.wireframe
        if wireframe is not None:
            self._wire_curves = {curve.id: curve for curve in wireframe.curves}
            self._wire_points = {point.id: point for point in wireframe.points}
            points = np.asarray([point.point_mm for point in wireframe.points], dtype=float).reshape(-1, 3)
            self._vertex_points = np.concatenate((self._vertex_points, points))
            self._point_ids = np.concatenate((self._point_ids, [point.id for point in wireframe.points])).astype(
                np.int64, copy=False)
            self._point_kinds = np.concatenate((self._point_kinds, np.full(len(points), "point", dtype=object)))
            # Independent source geometry is already in AssemblyRoot mm. Its
            # display samples nominate curve IDs only; they never create BRep
            # edges/vertices or supply a replacement for the native length.
            curve_points = [self._boundary_points]
            offset = len(self._boundary_points)
            for curve in wireframe.curves:
                points = np.asarray(curve.polyline_mm, dtype=float).reshape(-1, 3)
                curve_points.append(points)
                for index in range(len(points) - 1):
                    segments.append((offset + index, offset + index + 1))
                    labels.append(curve.id)
                    kinds.append("curve")
                offset += len(points)
            self._boundary_points = np.concatenate(curve_points)
        self._segments = np.asarray(segments, dtype=np.int64).reshape(-1, 2)
        self._segment_ids = np.asarray(labels, dtype=np.int64)
        self._segment_kinds = np.asarray(kinds, dtype=object)

    @staticmethod
    def _label(dataset, name: str, cell: int, count: int) -> int | None:
        if dataset is None:
            return None
        array = dataset.GetCellData().GetArray(name)
        if array is None or cell < 0 or cell >= array.GetNumberOfTuples():
            return None
        value = array.GetComponent(cell, 0)
        if not math.isfinite(value) or value != math.trunc(value):
            return None
        label = int(value)
        return label if 0 <= label < count else None

    def resolve(self, kind: str, id: int) -> Hit | None:
        """Restore a caller's same-snapshot key after redraw, using a fresh token.

        The caller must discard keys on new-model adoption. A bare dense index
        cannot establish that two independently loaded display copies are related.
        """
        if self._topology is None or type(id) is not int:
            return None
        if self._entry(kind, id) is None:
            return None
        return Hit(kind, id, self._scene_key)

    def is_current(self, hit: Hit | None) -> bool:
        """Reject stale IDs even if the next display copy reuses the same numbers."""
        if hit is None or self._topology is None or hit._scene_key is not self._scene_key:
            return False
        return type(hit.id) is int and self._entry(hit.kind, hit.id) is not None

    def _entry(self, kind, id):
        if kind in ("curve", "point"):
            return (self._wire_curves if kind == "curve" else self._wire_points).get(id)
        if self._dataset is None:
            return None
        table = {"face": self._topology.faces, "edge": self._topology.edges,
                 "vertex": self._topology.vertices}.get(kind)
        return table[id] if table is not None and 0 <= id < len(table) and table[id].id == id else None

    def info(self, hit: Hit | None):
        """Return only the precomputed native payload, including unavailable status."""
        if not self.is_current(hit):
            return None
        return self._entry(hit.kind, hit.id)

    @staticmethod
    def _matrix(value) -> np.ndarray:
        return np.array([[value.GetElement(row, column) for column in range(4)] for row in range(4)])

    def _display(self, clip: np.ndarray) -> np.ndarray:
        ndc = clip[:, :3] / clip[:, 3, None]
        x0, y0 = self._origin
        width, height = self._size
        return np.column_stack((x0 + (ndc[:, 0] + 1.) * width * .5,
                                y0 + (ndc[:, 1] + 1.) * height * .5, ndc[:, 2]))

    def _project(self) -> None:
        renderer = self.plot.renderer
        projection = self._matrix(renderer.GetActiveCamera().GetCompositeProjectionTransformMatrix(
            renderer.GetTiledAspectRatio(), 0., 1.))
        body = self._matrix(self._body_actor.GetMatrix()) if self._body_actor is not None else np.eye(4)
        self._origin = renderer.GetOrigin()
        self._size = renderer.GetSize()
        key = (tuple(projection.ravel()), tuple(body.ravel()), self._origin, self._size)
        if key == self._projection_key:
            return
        self._projection_key = key
        self._projection = projection
        vertices = self._clip_points(self._vertex_points, self._native_vertex_count, body)
        self._vertex_valid = (np.isfinite(vertices).all(axis=1) & (vertices[:, 3] > 0.)
                              & (vertices[:, 2] >= 0.) & (vertices[:, 2] <= vertices[:, 3])
                              & (np.abs(vertices[:, :2]) <= vertices[:, 3, None]).all(axis=1))
        self._vertex_display = np.full((len(vertices), 3), np.nan)
        self._vertex_display[self._vertex_valid] = self._display(vertices[self._vertex_valid])
        if not len(self._segments):
            self._segment_display = np.empty((0, 2, 3))
            self._visible_segment_ids = np.empty(0, dtype=np.int64)
            self._visible_segment_kinds = np.empty(0, dtype=object)
            return
        points = self._clip_points(self._boundary_points, self._native_boundary_point_count, body)
        starts, ends = points[self._segments[:, 0]], points[self._segments[:, 1]]
        low, high = np.zeros(len(starts)), np.ones(len(starts))
        valid = np.isfinite(starts).all(axis=1) & np.isfinite(ends).all(axis=1)
        # Clip line geometry in homogeneous camera coordinates before division.
        # This preserves visible portions crossing a near plane, without inventing
        # a whole-line domain or treating projected coordinates as measurements.
        for a, b in ((starts[:, 3] + starts[:, 0], ends[:, 3] + ends[:, 0]),
                     (starts[:, 3] - starts[:, 0], ends[:, 3] - ends[:, 0]),
                     (starts[:, 3] + starts[:, 1], ends[:, 3] + ends[:, 1]),
                     (starts[:, 3] - starts[:, 1], ends[:, 3] - ends[:, 1]),
                     (starts[:, 2], ends[:, 2]),
                     (starts[:, 3] - starts[:, 2], ends[:, 3] - ends[:, 2])):
            inside_a, inside_b = a >= 0., b >= 0.
            valid &= inside_a | inside_b
            crossing = inside_a != inside_b
            fraction = np.zeros(len(starts))
            np.divide(-a, b - a, out=fraction, where=crossing)
            low = np.where(~inside_a & inside_b, np.maximum(low, fraction), low)
            high = np.where(inside_a & ~inside_b, np.minimum(high, fraction), high)
        clipped_a = starts + (ends - starts) * low[:, None]
        clipped_b = starts + (ends - starts) * high[:, None]
        valid &= ((low <= high) & (clipped_a[:, 3] > 0.) & (clipped_b[:, 3] > 0.)
                  & np.isfinite(clipped_a).all(axis=1) & np.isfinite(clipped_b).all(axis=1))
        self._segment_display = np.stack((self._display(clipped_a[valid]), self._display(clipped_b[valid])), axis=1)
        self._visible_segment_ids = self._segment_ids[valid]
        self._visible_segment_kinds = self._segment_kinds[valid]

    def _clip_points(self, points, native_count, body):
        world = np.column_stack((points, np.ones(len(points))))
        world[:native_count] = world[:native_count] @ body.T
        return world @ self._projection.T

    def _body_visible(self):
        return (self._body_actor is not None and self._body_actor.GetVisibility()
                and self._body_actor.GetPickable() and self.plot.renderer.HasViewProp(self._body_actor))

    def _surface_at(self, x: float, y: float) -> tuple[float, int | None] | None:
        if not self._body_visible():
            return None
        if not self._picker.Pick(float(x), float(y), 0., self.plot.renderer):
            return None
        # This private pick list contains exactly the body actor. Do not compare
        # Python wrapper identities: VTK may return a different wrapper of it.
        if self._picker.GetActor() is None:
            return None
        point = np.array((*self._picker.GetPickPosition(), 1.))
        clip = self._projection @ point  # PickPosition is already in world coordinates.
        if not np.isfinite(clip).all() or clip[3] <= 0.:
            return None
        face = self._label(self._picker.GetDataSet(), "TopoFace", self._picker.GetCellId(),
                           len(self._topology.faces))
        return float(clip[2] / clip[3]), face

    def _visible(self, point: np.ndarray) -> bool:
        front = self._surface_at(point[0], point[1])
        # This numerical display-depth guard is not a native tolerance or a
        # geometric visibility proof. Unknown face labels STILL block rear IDs.
        return front is None or point[2] <= front[0] + 1e-10

    def pick(self, x: float, y: float) -> Hit | None:
        """Nominate a visible native ID; no interaction events are consumed."""
        if (self._topology is None or not math.isfinite(x) or not math.isfinite(y)
                or x < 0. or y < 0.
                or not self.plot.renderer.IsInViewport(int(x), int(y))):
            return None
        width, height = self.plot.renderer.GetRenderWindow().GetSize()
        if x >= width or y >= height:
            return None
        self._project()
        cursor = np.array((x, y))
        tolerance_squared = self.pixel_tolerance ** 2
        distances = np.sum((self._vertex_display[:, :2] - cursor) ** 2, axis=1)
        point_valid = self._vertex_valid.copy()
        if not self._body_visible():
            point_valid[:self._native_vertex_count] = False
        candidates = np.flatnonzero(point_valid & (distances <= tolerance_squared))
        order = np.lexsort((candidates, self._vertex_display[candidates, 2], distances[candidates]))
        for index in candidates[order]:
            if self._visible(self._vertex_display[index]):
                return Hit(self._point_kinds[index], int(self._point_ids[index]), self._scene_key)
        if len(self._segment_display):
            a, b = self._segment_display[:, 0], self._segment_display[:, 1]
            directions = b[:, :2] - a[:, :2]
            length_squared = np.sum(directions ** 2, axis=1)
            # A view-aligned segment can collapse to one screen point. Its nearer
            # endpoint is still visible; choosing the source-first endpoint could
            # incorrectly reject the entire native edge as being behind the body.
            fraction = np.where(b[:, 2] < a[:, 2], 1., 0.)
            np.divide(np.sum((cursor - a[:, :2]) * directions, axis=1), length_squared,
                      out=fraction, where=length_squared > 0.)
            fraction = np.clip(fraction, 0., 1.)
            # Raster depth is affine in projected segment position. No 3D curve
            # length or supposedly exact CAD parameter is derived from this value.
            closest = a + fraction[:, None] * (b - a)
            distances = np.sum((closest[:, :2] - cursor) ** 2, axis=1)
            candidates = np.flatnonzero(distances <= tolerance_squared)
            order = np.lexsort((self._visible_segment_ids[candidates], closest[candidates, 2],
                                distances[candidates]))
            for index in candidates[order]:
                if self._visible_segment_kinds[index] == "edge" and not self._body_visible():
                    continue
                if self._visible(closest[index]):
                    return Hit(self._visible_segment_kinds[index], int(self._visible_segment_ids[index]),
                               self._scene_key)
        surface = self._surface_at(x, y)
        if surface is not None and surface[1] is not None:
            return Hit("face", surface[1], self._scene_key)
        return None

    def clear_highlight(self, slot: str | None = None) -> None:
        """Remove only actors owned by this picker, without resetting camera/lights."""
        slots = tuple(self._highlights) if slot is None else (slot,)
        for name in slots:
            for actor in self._highlights.pop(name, ()):
                self.plot.remove_actor(actor, reset_camera=False, render=False)

    def highlight(self, hit: Hit | None, color="#70ccff", *, slot="hover") -> None:
        self.highlight_many(() if hit is None else (hit,), color, slot=slot)

    def highlight_many(self, hits, color="#ffbd54", *, slot="selection") -> None:
        """Depth-tested disposable overlays; native measurements stay untouched."""
        self.clear_highlight(slot)
        actors = []
        seen = set()
        for hit in hits:
            if not self.is_current(hit) or (hit.kind, hit.id) in seen:
                continue
            seen.add((hit.kind, hit.id))
            options = dict(color=color, lighting=False, reset_camera=False, render=False, pickable=False)
            if hit.kind in ("vertex", "point"):
                geometry = pv.PolyData(np.array([self.info(hit).point_mm]))
                options.update(point_size=12, render_points_as_spheres=True)
            elif hit.kind == "curve":
                geometry = pv.lines_from_points(np.asarray(self.info(hit).polyline_mm, dtype=float))
                options.update(line_width=4, render_lines_as_tubes=True)
            else:
                dataset = self._dataset if hit.kind == "face" else self._boundary
                label = "TopoFace" if hit.kind == "face" else "TopoEdge"
                if dataset is None:
                    continue
                wrapped = pv.wrap(dataset)
                if label not in wrapped.cell_data:
                    continue
                geometry = wrapped.extract_cells(np.flatnonzero(wrapped.cell_data[label] == hit.id))
                if geometry.n_cells == 0:
                    continue
                options.update(opacity=.28) if hit.kind == "face" else options.update(
                    line_width=4, render_lines_as_tubes=True)
            actor = self.plot.add_mesh(geometry, **options)
            if hit.kind == "face":
                # The overlay duplicates the body triangles exactly. Bias only
                # raster depth (not model coordinates) to avoid coplanar stripes;
                # normal depth testing still hides occluded selected surfaces.
                actor.mapper.SetResolveCoincidentTopologyToPolygonOffset()
                actor.mapper.SetRelativeCoincidentTopologyPolygonOffsetParameters(-1, -1)
            if hit.kind in ("face", "edge", "vertex"):
                actor.SetUserMatrix(self._body_actor.GetMatrix())
            actors.append(actor)
        self._highlights[slot] = actors
