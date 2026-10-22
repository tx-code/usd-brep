# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Metadata target retention cannot change the independent analytic body."""
import unittest

from tools.step_import_native_validation.tests import test_general
from tools.step_semantic_validation.tests.test_style_representation import styled_representation_box


class NativeStyleRepresentationTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes

    def test_selected_style_target_keeps_complete_box_and_source_error(self):
        source = styled_representation_box()
        strict, rejected = self.invoke_bytes(source)
        self.assertNotEqual(0, strict.returncode)
        self.assertIsNone(rejected['model'])
        run, report = self.invoke_bytes(source, '--interop-nongeometric')
        self.assertEqual(0, run.returncode, run.stderr or report)
        self.assertEqual('Accepted', report['status'])
        self.assertEqual('None', report['repair'])
        self.assertEqual((6, 12, 8), tuple(report['model'][k] for k in ['faces', 'edges', 'vertices']))
        self.assertAlmostEqual(6000, report['model']['material_volume']['value_mm3'], delta=1e-5)
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('latin1'))
        self.assertEqual(['ReferenceTypeMismatch'], [d['key'] for d in report['source_inspection']['preserved_diagnostics']])
