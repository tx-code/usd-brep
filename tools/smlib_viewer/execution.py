# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit application resources, independent of physical quality and repair."""
import json

SIDECAR = 'execution-resources.json'


def arguments(settings):
    work = getattr(settings, 'max_exact_work_units', None)
    deadline = getattr(settings, 'deadline_ms', None)
    timeout = getattr(settings, 'timeout_seconds', getattr(settings, 'worker_timeout_seconds', 120))
    if type(timeout) is not int or not 0 < timeout <= 2147483:
        raise ValueError('InvalidWorkerTimeoutSeconds')
    result = []
    for flag, value, maximum in (('--max-exact-work-units', work, (1 << 63) - 1),
                                 ('--deadline-ms', deadline, (1 << 31) - 1)):
        if value is not None:
            if type(value) is not int or not 0 < value <= maximum:
                raise ValueError('InvalidExecutionResourceArgument')
            result.extend([flag, str(value)])
    if deadline is not None and deadline >= timeout * 1000:
        raise ValueError('ExplicitDeadlineRequiresLongerWorkerTimeout')
    return result


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('DuplicateExecutionResourceField')
        result[key] = value
    return result


def validate_receipt(directory, scene, settings, resolve):
    """The worker captures applied options AND actual core work/deadline evidence.

    This fixed small sidecar avoids reading a second copy of the full report.
    It must be owned and checked before mesh decoding/scene adoption, and has
    no geometry qualification of its own. Legacy requests need no new payload.
    """
    if not arguments(settings):
        if (directory / SIDECAR).exists():
            raise ValueError('UnexpectedExecutionResourceReceipt')
        return None
    path = resolve(directory, SIDECAR)
    with path.open('rb') as stream:
        payload = stream.read(4097)
    if len(payload) > 4096:
        raise ValueError('ExecutionResourceReceiptLimit')
    value = json.loads(payload.decode('utf-8'), object_pairs_hook=_object)
    work = getattr(settings, 'max_exact_work_units', None) or 2000000
    deadline = getattr(settings, 'deadline_ms', None) or 90000
    fields = {'format', 'scope', 'source_sha256', 'max_exact_work_units',
              'deadline_ms', 'deadline_configured', 'exact_geometry_work'}
    if (not isinstance(value, dict) or set(value) != fields or value.get('format') != 'brep-view-execution.v1'
            or value.get('scope') != 'ApplicationResourcesOnly'
            or not scene.get('source_sha256') or value.get('source_sha256') != scene['source_sha256']
            or type(value.get('max_exact_work_units')) is not int or value['max_exact_work_units'] != work
            or type(value.get('deadline_ms')) is not int or value['deadline_ms'] != deadline
            or value.get('deadline_configured') is not True):
        raise ValueError('ExecutionResourceReceiptMismatch')
    observed = value.get('exact_geometry_work')
    if observed is not None and (not isinstance(observed, dict)
            or set(observed) != {'maximum_units', 'used_units'}
            or type(observed.get('maximum_units')) is not int or observed['maximum_units'] != work
            or type(observed.get('used_units')) is not int or not 0 <= observed['used_units'] <= work):
        raise ValueError('ExecutionResourceWorkEvidenceMismatch')
    return value
