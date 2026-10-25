# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Complete, owning assembly-root display of independent wireframe definitions."""
from __future__ import annotations

from dataclasses import dataclass, field
import json
import math
from pathlib import Path
import re

import numpy as np
import pyvista as pv

from .topology import _invalid_constant, _number, _object

MAX_WIRE_BYTES = 16 * 1024 * 1024
MAX_WIRE_GROUPS = 10000
MAX_WIRE_MEMBERS = 10000
MAX_WIRE_SAMPLES = 200000
CURVE_CHORD_OBSERVATION_MM = 0.01
LENGTH_ABSOLUTE_ACCURACY_MM = 1e-5


@dataclass(frozen=True, slots=True)
class WireCurveInfo:
    id: int
    source_id: str
    basis_id: str
    instance_index: int
    definition_index: int
    set_id: str
    parameter_domain: tuple[float, float]
    source_forward_in_native: bool
    parameters: tuple[float, ...]
    polyline_mm: tuple[tuple[float, float, float], ...]
    length_mm: float | None
    measurement_status: str
    length_absolute_accuracy_mm: float


@dataclass(frozen=True, slots=True)
class WirePointInfo:
    id: int
    source_id: str
    instance_index: int
    definition_index: int
    set_id: str
    point_mm: tuple[float, float, float]


@dataclass(frozen=True, slots=True)
class WireframeScene:
    """No BRep topology, inferred connections, native handles or borrowed paths.

    IDs are dense display membership ordinals per kind. Coincident instances or
    the same source member in two source sets deliberately remain separate.
    Polyline sampling is display evidence only; lengths are native measurements.
    """

    curves: tuple[WireCurveInfo, ...]
    points: tuple[WirePointInfo, ...]
    group_count: int
    instance_count: int
    source_sha256: str
    payload_bytes: int = field(default=0, repr=False, compare=False)
    units: str = 'mm'
    coordinate_scope: str = 'AssemblyRoot'
    curve_chord_observation_mm: float = CURVE_CHORD_OBSERVATION_MM
    continuous_error_bound_established: bool = False

    def render(self, plot):
        """Batch disposable actors without welding independent memberships."""
        actors = []
        if self.curves:
            points, lines = [], []
            for curve in self.curves:
                start = len(points)
                points.extend(curve.polyline_mm)
                lines.extend((len(curve.polyline_mm), *range(start, len(points))))
            mesh = pv.PolyData(np.asarray(points, dtype=float), lines=np.asarray(lines, dtype=np.int64))
            mesh.cell_data['WireCurve'] = np.arange(len(self.curves), dtype=np.int32)
            actors.append(plot.add_mesh(mesh, color='#78d6e6', line_width=2, lighting=False,
                                        render=False, reset_camera=False))
        if self.points:
            mesh = pv.PolyData(np.asarray([point.point_mm for point in self.points], dtype=float))
            mesh.cell_data['WirePoint'] = np.arange(len(self.points), dtype=np.int32)
            actors.append(plot.add_mesh(mesh, color='#78d6e6', point_size=8, render_points_as_spheres=True,
                                        lighting=False, render=False, reset_camera=False))
        return actors

    def combined_bounds(self, body_bounds_or_none):
        """Bounds describe visible samples, never a continuous curve certificate."""
        boxes = []
        if body_bounds_or_none is not None:
            bounds = tuple(_number(value) for value in body_bounds_or_none)
            if len(bounds) != 6 or any(bounds[i] > bounds[i + 1] for i in (0, 2, 4)):
                raise ValueError('InvalidWireframeBodyBounds')
            boxes.append(np.asarray([bounds[::2], bounds[1::2]]))
        for curve in self.curves:
            points = np.asarray(curve.polyline_mm)
            boxes.append(np.asarray([points.min(axis=0), points.max(axis=0)]))
        if self.points:
            points = np.asarray([point.point_mm for point in self.points])
            boxes.append(np.asarray([points.min(axis=0), points.max(axis=0)]))
        if not boxes:
            raise ValueError('EmptyWireframeScene')
        low = np.min([box[0] for box in boxes], axis=0)
        high = np.max([box[1] for box in boxes], axis=0)
        return tuple(float(value) for value in np.column_stack((low, high)).ravel())


def _identity(value):
    if not isinstance(value, str) or not re.fullmatch(r'0|[1-9][0-9]*', value):
        raise ValueError('InvalidWireframeSourceIdentity')
    return value


def _point(value):
    if not isinstance(value, list) or len(value) != 3:
        raise ValueError('InvalidWireframePoint')
    return tuple(_number(number) for number in value)


def _domain(value):
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError('InvalidWireframeParameterDomain')
    lo, hi = (_number(number) for number in value)
    if not lo < hi:
        raise ValueError('InvalidWireframeParameterDomain')
    return lo, hi


