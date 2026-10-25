# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit original/use-bound 3D priority, without another STEP interpretation."""
import json
import math
from pathlib import Path
import re

INTERPRETATION = 'ExplicitCurve3dPriority'
FIELD = 'surface_curve_3d_interpretation'
QUALITY_IDS = {
    'development': 'Q-AP214-development-mm-01',
    'tolerance-study': 'Q-AP214-tolerance-study-mm-01',
    'work-tolerance-study': 'Q-AP214-work-tolerance-study-mm-01',
    'fine-work-tolerance-study': 'Q-AP214-fine-work-tolerance-study-mm-01',
}
# Fixed viewer profiles from STEP_IMPORT/src/StepImport.cpp. These bind the
# receipt to the chosen application profile; they do not select new budgets.
QUALITY_BUDGETS = {
    'development': ((1, 1000), (1, 1000), (1, 1000)),
    'tolerance-study': ((1, 100), (1, 100), (1, 100)),
    'work-tolerance-study': ((1, 100), (1, 100), (1, 50)),
    'fine-work-tolerance-study': ((1, 1000), (1, 1000), (1, 10000)),
}
QUALIFICATION_FLAGS = ('strict_original_geometry_qualified', 'source_priority_declaration_rewritten',
                       'source_definitions_rewritten', 'source_pcurve_consistency_qualified')


def _use(value):
    if not isinstance(value, str) or len(value) > 512:
        raise ValueError('InvalidSurfaceCurve3dUse')
    fields = value.split(':')
    if (len(fields) not in (7, 8) or fields[5] not in ('PCURVE_S1', 'PCURVE_S2')
            or (fields[5] == 'PCURVE_S2' and len(fields) != 8)
            or any(not re.fullmatch(r'0|[1-9][0-9]{0,63}', field) for field in fields[:5] + fields[6:])):
        raise ValueError('InvalidSurfaceCurve3dUse')
    return tuple(fields)


def arguments(settings):
    identity = getattr(settings, 'surface_curve_3d_source_sha256', '')
    maximum = getattr(settings, 'maximum_surface_curve_3d_uses', 0)
    uses = getattr(settings, 'surface_curve_3d_uses', ())
    if not isinstance(identity, str) or type(maximum) is not int or not isinstance(uses, (tuple, list)):
        raise ValueError('InvalidSurfaceCurve3dRequest')
    if not identity and maximum == 0 and not uses:
        return []
    # StepImportSurfaceCurveSession.cpp owns source binding/eligibility. These
    # are only the same canonical request/combination and application record
    # capacity checks; no schema SHA, path, geometry or prior request is inferred.
    if (not re.fullmatch('[0-9a-f]{64}', identity) or not 0 < len(uses) <= maximum <= 1000000):
        raise ValueError('ExplicitSourceBoundSurfaceCurve3dRequestRequired')
    plans = [_use(value) for value in uses]
    if len({plan[:3] for plan in plans}) != len(plans):
        raise ValueError('DuplicateSurfaceCurve3dUse')
    if (getattr(settings, 'normalize_periodic', False) or getattr(settings, 'normalize_periodic_faces', False)
            or getattr(settings, 'restrict_domains', False)
            or getattr(settings, 'restrict_unused_support_domains', False)
            or getattr(settings, 'repair_source_sha256', '') or getattr(settings, 'face_orientation_edits', ())
            or getattr(settings, 'band_repair_source_sha256', '') or getattr(settings, 'cylindrical_band_repairs', ())
            or getattr(settings, 'selection_source_sha256', '') or getattr(settings, 'selected_definitions', ())
            or getattr(settings, 'source_error_isolation', '')):
        raise ValueError('SurfaceCurve3dPlanCombinationUnsupported')
    result = ['--surface-curve-3d-source-sha256', identity, '--max-surface-curve-3d-uses', str(maximum)]
    for value in uses:
        result.extend(['--surface-curve-3d-use', value])
    return result


def require_explicit_file(settings, filename, *, scan=False):
    if arguments(settings) and (scan or not filename or Path(filename).suffix.lower() not in ('.step', '.stp')):
        raise ValueError('SurfaceCurve3dRequiresOneExplicitStepFile')


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('DuplicateSurfaceCurve3dReportField')
        result[key] = value
    return result


