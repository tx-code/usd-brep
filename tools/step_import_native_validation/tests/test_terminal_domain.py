# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit CLI interpretation of a public analytically defined cylinder."""
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


def source():
    path = Path(__file__).resolve().parents[3] / "source/STEP_IMPORT/tests/fixtures/source_pcurve_revolution.step"
    data = path.read_bytes()
    replacements = (
        (b"B_SPLINE_CURVE(1,(#102,#103)", b"B_SPLINE_CURVE(1,(#102,#943)"),
        (b"(-2.,8.),.UNSPECIFIED.)", b"(-2.,7.),.UNSPECIFIED.)"),
        (b"RATIONAL_B_SPLINE_CURVE((1.,2.))", b"RATIONAL_B_SPLINE_CURVE((1.,1.))"),
        (b"#960=SURFACE_CURVE", b"#943=CARTESIAN_POINT('',(0.,3.,6.3));\n#960=SURFACE_CURVE"),
    )
    for old, new in replacements:
        assert data.count(old) == 1
        data = data.replace(old, new)
    # Original generator z=.7*(v+2) on [-2,7]; the explicit terminal formula
    # at the preserved pcurve endpoint v=8 gives z=7. Radius remains 3.
    return data


class TerminalDomainTests(unittest.TestCase):
    def invoke(self, data, *, enabled=True, hash_override=None, work=1000000):
        args = [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality", "Q-AP214-development-mm-01"]
        if enabled:
            args += ["--terminal-domain-source-sha256", hash_override or hashlib.sha256(data).hexdigest(),
                     "--terminal-domain-pcurve", "142:961",  # Test order-independent plan transport.
                     "--terminal-domain-surface", "142:-:-:-2:8", "--max-terminal-domain-surfaces", "1",
                     "--max-terminal-domain-proof-controls", "4096", "--max-terminal-domain-proof-work", str(work)]
        with tempfile.TemporaryDirectory(prefix="public-terminal-domain-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([*args, str(path)], capture_output=True, timeout=30)
            self.assertEqual(data, path.read_bytes())
        self.assertTrue(run.stdout, run.stderr)
        return run, json.loads(run.stdout)

    def test_explicit_cli_plan_retains_source_and_separate_qualification(self):
        data = source()
        ordinary, before = self.invoke(data, enabled=False)
        self.assertEqual(8, ordinary.returncode)
        self.assertEqual("Indeterminate", before["status"])
        self.assertIsNone(before["model"])
        self.assertNotIn("source_domain_interpretation", before)
        run, result = self.invoke(data)
        self.assertEqual(11, run.returncode, run.stderr or result)
        self.assertEqual("AcceptedUnderInterpretation", result["status"])
        self.assertEqual(data.decode(), result["source_inspection"]["source"]["input"]["raw"])
        receipt = result["source_domain_interpretation"]
        self.assertTrue(receipt["request_complete"])
        self.assertFalse(receipt["strict_original_geometry_qualified"])
        self.assertFalse(receipt["source_definitions_rewritten"])
        self.assertFalse(receipt["source_uv_rewritten"])
        self.assertTrue(receipt["actions"])
        for action in receipt["actions"]:
            self.assertEqual("GeneralPcurveBasisDomainUnestablished", action["original_domain_diagnostic"])
            self.assertLessEqual(action["whole_new_domain_error_upper_mm"], 1e-9)
        self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"], delta=.001)
        self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, result["validations"])

    def test_wrong_source_identity_refuses_before_native_work(self):
        run, result = self.invoke(source(), hash_override="0" * 64)
        self.assertEqual(2, run.returncode)
        self.assertIsNone(result["model"])
        self.assertEqual(0, result["native_calls"])
        self.assertIn("TerminalDomainSourceMismatch", {row["key"] for row in result["diagnostics"]})

    def test_cumulative_work_refusal_never_publishes(self):
        run, result = self.invoke(source(), work=1)
        self.assertEqual(3, run.returncode)
        self.assertEqual("ResourceLimit", result["status"])
        self.assertIsNone(result["model"])
        self.assertFalse(result["source_domain_interpretation"]["request_complete"])
