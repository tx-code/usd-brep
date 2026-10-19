# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""A lexical compatibility policy never verifies the original source carrier."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_carrier_level1 import level1_box
from tools.step_import_validation.tests.test_lowercase_hex import FLAG, MODE


def hex_box():
    data = level1_box()
    assert data.count(b"'P0 analytic fixture'") == 1
    return data.replace(b"'P0 analytic fixture'", b"'\\X2\\4e2d\\X0\\'")


class NativeLowercaseHexTests(unittest.TestCase):
    def invoke(self, data, *flags, interop=True):
        with tempfile.TemporaryDirectory(prefix="step-native-lower-hex-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            args = [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", str(path),
                    "--quality", "Q-AP214-development-mm-01"]
            if interop:
                args.append("--interop-nongeometric")
            run = subprocess.run(args + list(map(str, flags)), capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_actual_original_and_complete_model_keep_independent_truth_and_evidence(self):
        old, rejected = self.invoke(hex_box(), "--interpret-level1-as-corrected")
        self.assertEqual(1, old.returncode)
        self.assertEqual(0, rejected["native_calls"])
        run, report = self.invoke(hex_box(), FLAG)
        self.assertEqual(11, run.returncode, run.stderr or run.stdout)
        self.assertEqual("AcceptedUnderInterpretation", report["status"])
        self.assertFalse(report["source_carrier_verified"])
        self.assertEqual(MODE, report["source_carrier_interpretation"])
        raw = report["source_inspection"]["source"]
        self.assertEqual(hex_box().decode(), raw["input"]["raw"])
        product = next(r for r in raw["records"] if r["id"] == "4")
        value = product["components"][0]["parameters"][1]
        self.assertEqual("中", value["text"])
        self.assertEqual("'\\X2\\4e2d\\X0\\'", value["raw"])
        self.assertEqual(1, len(raw["carrier_interpretation"]["string_hex_interpretations"]))
        self.assertAlmostEqual(6000, report["model"]["material_volume"]["value_mm3"], delta=1e-5)

    def test_later_failure_and_native_budget_withhold_the_model(self):
        data = hex_box().replace(b"4e2d", b"4e2dGGGG")
        run, report = self.invoke(data, FLAG)
        self.assertEqual(1, run.returncode)
        self.assertIsNone(report["model"])
        self.assertFalse(report["source_inspection"]["source"]["carrier_interpretation"]
                         ["string_hex_interpretations"][-1]["complete"])
        run, report = self.invoke(hex_box(), FLAG, "--max-native-calls", 1)
        self.assertEqual(3, run.returncode)
        self.assertIsNone(report["model"])
        self.assertFalse(report["source_carrier_verified"])

    def test_existing_schema_interpretation_can_be_selected_separately(self):
        data = hex_box().replace(b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}", b"AUTOMOTIVE_DESIGN")
        run, report = self.invoke(data, FLAG, "--interpret-ap214-name-only-as-2010")
        self.assertEqual(11, run.returncode, run.stderr or run.stdout)
        self.assertFalse(report["source_carrier_verified"])
        self.assertFalse(report["source_binding_verified"])
        self.assertEqual("Ap214NameOnlyAs2010", report["source_interpretation"])

    def test_option_does_not_select_interoperability_or_silently_replace_a_policy(self):
        run, report = self.invoke(hex_box(), FLAG, interop=False)
        self.assertEqual(2, run.returncode)
        self.assertIsNone(report)
        run, report = self.invoke(hex_box(), FLAG, "--interpret-level1-as-corrected")
        self.assertEqual(2, run.returncode)
        self.assertIsNone(report)