def _invalid_constant(value):
    raise ValueError('InvalidSurfaceCurve3dReportNumber')


def _finite(value):
    if type(value) not in (str, int, float):
        raise ValueError('InvalidSurfaceCurve3dReportNumber')
    try:
        result = float(value)
    except (ValueError, OverflowError) as error:
        raise ValueError('InvalidSurfaceCurve3dReportNumber') from error
    if not math.isfinite(result):
        raise ValueError('InvalidSurfaceCurve3dReportNumber')
    return result


def _plan(entry):
    if not isinstance(entry, dict) or not isinstance(entry.get('source_pcurves'), list):
        raise ValueError('SurfaceCurve3dPlanMismatch')
    fields = [entry.get(key) for key in ('source_definition', 'source_shape', 'source_edge',
                                        'source_surface_curve', 'source_curve_3d', 'original_master')]
    fields.extend(entry['source_pcurves'])
    if any(not isinstance(value, str) for value in fields):
        raise ValueError('SurfaceCurve3dPlanMismatch')
    return _use(':'.join(fields))


def _budgets(receipt, report, profile):
    quality = report.get('quality')
    if not isinstance(quality, dict):
        raise ValueError('SurfaceCurve3dQualityMismatch')
    keys = (('consistency_budget_mm', 'consistency_mm'), ('derived_uv_budget_mm', 'derived_uv_mm'),
            ('maximum_work_tolerance_mm', 'maximum_work_tolerance_mm'))
    for (key, original), expected in zip(keys, QUALITY_BUDGETS[profile]):
        pair, reference = receipt.get(key), quality.get(original)
        if (not isinstance(pair, list) or len(pair) != 2 or any(type(value) is not int for value in pair)
                or pair[0] < 0 or pair[1] <= 0 or not isinstance(reference, dict)
                or reference.get('kind') != 'rational'
                or any(not isinstance(reference.get(part), str)
                       or not re.fullmatch(r'0|[1-9][0-9]{0,19}', reference[part]) for part in ('num', 'den'))
                or int(reference['den']) <= 0
                or pair[0] * int(reference['den']) != int(reference['num']) * pair[1]
                or pair[0] * expected[1] != expected[0] * pair[1]):
            raise ValueError('SurfaceCurve3dQualityMismatch')


def _read_report(directory, settings, resolve):
    report_path = resolve(directory, 'import-report.json')
    budget = max(32, settings.report_budget_mib) * 1024 * 1024
    if report_path.stat().st_size > budget:
        raise ValueError('SurfaceCurve3dReportLimit')
    with report_path.open('rb') as stream:
        payload = stream.read(budget + 1)
    if len(payload) > budget:
        raise ValueError('SurfaceCurve3dReportLimit')
    return json.loads(payload.decode('utf-8'), object_pairs_hook=_object, parse_constant=_invalid_constant,
                      parse_float=_finite)


def observe_failure_receipt(directory, scene, settings, resolve):
    """Retain unfinished native evidence without qualifying or publishing it.

    A later native/resource/display failure must not erase already observed
    original conflicts. The caller keeps parsing failures as secondary details,
    preserving the primary native failure and its existing diagnostics.
    """
    if not arguments(settings) or FIELD not in scene:
        return None
    report = _read_report(directory, settings, resolve)
    if (not isinstance(report, dict) or report.get('source_sha256') != scene.get('source_sha256')
            or report.get('status') != scene.get('native_import_status')
            or not isinstance(report.get(FIELD), dict)
            or report[FIELD].get('source_sha256') != settings.surface_curve_3d_source_sha256):
        raise ValueError('SurfaceCurve3dFailureObservationMismatch')
    return dict(publication='Unpublished', native_report_status=report['status'], receipt=report[FIELD])


