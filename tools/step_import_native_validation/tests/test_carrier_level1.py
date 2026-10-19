# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Source exploration cannot silently enable native carrier interpretation."""
import os
import json
import subprocess
import tempfile
from pathlib import Path
import unittest

from tools.step_semantic_validation.tests.test_carrier_level1 import FLAG, level1_box


class NativeCarrierBoundaryTests(unittest.TestCase):
    def test_native_default_keeps_its_original_carrier_refusal_without_calls(self):
        with tempfile.TemporaryDirectory(prefix="step-carrier-native-default-") as directory:
            source = Path(directory) / "source.step"
            source.write_bytes(level1_box())
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                                  "Q-AP214-development-mm-01", str(source), "--interop-nongeometric"],
                                 capture_output=True, timeout=20)
        report = json.loads(run.stdout)
        self.assertEqual(2, run.returncode, run.stderr or report)
        self.assertIsNone(report["model"])
        self.assertEqual(0, report["native_calls"])
        self.assertIn("UnsupportedImplementationLevel", [x["key"] for x in report["diagnostics"]])

    def test_source_only_flag_is_not_a_native_import_option(self):
        with tempfile.TemporaryDirectory(prefix="step-carrier-native-boundary-") as directory:
            source = Path(directory) / "source.step"
            source.write_bytes(level1_box())
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", str(source), FLAG],
                                 capture_output=True, timeout=20)
        self.assertEqual(2, run.returncode)  # Native CLI's existing invalid-argument exit convention.
        self.assertFalse(run.stdout)
