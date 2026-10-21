# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Owning native display-copy identities and measurements, separate from tessellation."""
from __future__ import annotations

from dataclasses import dataclass
import json
import math
from pathlib import Path
from typing import Mapping

import numpy as np
import pyvista as pv

MAX_TOPOLOGY_BYTES = 32 * 1024 * 1024
MAX_FACES = 100000
MAX_EDGES = 200000
MAX_VERTICES = 200000
MAX_DISPLAY_CELLS = 2000000
MAX_DISPLAY_POINTS = 6000000
MEASUREMENT_STATUSES = frozenset({"Available", "NativeQueryFailed", "MeasurementBudget"})


@dataclass(frozen=True, slots=True)
class FaceInfo:
    id: int
    area_mm2: float | None
    measurement_status: str


@dataclass(frozen=True, slots=True)
class EdgeInfo:
    id: int
    length_mm: float | None
    measurement_status: str


@dataclass(frozen=True, slots=True)
class VertexInfo:
    id: int
    point_mm: tuple[float, float, float]


@dataclass(frozen=True, slots=True)
class TopologyData:
    """Dense indices identify this display copy only, never source STEP IDs.

    The worker has already released its native objects. Coordinates and optional
    native-query results are owned values; no kernel handles or mesh-derived
    replacement measurements escape the request directory.
    """

    faces: tuple[FaceInfo, ...]
    edges: tuple[EdgeInfo, ...]
    vertices: tuple[VertexInfo, ...]
    face_labels_complete: bool
    face_area_relative_accuracy: float
    edge_length_absolute_accuracy_mm: float
    identity_scope: str = "NativeDisplayCopy"
    units: str = "mm"
    continuous_error_bound_established: bool = False


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("DuplicateTopologyField")
        result[key] = value
    return result


def _invalid_constant(value):
    raise ValueError("InvalidTopologyNumber")


def _number(value, *, positive=False):
    if type(value) not in (int, float):
        raise ValueError("InvalidTopologyNumber")
    try:
        number = float(value)
    except (OverflowError, ValueError) as error:
        raise ValueError("InvalidTopologyNumber") from error
    if not math.isfinite(number) or (positive and number <= 0):
        raise ValueError("InvalidTopologyNumber")
    return number


def _table(source, name, limit, manifest):
    entries = source.get(name)
    count = manifest.get(name)
    if (not isinstance(entries, list) or len(entries) > limit
            or type(count) is not int or count < 0 or count != len(entries)):
        raise ValueError("InvalidTopologyInventory")
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict) or type(entry.get("id")) is not int or entry["id"] != index:
            raise ValueError("InvalidTopologyIdentity")
    return entries


def _measurement(entry, name):
    status = entry.get("measurement_status")
    if not isinstance(status, str) or status not in MEASUREMENT_STATUSES or name not in entry:
        raise ValueError("InvalidTopologyMeasurement")
    value = entry[name]
    # A failed or budget-limited native query remains unavailable. In particular,
    # zero and triangle area/length are not substitutes for missing evidence.
    if status != "Available":
        if value is not None:
            raise ValueError("InvalidTopologyMeasurement")
        return None, status
    number = _number(value)
    if number < 0:
        raise ValueError("InvalidTopologyMeasurement")
    return number, status


def _labels(dataset, name, count, *, unknown=False):
    if name not in dataset.cell_data:
        raise ValueError("MissingTopologyLabels")
    labels = np.asarray(dataset.cell_data[name])
    if labels.shape != (dataset.n_cells,) or labels.dtype.kind not in "iu":
        raise ValueError("InvalidTopologyLabels")
    if labels.size and (np.any(labels < (-1 if unknown else 0)) or np.any(labels >= count)):
        raise ValueError("InvalidTopologyLabelRange")
    return labels


def _display(mesh, boundary, manifest, face_count, edge_count, complete):
    if (not isinstance(mesh, pv.PolyData) or not mesh.n_cells or not mesh.is_all_triangles
            or mesh.n_cells > MAX_DISPLAY_CELLS or mesh.n_points > MAX_DISPLAY_POINTS
            or not np.isfinite(mesh.points).all()):
        raise ValueError("InvalidTopologyDisplayMesh")
    for key, actual in (("triangles", mesh.n_cells), ("points", mesh.n_points)):
        if type(manifest.get(key)) is not int or manifest[key] != actual:
            raise ValueError("InvalidTopologyDisplayInventory")
    labels = _labels(mesh, "TopoFace", face_count, unknown=True)
    if complete != (not np.any(labels == -1)):
        raise ValueError("InvalidTopologyFaceCompleteness")
    # Boundary polylines retain native edge labels even when several lines use
    # one edge. Degenerate native edges need not emit a display polyline at all.
    if (not isinstance(boundary, pv.PolyData) or boundary.n_cells != boundary.n_lines
            or boundary.n_cells > MAX_DISPLAY_CELLS or boundary.n_points > MAX_DISPLAY_POINTS
            or not np.isfinite(boundary.points).all()):
        raise ValueError("InvalidTopologyDisplayBoundary")
    _labels(boundary, "TopoEdge", edge_count)


def read_topology(path: Path, mesh, boundary, manifest: Mapping) -> TopologyData:
    """Validate the complete v3 transport before the loader can publish a scene."""
    # Read at most one byte beyond the bound as well as checking stat: a changing
    # or truncated worker payload must not bypass the in-memory resource limit.
    if path.stat().st_size > MAX_TOPOLOGY_BYTES:
        raise ValueError("TopologyReportLimit")
    with path.open("rb") as stream:
        payload = stream.read(MAX_TOPOLOGY_BYTES + 1)
    if len(payload) > MAX_TOPOLOGY_BYTES:
        raise ValueError("TopologyReportLimit")
    source = json.loads(payload.decode("utf-8"), object_pairs_hook=_object, parse_constant=_invalid_constant)
    if (not isinstance(source, dict) or source.get("format") != "brep-view-topology.v1"
            or source.get("units") != "mm" or manifest.get("units") != "mm"
            or source.get("identity_scope") != "NativeDisplayCopy"
            or source.get("continuous_error_bound_established") is not False
            or type(source.get("face_labels_complete")) is not bool):
        raise ValueError("InvalidTopologyFormat")
    area_accuracy = _number(source.get("face_area_relative_accuracy"), positive=True)
    length_accuracy = _number(source.get("edge_length_absolute_accuracy_mm"), positive=True)
    faces = tuple(FaceInfo(entry["id"], *_measurement(entry, "area_mm2"))
                  for entry in _table(source, "faces", MAX_FACES, manifest))
    edges = tuple(EdgeInfo(entry["id"], *_measurement(entry, "length_mm"))
                  for entry in _table(source, "edges", MAX_EDGES, manifest))
    vertices = []
    for entry in _table(source, "vertices", MAX_VERTICES, manifest):
        point = entry.get("point_mm")
        if not isinstance(point, list) or len(point) != 3:
            raise ValueError("InvalidTopologyVertex")
        vertices.append(VertexInfo(entry["id"], tuple(_number(value) for value in point)))
    _display(mesh, boundary, manifest, len(faces), len(edges), source["face_labels_complete"])
    return TopologyData(faces, edges, tuple(vertices), source["face_labels_complete"], area_accuracy, length_accuracy)
