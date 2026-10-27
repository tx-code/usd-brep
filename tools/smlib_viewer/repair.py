# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit one-file repair intent, separate from source interpretation policy."""
import re
from decimal import Decimal, InvalidOperation
from fractions import Fraction
from .apex_repair import arguments as apex_arguments


def _circle_vertex_arguments(settings):
    count = getattr(settings, 'maximum_band_circle_vertices', 0)
    move = getattr(settings, 'maximum_band_vertex_move_mm', '')
    total = getattr(settings, 'maximum_band_total_vertex_move_mm', '')
    edits = tuple(getattr(settings, 'band_circle_vertices', ()))
    if not count and not move and not total and not edits:
        return []
    if type(count) is not int or count <= 0 or not edits or len(edits) > count:
        raise ValueError('ExplicitBoundedCircleVertexPlanRequired')
    for value in (move, total):
        # Keep exact decimal text on the wire, matching the native int64
        # rational capacity; never convert a displacement budget through float.
        if (not isinstance(value, str) or len(value) > 80
                or not re.fullmatch(r'\+?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?', value)):
            raise ValueError('InvalidCircleVertexMovementBudget')
        try:
            decimal = Decimal(value)
            if not decimal.is_finite() or decimal <= 0 or abs(decimal.adjusted()) > 18:
                raise ValueError('InvalidCircleVertexMovementBudget')
            fraction = Fraction(decimal)
            if max(fraction.numerator, fraction.denominator) > 2**63 - 1:
                raise ValueError('InvalidCircleVertexMovementBudget')
        except InvalidOperation as error:
            raise ValueError('InvalidCircleVertexMovementBudget') from error
    result = ['--max-band-circle-vertices', str(count), '--max-band-vertex-move-mm', move,
              '--max-band-total-vertex-move-mm', total]
    seen = set()
    for edit in edits:
        if not isinstance(edit, str) or not re.fullmatch(r'(?:0|[1-9][0-9]{0,63})(?::(?:0|[1-9][0-9]{0,63})){3}', edit):
            raise ValueError('InvalidCircleVertexEdit')
        key = tuple(edit.split(':')[:3])
        if key in seen:
            raise ValueError('AmbiguousCircleVertexEdit')
        seen.add(key)
        result.extend(['--project-band-circle-vertex', edit])
    return result


def _orientation_arguments(settings):
    identity = getattr(settings, 'repair_source_sha256', '')
    edits = tuple(getattr(settings, 'face_orientation_edits', ()))
    limits = [getattr(settings, name, 0) for name in
              ('maximum_face_orientation_edits', 'maximum_repair_proof_controls', 'maximum_repair_proof_products')]
    if not identity and not edits and not any(limits):
        return []
    if (not isinstance(identity, str) or not re.fullmatch(r'[0-9a-f]{64}', identity) or not edits or
            any(type(n) is not int or n <= 0 for n in limits) or len(edits) > limits[0] or
            limits[1] > 2000000 or limits[2] > 100000000 or
            getattr(settings, 'normalize_periodic', getattr(settings, 'normalize_periodic_faces', False)) or
            getattr(settings, 'restrict_domains', getattr(settings, 'restrict_unused_support_domains', False))):
        raise ValueError('ExplicitBoundedRepairPlanRequired')
    seen = set()
    for edit in edits:
        if not isinstance(edit, str) or not re.fullmatch(r'(?:0|[1-9][0-9]{0,63}):(?:0|[1-9][0-9]{0,63}):'
                                                       r'(?:0|[1-9][0-9]{0,63}):[TF]:[TF]', edit):
            raise ValueError('InvalidFaceOrientationEdit')
        fields = edit.split(':')
        key = tuple(fields[:3])
        if key in seen or fields[3] == fields[4]:
            raise ValueError('AmbiguousFaceOrientationEdit')
        seen.add(key)
    result = ['--repair-source-sha256', identity, '--max-face-orientation-edits', str(limits[0]),
              '--max-repair-proof-controls', str(limits[1]), '--max-repair-proof-products', str(limits[2])]
    for edit in edits:
        result.extend(['--repair-face-orientation', edit])
    return result