def _root_point_matches(point, local, origin):
    """Enclose display-transform arithmetic, without a new geometry tolerance.

    The child report owns the native definition-local point. Enclose each
    binary64 multiply/add outwards by one ULP in the C++ ToRoot row order; the
    interval also covers a fused multiply/add's single rounding. This checks
    transport placement only, not source geometry accuracy or curve coverage.
    """
    local = _point(local)
    rotation = origin.get('rotation_to_root')
    translation = _point(origin.get('translation_to_root_mm'))
    if not isinstance(rotation, list) or len(rotation) != 3:
        raise ValueError('InvalidWireframeTransform')
    for axis, row in enumerate(rotation):
        if not isinstance(row, list) or len(row) != 3:
            raise ValueError('InvalidWireframeTransform')
        lo = hi = translation[axis]
        for coefficient, value in zip(row, local):
            product = _number(coefficient) * value
            product_lo = math.nextafter(product, -math.inf)
            product_hi = math.nextafter(product, math.inf)
            lo = math.nextafter(lo + product_lo, -math.inf)
            hi = math.nextafter(hi + product_hi, math.inf)
        if not lo <= point[axis] <= hi:
            raise ValueError('WireframePointTransformMismatch')


def _definition_groups(definition, check, source_sha256, native_status):
    """Bind memberships to complete native child checks, not display inventory."""
    sets = definition.get('source_wireframe_sets')
    child = check.get('checks')
    if (not isinstance(sets, list) or not sets or len(sets) > MAX_WIRE_GROUPS
            or any(_identity(identity) != identity for identity in sets) or len(set(sets)) != len(sets)
            or not isinstance(child, dict)
            or child.get('format_version') != 'step-assembly-definition-checks.v1'
            or child.get('scope') != 'WithinAssembly' or child.get('status') != native_status
            or child.get('source_sha256') != source_sha256 or child.get('candidate_discarded') is not False
            or child.get('source_evidence') != 'AssemblyWireframeDefinition'
            or child.get('execution_evidence') != 'AssemblyWireframeDefinition'):
        raise ValueError('MissingWireframeDefinitionChecks')
    model, validations = child.get('model'), child.get('validations')
    if (not isinstance(model, dict) or model.get('kind') != 'OwningReadOnlyWireframeSnapshot'
            or model.get('role') != 'WireframeModel' or type(model.get('brep_body_count')) is not int
            or model['brep_body_count'] != 0 or not isinstance(validations, dict)
            or validations.get('scope') != 'DetachedCurves'
            or validations.get('face_integration') != 'NotApplicable'
            or validations.get('material_classification') != 'NotApplicable'
            or type(validations.get('level2_reports')) is not int or validations['level2_reports'] != 0
            or 'pointer_reports' not in validations or validations['pointer_reports'] is not None):
        raise ValueError('InvalidWireframeDefinitionRole')
    groups = child.get('wireframes')
    if not isinstance(groups, list) or len(groups) != len(sets):
        raise ValueError('IncompleteWireframeDefinition')
    for index, group in enumerate(groups):
        if (not isinstance(group, dict) or group.get('source_set') != sets[index]
                or group.get('role') != 'DefinitionWireframe' or group.get('material_boundary') is not False
                or group.get('source_relationship') is not None or 'source_relationship' not in group):
            raise ValueError('InvalidWireframeDefinitionGroup')
        _identity(group.get('source_representation'))
        members = []
        for kind in ('curves', 'points'):
            entries = group.get(kind)
            if not isinstance(entries, list) or len(entries) > MAX_WIRE_MEMBERS:
                raise ValueError('InvalidWireframeDefinitionMembers')
            for entry in entries:
                if not isinstance(entry, dict):
                    raise ValueError('InvalidWireframeDefinitionMembers')
                members.append(_identity(entry.get('source_id')))
        if not members or len(set(members)) != len(members):
            raise ValueError('InvalidWireframeDefinitionMembers')
    return groups


