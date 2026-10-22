# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit one-file repair intent, separate from source interpretation policy."""
import re


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
    identity = getattr(settings, 'band_repair_source_sha256', '')
    faces = tuple(getattr(settings, 'cylindrical_band_repairs', ()))
    limits = [getattr(settings, name, 0) for name in
              ('maximum_band_repair_faces', 'maximum_band_bound_edits', 'maximum_band_proof_operations')]
    if not identity and not faces and not any(limits):
        return orientation
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
    return result
