# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Source-bound definition selection with explicit, qualified local-error isolation."""
import re

CONFIGURATION = 'SourceCompleteNativeSelection-0.1'
ISOLATED_CONFIGURATION = 'SourceFaceClauseSelection-0.1'


def arguments(settings):
    identity = getattr(settings, 'selection_source_sha256', '')
    definitions = getattr(settings, 'selected_definitions', ())
    isolation = getattr(settings, 'source_error_isolation', '')
    failures = getattr(settings, 'maximum_isolated_source_failures', 0)
    visits = getattr(settings, 'maximum_source_influence_visits', 0)
    if (isolation not in ('', 'FaceBoundsClause') or not isinstance(isolation, str) or
            type(failures) is not int or type(visits) is not int):
        raise ValueError('InvalidSourceIsolationPolicy')
    if isolation or failures or visits:
        if (isolation != 'FaceBoundsClause' or not identity or not definitions or
                type(failures) is not int or not 0 < failures <= 1000000 or
                type(visits) is not int or not 0 < visits <= 10000000 or
                getattr(settings, 'repair_source_sha256', '') or getattr(settings, 'band_repair_source_sha256', '')):
            raise ValueError('ExplicitSelectedSourceIsolationRequired')
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
    if isolation:
        result.extend(['--source-error-isolation', isolation, '--max-isolated-source-failures', str(failures),
                       '--max-source-influence-visits', str(visits)])
    return result


def validate_manifest(manifest, settings=None):
    receipt = manifest.get('definition_selection')
    requested = bool(arguments(settings)) if settings is not None else None
    if requested is False and receipt is not None:
        raise ValueError('UnexpectedDefinitionSelection')
    if receipt is None:
        if requested or manifest.get('native_import_status') == 'AcceptedWithSourceIsolation':
            raise ValueError('MissingDefinitionSelection')
        return
    if not isinstance(receipt, dict):
        raise ValueError('InvalidDefinitionSelection')
    selected = receipt.get('selected_definitions')
    source_count = receipt.get('source_definition_count')
    configuration = receipt.get('configuration')
    if (manifest.get('format') != 'brep-view-scene.v5' or
            configuration not in (CONFIGURATION, ISOLATED_CONFIGURATION) or
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
    if configuration == ISOLATED_CONFIGURATION:
        count = receipt.get('isolated_source_failure_count')
        maximum, visits = receipt.get('maximum_isolated_failures'), receipt.get('maximum_influence_visits')
        work = receipt.get('influence_visits')
        if (type(count) is not int or type(maximum) is not int or not 0 <= count <= maximum <= 1000000 or not maximum or
                type(visits) is not int or not 0 < visits <= 10000000 or
                type(work) is not int or not 0 < work <= visits or
                receipt.get('source_scope') != ('SelectedDefinitionsOnly' if count else 'AllDefinitionsChecked') or
                (count > 0 and (source_count <= len(selected) or
                                manifest.get('native_import_status') != 'AcceptedWithSourceIsolation')) or
                (count == 0 and manifest.get('native_import_status') not in
                 ('Accepted', 'AcceptedUnderInterpretation'))):
            raise ValueError('InvalidSourceIsolationQualification')
        if requested and (getattr(settings, 'source_error_isolation', '') != 'FaceBoundsClause' or
                          settings.maximum_isolated_source_failures != maximum or
                          settings.maximum_source_influence_visits != visits):
            raise ValueError('SourceIsolationRequestMismatch')
    elif (manifest.get('native_import_status') == 'AcceptedWithSourceIsolation' or
          (requested and getattr(settings, 'source_error_isolation', ''))):
        raise ValueError('MissingSourceIsolationQualification')
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
