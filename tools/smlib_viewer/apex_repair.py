# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit cone-point intent and independent owning receipt checks."""
import math
import re
from decimal import Decimal, InvalidOperation
from fractions import Fraction

from .native_report import use_report

_ID = r'(?:0|[1-9][0-9]{0,63})'
_CONFIGURATION = 'ConicalVertexLoopApexCorrection-Study-0.1'


def _allowance(value):
    if (not isinstance(value, str) or len(value) > 80
            or not re.fullmatch(r'\+?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?', value)):
        raise ValueError('InvalidConicalApexMovementBudget')
    try:
        decimal = Decimal(value)
        if not decimal.is_finite() or decimal <= 0 or abs(decimal.adjusted()) > 18:
            raise ValueError('InvalidConicalApexMovementBudget')
        result = Fraction(decimal)
        if max(result.numerator, result.denominator) > 2**63 - 1:
            raise ValueError('InvalidConicalApexMovementBudget')
        return result
    except InvalidOperation as error:
        raise ValueError('InvalidConicalApexMovementBudget') from error


def arguments(settings):
    identity = getattr(settings, 'apex_repair_source_sha256', '')
    uses = tuple(getattr(settings, 'conical_apex_uses', ()))
    limits = [getattr(settings, key, 0) for key in
              ('maximum_apex_repair_vertices', 'maximum_apex_repair_controls', 'maximum_apex_repair_proof_work')]
    move = getattr(settings, 'maximum_apex_repair_move_mm', '')
    total = getattr(settings, 'maximum_apex_repair_total_move_mm', '')
    if not identity and not uses and not any(limits) and not move and not total:
        return []
    if (not isinstance(identity, str) or not re.fullmatch(r'[0-9a-f]{64}', identity) or not uses
            or any(type(n) is not int or n <= 0 for n in limits) or len(uses) > limits[0]
            or limits[1] > 2000000 or limits[2] > 100000000
            or getattr(settings, 'normalize_periodic', False) or getattr(settings, 'restrict_domains', False)
            or getattr(settings, 'selected_definitions', ()) or getattr(settings, 'assembly', False)
            or getattr(settings, 'surface_curve_3d_uses', ())):
        raise ValueError('ExplicitBoundedConicalApexRepairRequired')
    _allowance(move)
    _allowance(total)
    seen = set()
    for use in uses:
        if not isinstance(use, str) or not re.fullmatch(_ID + '(?::' + _ID + '){6}', use):
            raise ValueError('InvalidConicalApexRepairUse')
        fields = use.split(':')
        key = tuple(fields[:2] + fields[5:6])
        if key in seen:
            raise ValueError('AmbiguousConicalApexRepairVertex')
        seen.add(key)
    result = ['--apex-repair-source-sha256', identity, '--max-apex-repair-vertices', str(limits[0]),
              '--max-apex-repair-controls', str(limits[1]), '--max-apex-repair-proof-work', str(limits[2]),
              '--max-apex-repair-move-mm', move, '--max-apex-repair-total-move-mm', total]
    for use in uses:
        result.extend(['--repair-conical-apex-use', use])
    return result


def _number(value):
    try:
        finite = type(value) in (float, int) and math.isfinite(value)
    except OverflowError:
        finite = False
    if not finite:
        raise ValueError('InvalidConicalApexRepairNumber')
    return Fraction(value)


def _fraction(value):
    if not isinstance(value, dict) or value.get('kind') != 'rational':
        raise ValueError('InvalidConicalApexRepairFraction')
    try:
        # Native FractionJson writes exact integer text. Do not truncate a
        # malformed JSON float or coerce bool into an apparently matching budget.
        parts = [value['num'], value['den']]
        if any(type(v) is not int and (not isinstance(v, str) or not re.fullmatch(r'-?(?:0|[1-9][0-9]{0,18})', v))
               for v in parts):
            raise ValueError('InvalidConicalApexRepairFraction')
        numerator, denominator = map(int, parts)
        if not -(2**63) <= numerator < 2**63 or not 0 < denominator < 2**63:
            raise ValueError('InvalidConicalApexRepairFraction')
        return Fraction(numerator, denominator)
    except (KeyError, ValueError, ZeroDivisionError) as error:
        raise ValueError('InvalidConicalApexRepairFraction') from error


def _use(value):
    if not isinstance(value, dict):
        raise ValueError('InvalidConicalApexRepairUseReceipt')
    fields = [value.get(key) for key in
              ('definition_id', 'shape_id', 'face_id', 'bound_id', 'loop_id', 'vertex_id', 'surface_id')]
    if any(not isinstance(v, str) or not re.fullmatch(_ID, v) for v in fields):
        raise ValueError('InvalidConicalApexRepairUseReceipt')
    return ':'.join(fields)


