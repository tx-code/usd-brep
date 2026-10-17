# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Repeatable qualification through the real four command-line interfaces."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

from tools.step_import_validation.common import load_json


class QualificationTests(unittest.TestCase):
    def test_all_frozen_cases_have_fresh_bound_stage_results(self):
        with tempfile.TemporaryDirectory(prefix="step-profile-") as root:
            output = Path(root) / "qualification"
            args = [sys.executable, "-m", "tools.step_import_native_validation", "--output", str(output)]
            for name, variable in (("source", "STEP_INSPECT_BINARY"), ("semantic", "STEP_SEMANTIC_BINARY"),
                                   ("planar", "STEP_PLANAR_BINARY"), ("native", "STEP_NATIVE_BINARY")):
                args += ["--" + name, os.environ[variable]]
            actual = subprocess.run(args, capture_output=True, timeout=180)
            self.assertEqual(0, actual.returncode, actual.stderr or actual.stdout)
            report = load_json(output / "qualification.json")
            self.assertEqual("Matched", report["status"])
            self.assertEqual({"Accepted": 13, "Rejected": 29, "Unsupported": 7}, report["native_outcomes"])
            self.assertEqual(49, len(report["cases"]))
            for case in report["cases"]:
                self.assertEqual({"source", "semantic", "planar", "native"}, set(case["stages"]))
                self.assertTrue(all(r["comparison"] == "Matched" for r in case["stages"].values()))
                self.assertTrue(all((output / case["case_id"] / (stage + ".stdout.json")).is_file()
                                    for stage in case["stages"]))
            self.assertEqual("Matched", report["combination"]["status"])
            self.assertFalse(report["whole_stage_qualification"])
            # Repeating a run must not merge current results into an older directory.
            repeated = subprocess.run(args, capture_output=True, timeout=10)
            self.assertNotEqual(0, repeated.returncode)


if __name__ == "__main__":
    unittest.main()
