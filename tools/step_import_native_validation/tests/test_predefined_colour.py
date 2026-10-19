# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Appearance leaves preserve analytic geometry and separate source qualification."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP214
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX
from tools.step_semantic_validation.tests.test_ap214_predefined_colour import colour_source
from tools.step_semantic_validation.tests.test_interpretation import UNREGISTERED


class NativePredefinedColourTests(unittest.TestCase):
    def test_named_colour_preserves_the_box_and_distinct_interpretation_status(self):
        for schema in (AP214, MBX, UNREGISTERED):
            with self.subTest(schema=schema), tempfile.TemporaryDirectory(prefix="step-native-palette-") as directory:
                data = colour_source(schema=schema, style="curve")
                path = Path(directory) / "source.step"
                path.write_bytes(data)
                command = [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                           "Q-AP214-development-mm-01", "--interop-nongeometric", str(path)]
                if schema == UNREGISTERED:
                    command.append("--interpret-unregistered-ap214-2010")
                run = subprocess.run(command, capture_output=True, timeout=30)
                self.assertEqual(11 if schema == UNREGISTERED else 0, run.returncode, run.stderr)
                report = json.loads(run.stdout)
                self.assertEqual("AcceptedUnderInterpretation" if schema == UNREGISTERED else "Accepted",
                                 report["status"])
                self.assertEqual((6, 12, 8), tuple(report["model"][k] for k in ("faces", "edges", "vertices")))
                self.assertAlmostEqual(6000, report["model"]["material_volume"]["value_mm3"], delta=1e-5)
                self.assertAlmostEqual(2200, report["model"]["area_mm2"], delta=1e-6)
                self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])
                if schema == UNREGISTERED:
                    self.assertFalse(report["source_binding_verified"])
                    self.assertEqual("UnsupportedSchemaBinding", report["source_inspection"]["schema_interpretation"]
                                     ["original_binding_diagnostics"][0]["key"])

    def test_invalid_palette_has_no_native_candidate_even_with_interoperability(self):
        with tempfile.TemporaryDirectory(prefix="step-native-invalid-palette-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(colour_source(b"'RED'", style="curve"))
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                                  "Q-AP214-development-mm-01", "--interop-nongeometric", str(path)],
                                 capture_output=True, timeout=30)
            self.assertEqual(1, run.returncode, run.stderr)
            report = json.loads(run.stdout)
            self.assertIsNone(report["model"])
            self.assertEqual(0, report["native_calls"])
            self.assertEqual("SourceRuleViolation:DRAUGHTING_PRE_DEFINED_COLOUR.WR1",
                             report["source_inspection"]["diagnostics"][0]["key"])

    def test_query_copy_coefficients_and_archive_still_pass_with_a_named_colour(self):
        with tempfile.TemporaryDirectory(prefix="step-native-palette-copy-") as directory:
            root = Path(directory)
            path, output = root / "source.step", root / "out"
            path.write_bytes(colour_source(style="curve"))
            cli = Path(os.environ["STEP_NATIVE_BINARY"])
            binary = os.environ.get("STEP_NATIVE_GENERAL_BINARY",
                                    str(cli.with_name("step_import_general_tests" + cli.suffix)))
            run = subprocess.run([binary, "--development-file", str(path), str(output), "normal"],
                                 capture_output=True, timeout=30)
            self.assertEqual(0, run.returncode, run.stderr)
            checks = json.loads((output / "checks.json").read_bytes())
            self.assertEqual("Passed", checks["status"])
            self.assertTrue(checks["defining_coefficients_and_uv_checked"])
            for field in ("volume_mm3", "copied_volume_mm3", "reloaded_volume_mm3"):
                self.assertAlmostEqual(6000, checks[field], delta=1e-5)
