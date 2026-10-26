# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""One bounded owning parse shared by independent native-report comparators."""
from dataclasses import dataclass
import json
import math
from pathlib import Path


@dataclass(frozen=True)
class LoadedReport:
    path: Path
    byte_count: int
    document: dict


def read_report(directory, resolve, budget, *, prefix='Native'):
    if type(budget) is not int or not 0 < budget <= 256 * 1024 * 1024:
        raise ValueError('InvalidNativeReportCapacity')
    path = resolve(directory, 'import-report.json')
    if path.stat().st_size > budget:
        raise ValueError(prefix + 'ReportLimit')
    with path.open('rb') as stream:
        payload = stream.read(budget + 1)
    if len(payload) > budget:
        raise ValueError(prefix + 'ReportLimit')

    def object_fields(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Duplicate' + prefix + 'ReportField')
            result[key] = value
        return result

    def number(text):
        value = float(text)
        if not math.isfinite(value):
            raise ValueError('Invalid' + prefix + 'ReportNumber')
        return value

    document = json.loads(payload.decode('utf-8'), object_pairs_hook=object_fields,
                          parse_constant=number, parse_float=number)
    return LoadedReport(path, len(payload), document)


def use_report(shared, directory, resolve, budget, *, prefix='Native'):
    # A request-local owning parse is reusable; a bare dictionary, another
    # directory's receipt, or a larger capture cannot bypass the caller's bound.
    if type(budget) is not int or not 0 < budget <= 256 * 1024 * 1024:
        raise ValueError('InvalidNativeReportCapacity')
    if shared is None:
        shared = read_report(directory, resolve, budget, prefix=prefix)
    if (not isinstance(shared, LoadedReport) or shared.path != resolve(directory, 'import-report.json')
            or shared.byte_count > budget):
        raise ValueError(prefix + 'ReportCaptureMismatch')
    return shared.document
