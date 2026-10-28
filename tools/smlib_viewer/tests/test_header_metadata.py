# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
from copy import deepcopy
from pathlib import Path
import unittest

from tools.smlib_viewer.backend import ImportSettings
from tools.smlib_viewer.header_metadata import validate_receipt
from tools.smlib_viewer.native_report import LoadedReport


class HeaderMetadataTests(unittest.TestCase):
    def baseline(self):
        identity = 'a' * 64
        issue = {'key': 'InvalidHeader', 'component': 'FILE_NAME', 'parameter_path': [7],
                 'related': [{'span': [100, 101]}]}
        receipt = {'policy': 'PreserveUnsetAuthorization', 'applied': True, 'source_graph_complete': True,
                   'header_schema_conformance': 'KnownNonGeometricErrorPreserved', 'original_diagnostics': [issue]}
        document = {'source_sha256': identity, 'header_metadata_preservation': receipt}
        return LoadedReport(Path('import-report.json'), 1024, document), {'source_sha256': identity}

    def test_explicit_settings_and_owned_receipt(self):
        self.assertFalse(ImportSettings().preserve_unset_header_authorization)
        with self.assertRaises(ValueError):
            ImportSettings(strict_source=True, preserve_unset_header_authorization=True)
        report, manifest = self.baseline()
        evidence = validate_receipt(report, manifest)
        report.document['header_metadata_preservation']['original_diagnostics'].clear()
        self.assertEqual(len(evidence['original_diagnostics']), 1)

    def test_missing_mismatched_or_incomplete_receipt_refuses_adoption(self):
        for mode in range(6):
            report, manifest = self.baseline()
            fields = report.document['header_metadata_preservation']
            if mode == 0:
                report.document.pop('header_metadata_preservation')
            elif mode == 1:
                manifest['source_sha256'] = 'b' * 64
            elif mode == 2:
                fields['source_graph_complete'] = False
            elif mode == 3:
                fields['original_diagnostics'][0]['parameter_path'] = [1]
            elif mode == 4:
                fields['original_diagnostics'].append(deepcopy(fields['original_diagnostics'][0]))
            else:
                fields['header_schema_conformance'] = 'Conforming'
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                validate_receipt(report, manifest)

    def test_option_with_no_exception_is_not_fabricated(self):
        report, manifest = self.baseline()
        fields = report.document['header_metadata_preservation']
        fields.update(applied=False, original_diagnostics=[], header_schema_conformance='NoExceptionApplied')
        self.assertFalse(validate_receipt(report, manifest)['applied'])


if __name__ == '__main__':
    unittest.main()
