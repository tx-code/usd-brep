# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Validate every body before atomically adopting one native display scene."""
from dataclasses import replace
import re
import numpy as np
import pyvista as pv
from .topology import (BodyInfo, TopologyData, read_topology, MAX_FACES, MAX_EDGES,
                       MAX_VERTICES, MAX_DISPLAY_POINTS, MAX_DISPLAY_CELLS, MAX_TOPOLOGY_BYTES)


def read_collection(directory, manifest, resolve):
    bodies = manifest.get('body_scenes')
    count = manifest.get('body_count')
    if (type(count) is not int or count < 2 or count > MAX_FACES or
            not isinstance(bodies, list) or len(bodies) != count or
            manifest.get('units') != 'mm' or manifest.get('role') != 'MaterialSolidCollection' or
            manifest.get('auxiliary_required') is not False or 'auxiliary' in manifest):
        raise ValueError('InvalidCollectionManifest')
    limits = dict(faces=MAX_FACES, edges=MAX_EDGES, vertices=MAX_VERTICES,
                  triangles=MAX_DISPLAY_CELLS, points=MAX_DISPLAY_POINTS)
    totals = dict.fromkeys(limits, 0)
    shape_ids = set()
    for ordinal, body in enumerate(bodies):
        if (not isinstance(body, dict) or type(body.get('ordinal')) is not int or body['ordinal'] != ordinal or
                body.get('format') != 'brep-view-scene.v3' or body.get('auxiliary_required') is not False or
                'auxiliary' in body or not isinstance(body.get('source_shape_id'), str) or
                not re.fullmatch(r'0|[1-9][0-9]*', body['source_shape_id']) or
                body['source_shape_id'] in shape_ids):
            raise ValueError('InvalidCollectionBody')
        shape_ids.add(body['source_shape_id'])
        for key, limit in limits.items():
            value = body.get(key)
            if type(value) is not int or value < 0:
                raise ValueError('InvalidCollectionInventory')
            totals[key] += value
            if totals[key] > limit:
                raise ValueError('CollectionDisplayLimit')
    if any(type(manifest.get(k)) is not int or manifest[k] != n for k, n in totals.items()):
        raise ValueError('InvalidCollectionInventory')
    faces, edges, vertices, ownership, meshes, boundaries = [], [], [], [], [], []
    payload_bytes = boundary_points = boundary_cells = 0
    used_paths = set()
    complete = True
    area_accuracy = length_accuracy = None
    for ordinal, body in enumerate(bodies):
        paths = [resolve(directory, body[k]) for k in ('mesh', 'boundary', 'topology')]
        if any(path in used_paths for path in paths) or len(set(paths)) != 3:
            raise ValueError('ReusedCollectionPayload')
        used_paths.update(paths)
        mesh, boundary = pv.read(paths[0]), pv.read(paths[1])
        data = read_topology(paths[2], mesh, boundary, body)
        payload_bytes += data.payload_bytes
        boundary_points += boundary.n_points
        boundary_cells += boundary.n_cells
        if (payload_bytes > MAX_TOPOLOGY_BYTES or boundary_points > MAX_DISPLAY_POINTS or
                boundary_cells > MAX_DISPLAY_CELLS):
            raise ValueError('CollectionDisplayLimit')
        if area_accuracy is None:
            area_accuracy, length_accuracy = data.face_area_relative_accuracy, data.edge_length_absolute_accuracy_mm
        elif (area_accuracy != data.face_area_relative_accuracy or
              length_accuracy != data.edge_length_absolute_accuracy_mm):
            raise ValueError('InconsistentCollectionMeasurements')
        f, e, v = len(faces), len(edges), len(vertices)
        ownership.append(BodyInfo(ordinal, body['source_shape_id'], f, len(data.faces),
                                  e, len(data.edges), v, len(data.vertices)))
        faces.extend(replace(info, id=f + info.id, body=ordinal, local_id=info.id) for info in data.faces)
        edges.extend(replace(info, id=e + info.id, body=ordinal, local_id=info.id) for info in data.edges)
        vertices.extend(replace(info, id=v + info.id, body=ordinal, local_id=info.id) for info in data.vertices)
        # Keep -1 unknown labels unknown. AppendPolyData preserves points/cells;
        # coincident bodies must never be welded into shared topology here.
        labels = np.asarray(mesh.cell_data['TopoFace'], dtype=np.int64)
        mesh.cell_data['TopoFace'] = np.where(labels >= 0, labels + f, -1)
        boundary.cell_data['TopoEdge'] = np.asarray(boundary.cell_data['TopoEdge'], dtype=np.int64) + e
        meshes.append(mesh)
        boundaries.append(boundary)
        complete = complete and data.face_labels_complete
    mesh = meshes[0].append_polydata(*meshes[1:])
    boundary = boundaries[0].append_polydata(*boundaries[1:])
    if (mesh.n_cells != totals['triangles'] or mesh.n_points != totals['points'] or
            boundary.n_cells != boundary_cells or boundary.n_points != boundary_points):
        raise ValueError('InvalidCollectionAppend')
    data = TopologyData(tuple(faces), tuple(edges), tuple(vertices), complete, area_accuracy, length_accuracy,
                        bodies=tuple(ownership), payload_bytes=payload_bytes)
    return mesh, boundary, data
