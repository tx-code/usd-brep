# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Validate every body before atomically adopting one native display scene."""
from dataclasses import dataclass, replace
import re
import numpy as np
import pyvista as pv
from .topology import (BodyInfo, TopologyData, read_topology, MAX_FACES, MAX_EDGES,
                       MAX_VERTICES, MAX_DISPLAY_POINTS, MAX_DISPLAY_CELLS, MAX_TOPOLOGY_BYTES)
from .definition_selection import validate_manifest as validate_selection


def read_collection(directory, manifest, resolve, *, _allow_single=False):
    bodies = manifest.get('body_scenes')
    count = manifest.get('body_count')
    if (type(count) is not int or count < (1 if _allow_single else 2) or count > MAX_FACES or
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


@dataclass(frozen=True, slots=True)
class DefinitionScene:
    """Owning display projection in one source definition's local frame."""

    mesh: pv.PolyData
    boundary: pv.PolyData
    metadata: dict


def read_document(directory, manifest, resolve):
    """Validate ALL definitions before returning any displayable projection.

    Reuse body transport validation per definition, but enforce inventory,
    payload identity and capacity across the complete request. Definition-local
    coordinates are never appended into a fictitious assembly/world space.
    """
    count, bodies = manifest.get('definition_count'), manifest.get('body_scenes')
    validate_selection(manifest)
    minimum = 1 if manifest.get('definition_selection') else 2
    if (manifest.get('coordinate_scope') != 'DefinitionLocal' or
            manifest.get('role') != 'IndependentProductDefinitions' or manifest.get('units') != 'mm' or
            manifest.get('auxiliary_required') is not False or 'auxiliary' in manifest or
            type(count) is not int or count < minimum or count > MAX_FACES or not isinstance(bodies, list) or
            type(manifest.get('body_count')) is not int or not count <= len(bodies) <= MAX_FACES or
            manifest['body_count'] != len(bodies)):
        raise ValueError('InvalidDocumentManifest')
    groups = [[] for _ in range(count)]
    definitions, used_paths = {}, set()
    limits = dict(faces=MAX_FACES, edges=MAX_EDGES, vertices=MAX_VERTICES,
                  triangles=MAX_DISPLAY_CELLS, points=MAX_DISPLAY_POINTS)
    totals = dict.fromkeys(limits, 0)
    previous = 0
    for ordinal, body in enumerate(bodies):
        if not isinstance(body, dict):
            raise ValueError('InvalidDocumentBody')
        index, source = body.get('definition'), body.get('source_definition_id')
        if (type(body.get('ordinal')) is not int or body['ordinal'] != ordinal or type(index) is not int or
                not 0 <= index < count or index not in (previous, previous + 1) or
                (ordinal == 0 and index != 0) or body.get('role') != 'MaterialSolid' or
                not isinstance(source, str) or not re.fullmatch(r'0|[1-9][0-9]*', source)):
            raise ValueError('InvalidDocumentBody')
        if index in definitions and definitions[index] != source:
            raise ValueError('InconsistentDocumentDefinition')
        definitions[index] = source
        previous = index
        for key, limit in limits.items():
            value = body.get(key)
            if type(value) is not int or value < 0:
                raise ValueError('InvalidDocumentInventory')
            totals[key] += value
            if totals[key] > limit:
                raise ValueError('DocumentDisplayLimit')
        paths = [resolve(directory, body[k]) for k in ('mesh', 'boundary', 'topology')]
        if len(set(paths)) != 3 or any(path in used_paths for path in paths):
            raise ValueError('ReusedDocumentPayload')
        used_paths.update(paths)
        groups[index].append(dict(body, ordinal=len(groups[index])))
    if (len(definitions) != count or len(set(definitions.values())) != count or
            any(type(manifest.get(k)) is not int or manifest[k] != n for k, n in totals.items())):
        raise ValueError('InvalidDocumentInventory')
    scenes = []
    topology_bytes = boundary_points = boundary_cells = 0
    for index, group in enumerate(groups):
        projection = dict(manifest, role='MaterialSolidCollection', body_count=len(group), body_scenes=group)
        for key in limits:
            projection[key] = sum(body[key] for body in group)
        mesh, boundary, topology = read_collection(directory, projection, resolve, _allow_single=True)
        topology_bytes += topology.payload_bytes
        boundary_points += boundary.n_points
        boundary_cells += boundary.n_cells
        if (topology_bytes > MAX_TOPOLOGY_BYTES or boundary_points > MAX_DISPLAY_POINTS or
                boundary_cells > MAX_DISPLAY_CELLS):
            raise ValueError('DocumentDisplayLimit')
        projection.update(role='MaterialSolid' if len(group) == 1 else 'MaterialSolidCollection',
                          definition_index=index, source_definition_id=definitions[index],
                          document_body_count=len(bodies), _topology=topology)
        scenes.append(DefinitionScene(mesh, boundary, projection))
    return tuple(scenes)