def arguments(settings):
    orientation = _orientation_arguments(settings)
    apex = apex_arguments(settings)
    vertices = _circle_vertex_arguments(settings)
    identity = getattr(settings, 'band_repair_source_sha256', '')
    faces = tuple(getattr(settings, 'cylindrical_band_repairs', ()))
    limits = [getattr(settings, name, 0) for name in
              ('maximum_band_repair_faces', 'maximum_band_bound_edits', 'maximum_band_proof_operations')]
    if apex and (orientation or vertices or identity or faces or any(limits)):
        raise ValueError('ConicalApexRepairCombinationUnqualified')
    if not identity and not faces and not any(limits):
        if vertices:
            raise ValueError('CircleVerticesRequireCylindricalBandPlan')
        return orientation + apex
    if (orientation or not isinstance(identity, str) or not re.fullmatch(r'[0-9a-f]{64}', identity) or not faces or
            any(type(n) is not int or n <= 0 for n in limits) or len(faces) > limits[0] or len(faces) * 2 > limits[1] or
            limits[2] > 100000000 or getattr(settings, 'selected_definitions', ()) or
            getattr(settings, 'normalize_periodic', getattr(settings, 'normalize_periodic_faces', False)) or
            getattr(settings, 'restrict_domains', getattr(settings, 'restrict_unused_support_domains', False))):
        raise ValueError('ExplicitBoundedCylindricalBandRepairRequired')
    seen = set()
    for face in faces:
        if not isinstance(face, str) or not re.fullmatch(r'(?:0|[1-9][0-9]{0,63})(?::(?:0|[1-9][0-9]{0,63})){4}', face):
            raise ValueError('InvalidCylindricalBandRepairFace')
        fields = face.split(':')
        if tuple(fields[:3]) in seen or fields[3] == fields[4]:
            raise ValueError('AmbiguousCylindricalBandRepairFace')
        seen.add(tuple(fields[:3]))
    result = ['--band-repair-source-sha256', identity, '--max-band-repair-faces', str(limits[0]),
              '--max-band-bound-edits', str(limits[1]), '--max-band-proof-operations', str(limits[2])]
    for face in faces:
        result.extend(['--repair-cylindrical-band', face])
    result.extend(vertices)
    return result


def validate_vertex_manifest(manifest, settings):
    edits = tuple(getattr(settings, 'band_circle_vertices', ()))
    receipt = manifest.get('circle_vertex_repair')
    if not edits:
        if receipt is not None or manifest.get('repair_kind') == 'CylindricalBandWithCircleVertices':
            raise ValueError('UnexpectedCircleVertexRepair')
        return
    if not isinstance(receipt, dict):
        raise ValueError('MissingCircleVertexRepairReceipt')
    for key, value in (('maximum_displacement_mm', settings.maximum_band_vertex_move_mm),
                       ('maximum_total_displacement_mm', settings.maximum_band_total_vertex_move_mm)):
        actual = receipt.get(key)
        if not isinstance(actual, str) or len(actual) > 80:
            raise ValueError('CircleVertexRepairBudgetMismatch')
        try:
            if Fraction(actual) != Fraction(value):
                raise ValueError('CircleVertexRepairBudgetMismatch')
        except (ZeroDivisionError, ValueError) as error:
            raise ValueError('CircleVertexRepairBudgetMismatch') from error
    if (manifest.get('repair_kind') != 'CylindricalBandWithCircleVertices'
            or manifest.get('native_import_status') != 'AcceptedWithRepairs'
            or manifest.get('repair_applied') is not True
            or type(manifest.get('repaired_vertex_count')) is not int or manifest['repaired_vertex_count'] != len(edits)
            or type(receipt.get('maximum_vertices')) is not int
            or receipt['maximum_vertices'] != settings.maximum_band_circle_vertices
            or receipt.get('source_sha256') != settings.band_repair_source_sha256
            or receipt.get('request_complete') is not True or receipt.get('vertices') != list(edits)):
        raise ValueError('CircleVertexRepairReceiptMismatch')
