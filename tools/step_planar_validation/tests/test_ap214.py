# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""AP214 source success must stop before the AP203 rule evaluator."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import CORPUS, ap214_source, converted_ap214_source


class Ap214RuleBoundaryTests(unittest.TestCase):
    def test_both_si_and_conversion_profiles_leave_protocol_rules_unrun(self):
        cases = ((ap214_source((CORPUS / "S203-P01/input.step").read_bytes()), "Checked"),
                 (converted_ap214_source(), "Deferred"))
        for data, source_status in cases:
            with self.subTest(source_status=source_status), tempfile.TemporaryDirectory(prefix="step-ap214-rule-") as directory:
                path = Path(directory) / "source.step"
                path.write_bytes(data)
                run = subprocess.run([os.environ["STEP_PLANAR_BINARY"], "--fixture-limits", "--quality", "Q-P0-mm-01", str(path)],
                                     capture_output=True, timeout=15)
                result = json.loads(run.stdout)
                self.assertEqual(2, run.returncode)
                self.assertEqual(source_status, result["source_semantics"]["status"])
                self.assertEqual("UnsupportedRuleProfile", result["diagnostics"][0]["key"])
                for stage in ("local_rules", "global_rules_47", "planar_regions", "conversion_units", "native_build"):
                    self.assertEqual("NotRun", result["checks"][stage])
                self.assertEqual([], result["rules"])
                self.assertEqual(0, result["work"])
                self.assertFalse(result["qualified_source_evidence"])
                self.assertIsNone(result["model"])
