# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Bounded assembly metadata transport; complete scene validation stays shared."""
import json
import math

MAX_METADATA_BYTES = 32 * 1024 * 1024
METADATA_FILE = 'scene-metadata.json'


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('DuplicateSceneMetadataField')
        result[key] = value
    return result


def _invalid(value):
    raise ValueError('InvalidSceneMetadataNumber')


def _floating(text):
    value = float(text)
    if not math.isfinite(value):
        raise ValueError('InvalidSceneMetadataNumber')
    return value


def expand_scene(directory, root, resolve):
    """Own exactly one bounded complete v6/v7 descriptor, never partial rows.

    v8 is a transport envelope only. Source/status/format must agree; recursive
    envelopes and arbitrary paths are refused. The returned complete descriptor
    then takes the SAME assembly, topology, qualification and publication checks
    as inline scenes. This layer itself establishes no geometry qualification.
    """
    if not isinstance(root, dict) or root.get('format') != 'brep-view-scene.v8':
        return root
    fields = {'format', 'status', 'native_import_status', 'source_sha256',
              'metadata', 'metadata_format', 'metadata_bytes'}
    count = root.get('metadata_bytes')
    if (set(root) != fields or root.get('metadata') != METADATA_FILE
            or root.get('metadata_format') not in {'brep-view-scene.v6', 'brep-view-scene.v7'}
            or type(count) is not int or not 0 < count <= MAX_METADATA_BYTES
            or any(not isinstance(root.get(key), str)
                   for key in ('status', 'native_import_status', 'source_sha256'))):
        raise ValueError('InvalidSceneMetadataEnvelope')
    path = resolve(directory, METADATA_FILE)
    if path.stat().st_size != count:
        raise ValueError('SceneMetadataByteMismatch')
    with path.open('rb') as stream:
        data = stream.read(count + 1)
    if len(data) != count:
        raise ValueError('SceneMetadataByteMismatch')
    scene = json.loads(data.decode('utf-8'), object_pairs_hook=_object,
                       parse_constant=_invalid, parse_float=_floating)
    if (not isinstance(scene, dict) or scene.get('format') != root['metadata_format']
            or any(scene.get(key) != root[key] for key in ('status', 'native_import_status', 'source_sha256'))):
        raise ValueError('SceneMetadataEnvelopeMismatch')
    return scene
