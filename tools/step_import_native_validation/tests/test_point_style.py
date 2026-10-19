# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Point appearance does not resize or move imported physical points."""
import unittest

from tools.step_import_native_validation.tests import test_wireframe as wireframe_tests
from tools.step_semantic_validation.tests.test_point_style import point_style_source
from tools.step_semantic_validation.tests.test_wireframe import linked_wire_source


class PointStyleNativeTests(unittest.TestCase):
    invoke = wireframe_tests.LinkedWireframeNativeTests.invoke

    def test_point_appearance_keeps_complete_physical_geometry_and_call_sequence(self):
        run, before = self.invoke(linked_wire_source(), write=True)
        self.assertEqual(0, run.returncode, before["diagnostics"])
        for marker in (b"#961", b"MARKER_TYPE(.RING.)"):
            data = point_style_source(marker=marker)
            if marker != b"#961":
                data = data.replace(b"#961=PRE_DEFINED_POINT_MARKER_SYMBOL('circle');\n", b"")
            run, report = self.invoke(data, write=True)
            self.assertEqual(0, run.returncode, run.stderr or report["diagnostics"])
            self.assertEqual(before["model"], report["model"])
            self.assertEqual(before["linked_wireframes"], report["linked_wireframes"])
            self.assertEqual(before["native_calls"], report["native_calls"])

    def test_invalid_marker_or_size_fails_without_native_allocation(self):
        for data in (point_style_source(size=b"POSITIVE_LENGTH_MEASURE(0.)"),
                     point_style_source().replace(b"SYMBOL('circle')", b"SYMBOL('unknown')")):
            run, report = self.invoke(data, "--interop-nongeometric")
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(report["model"])
            self.assertEqual(0, report["native_calls"])
