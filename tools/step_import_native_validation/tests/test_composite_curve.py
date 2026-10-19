# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Source composite prerequisites cannot grant native construction or partial publication."""
import unittest

from tools.step_import_native_validation.tests import test_wireframe as wireframe_tests
from tools.step_semantic_validation.tests.test_composite_curve import composite_source


class CompositeNativeTests(unittest.TestCase):
    invoke = wireframe_tests.LinkedWireframeNativeTests.invoke

    def test_qualified_source_still_requires_separate_native_composite_support(self):
        run, report = self.invoke(composite_source())
        self.assertEqual(2, run.returncode, run.stderr or report["diagnostics"])
        self.assertEqual("Checked", report["source_inspection"]["status"])
        self.assertEqual("GeneralWireframeGeometryUnsupported", report["diagnostics"][0]["key"])
        self.assertIsNone(report["model"])
        self.assertEqual(0, report["native_calls"])

    def test_invalid_source_never_reaches_native_calls(self):
        data = composite_source().replace(b".CONTINUOUS.,.T.,#912", b".CONTINUOUS.,.T.,#940")
        run, report = self.invoke(data, "--interop-nongeometric")
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["model"])
        self.assertEqual(0, report["native_calls"])
        self.assertIn("SourceGeometryDependencyViolation", [x["key"] for x in report["diagnostics"]])
