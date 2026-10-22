# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit one-file repair intent, separate from source interpretation policy."""
import re


def arguments(settings):
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
