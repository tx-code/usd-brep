# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Attached date/rendering source values cannot change native geometry."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214_metadata import rendering_source


class Ap214MetadataNativeTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="ap214-metadata-native-") as directory:
            path = Path(directory) / "input.step"
            path.write_bytes(data)
            result = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality", "Q-AP214-development-mm-01",
                                     str(path), *options], capture_output=True, timeout=30)
        return result, json.loads(result.stdout) if result.stdout else None

    def test_missing_display_method_is_preserved_only_after_explicit_interop(self):
        data = rendering_source(b"$")
        run, rejected = self.invoke(data)
        self.assertEqual(1, run.returncode)
        self.assertEqual(0, rejected["native_calls"])
        self.assertIsNone(rejected["model"])
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, result["diagnostics"])
        self.assertEqual((6, 12, 8), tuple(result["model"][k] for k in ["faces", "edges", "vertices"]))
        self.assertAlmostEqual(6000, result["model"]["material_volume"]["value_mm3"], places=6)
        self.assertAlmostEqual(2200, result["model"]["area_mm2"], places=6)
        self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, result["validations"])
        self.assertEqual("rendering_method", result["source_inspection"]["preserved_diagnostics"][0]["field"])
        self.assertEqual(data.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])

    def test_valid_rendering_metadata_keeps_the_same_analytic_box(self):
        run, result = self.invoke(rendering_source())
        self.assertEqual(0, run.returncode, result["diagnostics"])
        self.assertAlmostEqual(6000, result["model"]["material_volume"]["value_mm3"], places=6)
        self.assertEqual([], result["source_inspection"].get("preserved_diagnostics", []))

    def test_unowned_style_failure_precedes_any_native_call(self):
        data = rendering_source(b"$").replace(
            b"#950=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('display',(#951),#12);\n", b"")
        run, rejected = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertEqual(0, rejected["native_calls"])
        self.assertIsNone(rejected["model"])
