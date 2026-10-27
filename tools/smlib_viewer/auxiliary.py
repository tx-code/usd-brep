# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Owning display projections of associated source roles, separate from BRep."""
from dataclasses import dataclass
import json
import re

from tools.smlib_gui.runtime import np, pv


def finite(value, shape):
    result = np.asarray(value, dtype=float)
    if result.shape != shape or not np.isfinite(result).all():
        raise ValueError("InvalidAuxiliaryCoordinates")
    return result


@dataclass
class DisplayItem:
    role: str
    kind: str
    source_id: str
    mesh: object


class AuxiliaryScene:
    def __init__(self, source):
        self.source_definition_id = source.get('source_definition_id') if isinstance(source, dict) else None
        if (self.source_definition_id is not None and
                (not isinstance(self.source_definition_id, str)
                 or not re.fullmatch(r'0|[1-9][0-9]*', self.source_definition_id))):
            raise ValueError('InvalidAuxiliaryDefinitionIdentity')
        if (not isinstance(source, dict) or source.get("format") != "brep-view-auxiliary.v1"
                or source.get("units") != "mm" or source.get("continuous_error_bound_established") is not False):
            raise ValueError("InvalidAuxiliaryFormat")
        scale = source.get("preview_scale_mm")
        if not isinstance(scale, (float, int)) or not np.isfinite(scale) or scale <= 0:
            raise ValueError("InvalidAuxiliaryPreviewScale")
        groups = source.get("groups")
        if not isinstance(groups, list) or not groups or len(groups) > 10000:
            raise ValueError("InvalidAuxiliaryGroups")
        self.source = source
        self.items = []
        self.counts = dict(wireframe_groups=0, supporting_groups=0, curves=0, points=0, frames=0, planes=0)
        self._bounds = []
        primitives = samples = 0
        for group in groups:
            role = group.get("role")
            if (role not in {"Wireframe", "ConstructiveSupportingGeometry"}
                    or group.get("material_boundary") is not False):
                raise ValueError("InvalidAuxiliaryRole")
            for key in ("representation_id", "relationship_id"):
                self.identity(group[key])
            self.counts["wireframe_groups" if role == "Wireframe" else "supporting_groups"] += 1
            if role == "Wireframe":
                self.identity(group["set_id"])
                box = finite(group["bounds_mm"], (2, 3))
                if np.any(box[0] > box[1]):
                    raise ValueError("InvalidAuxiliaryBounds")
                self._bounds.append(box)
                if group.get("frames") or group.get("planes"):
                    raise ValueError("WireframeRoleCannotContainSupportingFramesOrPlanes")
            for kind in ("curves", "points", "frames", "planes"):
                members = group.get(kind, [])
                if not isinstance(members, list):
                    raise ValueError("InvalidAuxiliaryMembers")
                primitives += len(members)
                self.counts[kind] += len(members)
                if primitives > 10000:
                    raise ValueError("AuxiliaryPrimitiveLimit")
                for member in members:
                    source_id = self.identity(member["source_id"])
                    if kind == "curves":
                        coordinates = np.asarray(member["polyline_mm"], dtype=float)
                        if coordinates.ndim != 2 or coordinates.shape[1] != 3 or len(coordinates) < 2:
                            raise ValueError("InvalidAuxiliaryPolyline")
                        coordinates = finite(coordinates, coordinates.shape)
                        samples += len(coordinates)
                        if samples > 200000:
                            raise ValueError("AuxiliarySampleLimit")
                        domain = finite(member["native_domain"], (2,))
                        parameters = finite(member["parameters"], (len(coordinates),))
                        forward = member["source_forward_in_native"]
                        if (type(forward) is not bool or member.get("parameter_space") != "Native"
                                or domain[0] >= domain[1]
                                or not np.array_equal(parameters[[0, -1]], domain if forward else domain[::-1])
                                or not np.all(np.diff(parameters) > 0 if forward else np.diff(parameters) < 0)):
                            raise ValueError("InvalidAuxiliaryParameterCoverage")
                        self.add(role, "curve", source_id, pv.lines_from_points(coordinates))
                    elif kind == "points":
                        point = finite(member["point_mm"], (3,))
                        self.add(role, "point", source_id, pv.PolyData(point.reshape(1, 3)))
                    else:
                        frame = member["frame"]
                        origin = finite(frame["origin_mm"], (3,))
                        axes = [finite(frame[axis], (3,)) for axis in ("x", "y", "z")]
                        if kind == "frames":
                            for axis, direction in zip(("x", "y", "z"), axes):
                                self.add(role, axis, source_id, pv.Line(origin, origin + scale * direction))
                        else:
                            self.identity(member["position_id"])
                            if member.get("source_bounded") is not False:
                                raise ValueError("SupportingPlaneCannotBecomeBoundedMaterialFace")
                            finite(member["native_domain"], (4,))  # Sentinels are never used as display bounds.
                            uv = finite(member["preview_uv_mm"], (4,))
                            if uv[2] <= uv[0] or uv[3] <= uv[1]:
                                raise ValueError("InvalidPlanePreviewWindow")
                            corners = finite(member["preview_corners_mm"], (4, 3))
                            self.add(role, "plane", source_id, pv.PolyData(corners, [4, 0, 1, 2, 3]))
                            for fraction in np.linspace(0, 1, 7):
                                a = corners[0] + fraction * (corners[1] - corners[0])
                                b = corners[3] + fraction * (corners[2] - corners[3])
                                c = corners[0] + fraction * (corners[3] - corners[0])
                                d = corners[1] + fraction * (corners[2] - corners[1])
                                self.add(role, "plane_grid", source_id, pv.Line(a, b))
                                self.add(role, "plane_grid", source_id, pv.Line(c, d))
        if not primitives:
            raise ValueError("EmptyAuxiliaryScene")

    @staticmethod
    def identity(value):
        if not isinstance(value, str) or not value:
            raise ValueError("InvalidAuxiliaryIdentity")
        return value

    def add(self, role, kind, source_id, mesh):
        if not np.isfinite(mesh.points).all():
            raise ValueError("AuxiliaryDisplayRange")
        # Provenance labels are local display-item ordinals; they never create
        # STEP vertices/edges/faces or merge equal-position source definitions.
        mesh.cell_data["SourceItem"] = np.full(mesh.n_cells, len(self.items), dtype=np.int32)
        self.items.append(DisplayItem(role, kind, source_id, mesh))
        self._bounds.append(np.array([mesh.bounds[::2], mesh.bounds[1::2]]))

    def combined_bounds(self, body_bounds):
        boxes = [np.array([body_bounds[::2], body_bounds[1::2]]), *self._bounds]
        low = np.min([box[0] for box in boxes], axis=0)
        high = np.max([box[1] for box in boxes], axis=0)
        return tuple(np.column_stack((low, high)).ravel())

    def render(self, plot):
        batches = {}
        for item in self.items:
            key = item.role, item.kind
            batches.setdefault(key, []).append(item.mesh)
        actors = []
        for (role, kind), meshes in batches.items():
            # Batch display calls while retaining coincident source members and
            # per-cell item identities. BRep surfaces remain in another actor.
            mesh = pv.merge(meshes, merge_points=False) if len(meshes) > 1 else meshes[0]
            color = {"x": "#e48178", "y": "#87cfa0", "z": "#8ab8ec"}.get(
                kind, "#78d6e6" if role == "Wireframe" else "#dab47b")
            actors.append(plot.add_mesh(
                mesh, color=color, lighting=False, line_width=2, point_size=8,
                render_points_as_spheres=kind == "point", opacity=.07 if kind == "plane" else 1,
                render=False, reset_camera=False))
        return actors


def read_auxiliary(path):
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("AuxiliaryReportLimit")
    return AuxiliaryScene(json.loads(path.read_text(encoding="utf-8")))
