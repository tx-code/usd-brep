# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Numerical stopping controls have independent mathematical volume truth."""
from fractions import Fraction
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_import_native_validation.tests.test_general import CORPUS


def bernstein_roof_source():
    """Exact affine x/y grid and one interior degree-ten height coefficient."""
    data = (CORPUS / "S203-P04/input.step").read_bytes()
    points = []
    rows = []
    for i in range(11):
        row = []
        for j in range(11):
            identity = 600 + 11 * i + j
            row.append(f"#{identity}")
            points.append(f"#{identity}=CARTESIAN_POINT('',({2 + i}.,{-3 + 2 * j}.,"
                          f"{36 if i == j == 1 else 35}.));\n")
        rows.append("(" + ",".join(row) + ")")
    surface = ("B_SPLINE_SURFACE_WITH_KNOTS('Bernstein roof',10,10,(" + ",".join(rows) + "),"
               ".UNSPECIFIED.,.F.,.F.,.F.,(11,11),(11,11),(0.,1.),(0.,1.),.UNSPECIFIED.)")
    assert data.count(b"PLANE('',#513)") == 1
    data = data.replace(b"PLANE('',#513)", surface.encode("ascii"))
    data = data.replace(b"'body',(#21),#12", b"'body',(#21,#513),#12")
    return data.replace(b"ENDSEC;\nEND-ISO", "".join(points).encode("ascii") + b"ENDSEC;\nEND-ISO")


class NumericalIntegrationTests(unittest.TestCase):
    def invoke(self, data, *flags, general=True):
        with tempfile.TemporaryDirectory(prefix="step-integration-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            args = [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits"]
            if general:
                args += ["--quality", "Q-AP214-development-mm-01"]
            else:
                args += ["--quality", "Q-P0-mm-01"]
            run = subprocess.run([*args, str(path), *map(str, flags)], capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_tighter_control_meets_independent_bernstein_volume(self):
        data = bernstein_roof_source()
        run, result = self.invoke(data, "--integration-relative-accuracy", "1e-7")
        self.assertEqual(0, run.returncode, run.stderr or result)
        # x=2+10u, y=-3+20v and z=35+B_1^10(u)B_1^10(v).
        # All four roof boundaries stay at z=35. Since integral(B_i^p)=1/(p+1),
        # Fubini gives V=6000+200/121 exactly. No CAD output defines this truth.
        truth = Fraction(6000) + Fraction(200, 121)
        actual = result["model"]["material_volume"]["value_mm3"]
        self.assertLessEqual(abs(actual - float(truth)), float(truth) * 1e-7)
        observation = result["numerical_integration"]
        self.assertEqual(1e-7, observation["requested_relative_accuracy"])
        self.assertEqual(1e-7, observation["applied_relative_accuracy"])
        self.assertEqual(1e-7, result["measurement_policy"]["native_requested_relative_accuracy"])
        self.assertTrue(observation["completed"])
        self.assertFalse(observation["continuous_error_bound_established"])
        self.assertEqual("DimensionlessNumericalStoppingOnly", observation["control_scope"])
        self.assertEqual(data.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])
        self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, result["validations"])

    def test_explicit_legacy_control_preserves_default_result(self):
        data = (CORPUS / "S203-P04/input.step").read_bytes()
        before, baseline = self.invoke(data)
        after, explicit = self.invoke(data, "--integration-relative-accuracy", "1e-4")
        self.assertEqual((0, 0), (before.returncode, after.returncode))
        self.assertNotIn("numerical_integration", baseline)
        self.assertTrue(explicit.pop("numerical_integration")["completed"])
        self.assertEqual(baseline, explicit)

    def test_invalid_stopping_controls_are_rejected(self):
        data = (CORPUS / "S203-P04/input.step").read_bytes()
        for value in ("nan", "inf", "-0.1", "0", "1e-9", "1e-3", "1e100", "1e-6garbage"):
            with self.subTest(value=value):
                run, result = self.invoke(data, "--integration-relative-accuracy", value)
                self.assertNotEqual(0, run.returncode)
                self.assertIsNone(result)
        for value in ("1e-8", "1e-4"):
            with self.subTest(boundary=value):
                run, result = self.invoke(data, "--integration-relative-accuracy", value)
                self.assertEqual(0, run.returncode, run.stderr or result)

    def test_planar_proof_path_rejects_numerical_control(self):
        data = (CORPUS / "S203-P04/input.step").read_bytes()
        run, result = self.invoke(data, "--integration-relative-accuracy", "1e-7", general=False)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(result)
        self.assertIn(b"general", run.stderr)

    def test_source_failure_retains_request_without_claiming_execution(self):
        run, result = self.invoke(b"not STEP", "--integration-relative-accuracy", "1e-7")
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(result["model"])
        observation = result["numerical_integration"]
        self.assertEqual(1e-7, observation["requested_relative_accuracy"])
        self.assertEqual(1e-7, result["measurement_policy"]["native_requested_relative_accuracy"])
        self.assertIsNone(observation["applied_relative_accuracy"])
        self.assertFalse(observation["completed"])

    def test_missing_mandatory_report_prevents_delivery(self):
        data = (CORPUS / "S203-P04/input.step").read_bytes()
        run, result = self.invoke(data, "--integration-relative-accuracy", "1e-7", "--max-report-bytes", "64")
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(result)


if __name__ == "__main__":
    unittest.main()
