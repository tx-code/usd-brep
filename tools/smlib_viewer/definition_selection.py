# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit native definition selection; source checking remains whole-file."""
import re

CONFIGURATION = 'SourceCompleteNativeSelection-0.1'


def arguments(settings):
    identity = getattr(settings, 'selection_source_sha256', '')
    definitions = getattr(settings, 'selected_definitions', ())
    if not identity and not definitions:
        return []
    if (not isinstance(identity, str) or not re.fullmatch(r'[0-9a-f]{64}', identity) or
            not isinstance(definitions, (tuple, list)) or not 0 < len(definitions) <= 1000000 or
            any(not isinstance(value, str) or not re.fullmatch(r'0|[1-9][0-9]{0,63}', value)
                for value in definitions) or
            len(set(definitions)) != len(definitions)):
        raise ValueError('ExplicitSourceBoundDefinitionSelectionRequired')
    result = ['--selection-source-sha256', identity]
    for value in definitions:
        result.extend(['--select-definition', value])
    return result


def validate_manifest(manifest, settings=None):
    receipt = manifest.get('definition_selection')
    requested = bool(arguments(settings)) if settings is not None else None
    if requested is False and receipt is not None:
        raise ValueError('UnexpectedDefinitionSelection')
    if receipt is None:
        if requested:
            raise ValueError('MissingDefinitionSelection')
        return
    if not isinstance(receipt, dict):
        raise ValueError('InvalidDefinitionSelection')
    selected = receipt.get('selected_definitions')
    source_count = receipt.get('source_definition_count')
    if (manifest.get('format') != 'brep-view-scene.v5' or receipt.get('configuration') != CONFIGURATION or
            receipt.get('native_scope') != 'ExplicitSelectedDefinitions' or
            not isinstance(receipt.get('source_sha256'), str) or
            not re.fullmatch(r'[0-9a-f]{64}', receipt['source_sha256']) or
            receipt['source_sha256'] != manifest.get('source_sha256') or
            not isinstance(selected, list) or not selected or type(manifest.get('definition_count')) is not int or
            len(selected) != manifest['definition_count'] or
            any(not isinstance(value, str) or not re.fullmatch(r'0|[1-9][0-9]{0,63}', value) for value in selected) or
            len(set(selected)) != len(selected) or type(source_count) is not int or
            not len(selected) <= source_count <= 1000000):
        raise ValueError('InvalidDefinitionSelection')
    bodies = manifest.get('body_scenes')
    if not isinstance(bodies, list) or not bodies:
        raise ValueError('InvalidDefinitionSelection')
    for body in bodies:
        index = body.get('definition') if isinstance(body, dict) else None
        if (type(index) is not int or not 0 <= index < len(selected) or
                body.get('source_definition_id') != selected[index]):
            raise ValueError('InvalidSelectedBodyIdentity')
    if requested and (receipt['source_sha256'] != settings.selection_source_sha256 or
                      set(selected) != set(settings.selected_definitions)):
        raise ValueError('DefinitionSelectionRequestMismatch')
