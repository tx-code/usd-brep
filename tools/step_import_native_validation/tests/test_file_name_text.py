# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Header text preservation does not select geometry or a schema."""
import unittest
from tools.step_import_native_validation.tests import test_general
from tools.step_inspect_validation.tests.test_file_name_text import FLAG, HEADER
from tools.step_semantic_validation.tests.test_ap214 import CORPUS, ap214_source


class NativeFileNameTextTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes

    def test_explicit_header_metadata_keeps_analytic_box_and_source(self):
        source = ap214_source((CORPUS/'S203-P04/input.step').read_bytes())
        start = source.index(b'FILE_NAME(')
        end = source.index(b';', start)+1
        source = source[:start]+HEADER+source[end:]
        strict, _ = self.invoke_bytes(source, FLAG)
        self.assertNotEqual(0, strict.returncode)
        run, report = self.invoke_bytes(source, FLAG, '--interop-nongeometric')
        self.assertEqual(0, run.returncode, run.stderr or report)
        self.assertEqual('Accepted', report['status'])
        self.assertEqual('None', report['repair'])
        self.assertAlmostEqual(6000, report['model']['material_volume']['value_mm3'], delta=1e-5)
        raw = report['source_inspection']['source']
        self.assertEqual(source, raw['input']['raw'].encode('latin1'))
        self.assertEqual('PreserveFileNameText', raw['header_name_policy'])
        self.assertEqual(7, len(raw['diagnostics']))