def validate_receipt(directory, manifest, settings, resolve, native_report=None):
    requested = arguments(settings)
    if not requested:
        if manifest.get('repair_kind') == 'ConicalApexVertices':
            raise ValueError('UnexpectedConicalApexRepair')
        return None
    expected = tuple(settings.conical_apex_uses)
    receipt = manifest.get('conical_apex_repair')
    if (manifest.get('native_import_status') != 'AcceptedWithRepairs'
            or manifest.get('repair_kind') != 'ConicalApexVertices'
            or manifest.get('repair_applied') is not True or not isinstance(receipt, dict)
            or receipt.get('source_sha256') != settings.apex_repair_source_sha256
            or receipt.get('request_complete') is not True or receipt.get('uses') != list(expected)
            or type(manifest.get('repaired_vertex_count')) is not int
            or manifest.get('repaired_vertex_count') != len(expected)
            or type(manifest.get('repaired_face_count')) is not int or manifest['repaired_face_count'] != 0):
        raise ValueError('ConicalApexRepairReceiptMismatch')
    move = _allowance(settings.maximum_apex_repair_move_mm)
    total = _allowance(settings.maximum_apex_repair_total_move_mm)
    if (Fraction(receipt['maximum_displacement_mm']) != move
            or Fraction(receipt['maximum_total_displacement_mm']) != total):
        raise ValueError('ConicalApexRepairBudgetMismatch')
    report = use_report(native_report, directory, resolve, settings.report_budget_mib * 1024 * 1024,
                        prefix='ConicalApexRepair')
    if not isinstance(report, dict):
        raise ValueError('InvalidConicalApexRepairReport')
    root = report.get('conical_apex_repair')
    if (report.get('status') != 'AcceptedWithRepairs'
            or report.get('source_sha256') != settings.apex_repair_source_sha256
            or not isinstance(root, dict) or root.get('source_sha256') != settings.apex_repair_source_sha256
            or root.get('configuration') != _CONFIGURATION or root.get('source_bytes_unchanged') is not True
            or type(root.get('tolerance_growth_allowance_mm')) not in (int, float)
            or root.get('tolerance_growth_allowance_mm') != 0
            or [_use(v) for v in root.get('requested_uses', [])] != list(expected)
            or _fraction(root.get('maximum_displacement_mm')) != move
            or _fraction(root.get('maximum_total_displacement_mm')) != total):
        raise ValueError('ConicalApexRepairNativeRequestMismatch')
    for values in (receipt, root):
        for key, capacity in (('maximum_vertices', settings.maximum_apex_repair_vertices),
                              ('maximum_proof_controls', settings.maximum_apex_repair_controls),
                              ('maximum_proof_work', settings.maximum_apex_repair_proof_work)):
            reported = values.get(key)
            if type(reported) is not int or reported != capacity:
                raise ValueError('ConicalApexRepairNativeCapacityMismatch')
    budget = root.get('budget') or {}
    if (budget.get('request_complete') is not True or type(budget.get('corrected_vertices')) is not int
            or budget['corrected_vertices'] != len(expected) or type(budget.get('proof_work')) is not int
            or not 0 < budget['proof_work'] <= settings.maximum_apex_repair_proof_work):
        raise ValueError('ConicalApexRepairNativeBudgetMismatch')
    actions = []

    # Traverse both single-body and grouped reports. Every requested source use
    # must have exactly one owning action; never accept an aggregate count as a
    # substitute for the per-body strict failure and displacement evidence.
    pending = [report]
    while pending:
        node = pending.pop()
        if not isinstance(node, dict):
            raise ValueError('InvalidConicalApexRepairReport')
        repair = node.get('conical_apex_repair') or {}
        evidence = repair.get('evidence')
        if evidence is not None:
            if (not isinstance(evidence, dict) or any(evidence.get(key) is not True for key in
                   ('curves_and_surfaces_identical', 'unrequested_vertices_identical', 'working_tolerances_identical',
                    'original_diagnostics_retained'))):
                raise ValueError('IncompleteConicalApexRepairEvidence')
            original = node.get('original_attempt') or {}
            if (original.get('status') != 'PostcheckFailure' or not original.get('diagnostics')
                    or original.get('repair') != 'None'):
                raise ValueError('MissingConicalApexStrictFailure')
            local = evidence.get('actions')
            if not isinstance(local, list) or not local or len(actions) + len(local) > len(expected):
                raise ValueError('MissingConicalApexRepairActions')
            work = _fraction(node['quality']['maximum_work_tolerance_mm'])
            if work <= 0:
                raise ValueError('InvalidConicalApexWorkTolerance')
            for action in local:
                if (not isinstance(action, dict) or type(action.get('target_vertex')) is not int
                        or action['target_vertex'] < 0 or not isinstance(action.get('source_point_id'), str)
                        or not re.fullmatch(_ID, action['source_point_id'])):
                    raise ValueError('InvalidConicalApexRepairTarget')
                before, after = action.get('original_mm'), action.get('corrected_mm')
                if any(not isinstance(v, list) or len(v) != 3 for v in (before, after)):
                    raise ValueError('InvalidConicalApexRepairPoint')
                displacement = _number(action.get('displacement_upper_mm'))
                protection = _number(action.get('local_protection_lower_mm'))
                squared = sum((_number(a) - _number(b))**2 for a, b in zip(after, before))
                if (not 0 < displacement <= move or protection <= 0 or squared > displacement**2
                        or displacement > protection / 100 or work > protection / 100):
                    raise ValueError('ConicalApexRepairMovementOrProtectionMismatch')
                actions.append(action)
        for entries in ('bodies', 'definitions'):
            children = node.get(entries, [])
            if not isinstance(children, list) or any(not isinstance(entry, dict) for entry in children):
                raise ValueError('InvalidConicalApexRepairReport')
            for entry in children:
                child = entry.get('checks')
                if child is not None:
                    pending.append(child)

    if sorted(_use(a.get('use')) for a in actions) != sorted(expected):
        raise ValueError('ConicalApexRepairActionScopeMismatch')
    cumulative = _number(budget.get('cumulative_displacement_upper_mm'))
    if cumulative > total or sum(_number(a['displacement_upper_mm']) for a in actions) > cumulative:
        raise ValueError('ConicalApexRepairCumulativeMovementMismatch')
    protection = _number(budget.get('minimum_local_protection_lower_mm'))
    if not 0 < protection <= min(_number(a['local_protection_lower_mm']) for a in actions):
        raise ValueError('ConicalApexRepairProtectionReceiptMismatch')
    return dict(receipt, actions=actions, cumulative_displacement_upper_mm=float(cumulative),
                original_source_retained=True)