def read_wireframes(path: Path, manifest, core, checks) -> WireframeScene:
    """Validate every source set for every instance before returning any scene."""
    if path.stat().st_size > MAX_WIRE_BYTES:
        raise ValueError('WireframeReportLimit')
    with path.open('rb') as stream:
        payload = stream.read(MAX_WIRE_BYTES + 1)
    if len(payload) > MAX_WIRE_BYTES:
        raise ValueError('WireframeReportLimit')
    source = json.loads(payload.decode('utf-8'), object_pairs_hook=_object, parse_constant=_invalid_constant)
    if (not isinstance(source, dict) or source.get('format') != 'brep-view-wires.v1'
            or source.get('units') != 'mm' or source.get('coordinate_scope') != 'AssemblyRoot'
            or source.get('source_sha256') != manifest['source_sha256']
            or source.get('continuous_error_bound_established') is not False
            or _number(source.get('curve_chord_observation_mm')) != CURVE_CHORD_OBSERVATION_MM):
        raise ValueError('InvalidWireframeFormat')
    expected = []
    wire_instances = 0
    definition_groups = {}
    expected_members = 0
    # The assembly validator has already established dense instance/definition
    # indices and their source ownership. Keep the original traversal and set
    # order: repeated definitions are not reusable display payloads in root space.
    for node in manifest['instances']:
        definition_index = node['definition']
        definition = core['definitions'][definition_index]
        if definition['role'] == 'WireframeModel':
            wire_instances += 1
            if definition_index not in definition_groups:
                definition_groups[definition_index] = _definition_groups(
                    definition, checks[definition_index], manifest['source_sha256'], manifest['native_import_status'])
            groups = definition_groups[definition_index]
            if len(expected) + len(groups) > MAX_WIRE_GROUPS:
                raise ValueError('WireframeGroupLimit')
            expected_members += sum(len(group['curves']) + len(group['points']) for group in groups)
            if expected_members > MAX_WIRE_MEMBERS:
                raise ValueError('WireframeMemberLimit')
            expected.extend((node['index'], definition_index, definition, group) for group in groups)
    groups = source.get('groups')
    count = manifest.get('wireframe_instance_count')
    if (type(count) is not int or not 0 < count <= 10000 or count != wire_instances
            or not isinstance(groups, list) or not expected or len(groups) != len(expected)):
        raise ValueError('IncompleteWireframeInstances')
    curves, points = [], []
    samples = 0
    for group, (instance, definition_index, definition, original) in zip(groups, expected):
        if (not isinstance(group, dict) or group.get('role') != 'DefinitionWireframe'
                or group.get('material_boundary') is not False
                or type(group.get('instance')) is not int or group['instance'] != instance
                or type(group.get('definition')) is not int or group['definition'] != definition_index
                or group.get('source_definition_id') != definition['source_definition_id']
                or group.get('representation_id') != original['source_representation']
                or group.get('set_id') != original['source_set']
                or group.get('relationship_id') is not None or 'relationship_id' not in group):
            raise ValueError('WireframeGroupSourceMismatch')
        for kind in ('curves', 'points'):
            members = group.get(kind)
            if not isinstance(members, list) or len(members) != len(original[kind]):
                raise ValueError('IncompleteWireframeMembers')
            if len(curves) + len(points) + len(members) > MAX_WIRE_MEMBERS:
                raise ValueError('WireframeMemberLimit')
            for member, reference in zip(members, original[kind]):
                if not isinstance(member, dict) or member.get('source_id') != reference['source_id']:
                    raise ValueError('WireframeMemberSourceMismatch')
                source_id = _identity(member['source_id'])
                if kind == 'points':
                    point = _point(member.get('point_mm'))
                    _root_point_matches(point, reference.get('point_mm'), core['instances'][instance])
                    points.append(WirePointInfo(len(points), source_id, instance, definition_index,
                                                group['set_id'], point))
                    continue
                domain = _domain(member.get('native_domain'))
                if (member.get('basis_id') != reference.get('source_basis')
                        or domain != _domain(reference.get('native_parameter_domain'))
                        or type(reference.get('source_forward_in_native')) is not bool
                        or member.get('source_forward_in_native') is not reference['source_forward_in_native']
                        or member.get('parameter_space') != 'Native'):
                    raise ValueError('WireframeCurveSourceMismatch')
                basis_id = _identity(member.get('basis_id'))
                parameters, polyline = member.get('parameters'), member.get('polyline_mm')
                if (not isinstance(parameters, list) or not isinstance(polyline, list)
                        or not 2 <= len(parameters) == len(polyline) <= MAX_WIRE_SAMPLES):
                    raise ValueError('InvalidWireframePolyline')
                samples += len(parameters)
                if samples > MAX_WIRE_SAMPLES:
                    raise ValueError('WireframeSampleLimit')
                parameters = tuple(_number(value) for value in parameters)
                forward = member['source_forward_in_native']
                endpoints = domain if forward else domain[::-1]
                if ((parameters[0], parameters[-1]) != endpoints
                        or any((b <= a if forward else b >= a) for a, b in zip(parameters, parameters[1:]))):
                    raise ValueError('InvalidWireframeParameterCoverage')
                polyline = tuple(_point(point) for point in polyline)
                status, length = member.get('measurement_status'), member.get('length_mm')
                if status == 'Available':
                    length = _number(length)
                    if length < 0:
                        raise ValueError('InvalidWireframeMeasurement')
                elif status != 'NativeQueryFailed' or length is not None or 'length_mm' not in member:
                    raise ValueError('InvalidWireframeMeasurement')
                accuracy = _number(member.get('length_absolute_accuracy_mm'), positive=True)
                if accuracy != LENGTH_ABSOLUTE_ACCURACY_MM:
                    raise ValueError('InvalidWireframeMeasurementAccuracy')
                curves.append(WireCurveInfo(len(curves), source_id, basis_id, instance, definition_index,
                                             group['set_id'], domain, forward, parameters, polyline,
                                             length, status, accuracy))
    return WireframeScene(tuple(curves), tuple(points), len(groups), wire_instances,
                          manifest['source_sha256'], len(payload))
