# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Validate the opt-in HEADER error receipt before adopting the complete scene."""
from copy import deepcopy


def validate_receipt(report, manifest):
    report = report.document
    if (not manifest.get('source_sha256') or report.get('source_sha256') != manifest['source_sha256']):
        raise ValueError('HeaderMetadataSourceIdentityMismatch')
    receipt = report.get('header_metadata_preservation')
    if (not isinstance(receipt, dict) or receipt.get('policy') != 'PreserveUnsetAuthorization'
            or type(receipt.get('applied')) is not bool or receipt.get('source_graph_complete') is not True):
        raise ValueError('MissingOrInvalidHeaderMetadataReceipt')
    issues = receipt.get('original_diagnostics')
    if not isinstance(issues, list) or len(issues) != int(receipt['applied']):
        raise ValueError('InvalidHeaderMetadataDiagnosticInventory')
    expected = 'KnownNonGeometricErrorPreserved' if issues else 'NoExceptionApplied'
    if receipt.get('header_schema_conformance') != expected:
        raise ValueError('InvalidHeaderMetadataConformance')
    for issue in issues:
        if (not isinstance(issue, dict) or issue.get('key') != 'InvalidHeader'
                or issue.get('component') != 'FILE_NAME' or issue.get('parameter_path') != [7]
                or not isinstance(issue.get('related'), list) or len(issue['related']) != 1):
            raise ValueError('InvalidHeaderMetadataSourcePath')
    return deepcopy(receipt)
