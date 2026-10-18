# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Appearance admission preserves the independent analytic material box."""
import unittest

from tools.step_import_native_validation.tests import test_general as general
from tools.step_semantic_validation.tests.test_ap214_curve_style import AP214, MBX, curve_style_source


class NativeCurveStyleTests(unittest.TestCase):
    def assert_box(self, source, report):
        self.assertEqual((6, 12, 8), tuple(report["model"][key] for key in ("faces", "edges", "vertices")))
        self.assertAlmostEqual(6000, report["model"]["material_volume"]["value_mm3"], delta=1e-6)
        self.assertAlmostEqual(2200, report["model"]["area_mm2"], delta=1e-6)
        self.assertEqual(source.decode("ascii"), report["source_inspection"]["source"]["input"]["raw"])

    def test_valid_curve_styles_keep_box_geometry_and_source(self):
        for schema in (AP214, MBX):
            for width in (b"POSITIVE_LENGTH_MEASURE(0.5)", b"DESCRIPTIVE_MEASURE('by context')"):
                with self.subTest(schema=schema, width=width):
                    source = curve_style_source(width, schema=schema)
                    run, report = general.GeneralTests().invoke_bytes(source)
                    self.assertEqual(0, run.returncode, run.stderr or report)
                    self.assert_box(source, report)

    def test_invalid_display_width_never_changes_the_physical_model(self):
        source = curve_style_source(b"'POSITIVE_LENGTH_MEASURE(0.)'")
        strict, rejected = general.GeneralTests().invoke_bytes(source)
        self.assertNotEqual(0, strict.returncode)
        self.assertIsNone(rejected["model"])
        run, report = general.GeneralTests().invoke_bytes(source, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, run.stderr or report)
        self.assert_box(source, report)
        self.assertEqual(rejected["source_inspection"]["diagnostics"],
                         report["source_inspection"]["preserved_diagnostics"])