def validate_receipt(directory, scene, settings, resolve):
    """Own the complete native receipt before decoding any display geometry.

    The 64 KiB scene carries a small marker only. Plans, original conflicts and
    exact mm budgets stay in the mandatory native import report, with its
    explicit request-selected bound; they are not reconstructed from meshes.
    Legacy/default scenes require no new sidecar and remain unchanged.
    """
    requested = bool(arguments(settings))
    marker = scene.get(FIELD)
    if not requested:
        if FIELD in scene:
            raise ValueError('UnexpectedSurfaceCurve3dReceipt')
        return None
    if not isinstance(marker, dict):
        raise ValueError('MissingSurfaceCurve3dReceiptMatchingWorkerRequired')
    identity = settings.surface_curve_3d_source_sha256
    maximum = settings.maximum_surface_curve_3d_uses
    expected = tuple(_use(value) for value in settings.surface_curve_3d_uses)
    quality_id = QUALITY_IDS.get(settings.profile)
    if (scene.get('native_import_status') != 'AcceptedUnderInterpretation'
            or scene.get('source_sha256') != identity or marker.get('source_sha256') != identity
            or marker.get('interpretation') != INTERPRETATION or marker.get('request_complete') is not True
            or type(marker.get('maximum_uses')) is not int or marker['maximum_uses'] != maximum
            or type(marker.get('plan_count')) is not int or marker['plan_count'] != len(expected)
            or type(marker.get('action_count')) is not int or marker['action_count'] != len(expected)
            or not quality_id or marker.get('quality_id') != quality_id
            or marker.get('report') != 'import-report.json'):
        raise ValueError('SurfaceCurve3dReceiptMismatch')
    report = _read_report(directory, settings, resolve)
    if (not isinstance(report, dict)
            or report.get('format_version') not in {'step-general-native-development.v1',
                'step-material-collection-development.v1', 'step-product-document-development.v1',
                'step-assembly-development.v1'}
            or report.get('status') != scene['native_import_status'] or report.get('candidate_discarded') is not False
            or report.get('source_sha256') != identity or report.get('quality_id') != quality_id):
        raise ValueError('SurfaceCurve3dCoreReceiptMismatch')
    receipt = report.get(FIELD)
    if (not isinstance(receipt, dict) or receipt.get('source_sha256') != identity
            or receipt.get('interpretation') != INTERPRETATION or receipt.get('request_complete') is not True
            or receipt.get('quality_id') != quality_id or type(receipt.get('maximum_uses')) is not int
            or receipt['maximum_uses'] != maximum
            or any(receipt.get(flag) is not False for flag in QUALIFICATION_FLAGS)):
        raise ValueError('SurfaceCurve3dQualificationMismatch')
    plans, actions = receipt.get('plans'), receipt.get('actions')
    if (not isinstance(plans, list) or len(plans) != len(expected)
            or tuple(_plan(plan) for plan in plans) != expected
            or not isinstance(actions, list) or len(actions) != len(expected)):
        raise ValueError('SurfaceCurve3dPlanMismatch')
    _budgets(receipt, report, settings.profile)
    seen = set()
    for action in actions:
        index = action.get('plan_index') if isinstance(action, dict) else None
        if (type(index) is not int or not 0 <= index < len(expected) or index in seen
                or action.get('native_checks_passed') is not True):
            raise ValueError('SurfaceCurve3dActionMismatch')
        seen.add(index)
        interval, conflicts = action.get('native_edge_interval'), action.get('original_conflicts')
        if (not isinstance(interval, list) or len(interval) != 2
                or any(type(value) not in (int, float) for value in interval)
                or not _finite(interval[0]) < _finite(interval[1])
                or not interval[0] < interval[1] or not isinstance(conflicts, list)):
            raise ValueError('SurfaceCurve3dActionMismatch')
        for conflict in conflicts:
            occurrence = conflict.get('association_occurrence') if isinstance(conflict, dict) else None
            pcurves = expected[index][6:]
            if (type(occurrence) is not int or not 1 <= occurrence <= len(pcurves)
                    or conflict.get('source_pcurve') != pcurves[occurrence - 1]
                    or any(not isinstance(conflict.get(key), str)
                           for key in ('status', 'key', 'record_id', 'stage', 'message'))):
                raise ValueError('SurfaceCurve3dConflictMismatch')
    # Return the original complete owning values, including conflicts and action
    # order. In particular, neither deduplicate the source PCURVE LIST nor sort
    # native action order into request order; plan_index is the correspondence.
    return receipt
