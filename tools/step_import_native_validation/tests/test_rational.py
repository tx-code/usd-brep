# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""A rationally parameterized box has independent exact physical geometry."""

import json, os, subprocess, tempfile, unittest
from pathlib import Path
from tools.step_semantic_validation.tests.test_rational import rational_box


class RationalNativeTests(unittest.TestCase):
    def invoke(self, data):
        with tempfile.TemporaryDirectory(prefix="step-rational-native-") as directory:
            path = Path(directory) / "box.step"
            path.write_bytes(data)
            run = subprocess.run(
                [
                    os.environ["STEP_NATIVE_BINARY"],
                    "--fixture-limits",
                    "--quality",
                    "Q-AP214-development-mm-01",
                    str(path),
                ],
                capture_output=True,
                timeout=30,
            )
        return run, json.loads(run.stdout) if run.stdout else None

    def test_weighted_line_and_surface_keep_the_analytic_box(self):
        for curve, surface in [(True, False), (False, True), (True, True)]:
            with self.subTest(curve=curve, surface=surface):
                data = rational_box(curve, surface)
                run, r = self.invoke(data)
                self.assertEqual(0, run.returncode, r["diagnostics"])
                self.assertEqual((6, 12, 8), tuple(r["model"][k] for k in ["faces", "edges", "vertices"]))
                self.assertAlmostEqual(6000, r["model"]["material_volume"]["value_mm3"], delta=1.2)
                self.assertAlmostEqual(2200, r["model"]["area_mm2"], delta=0.44)
                self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, r["validations"])
                self.assertEqual(data.decode("ascii"), r["source_inspection"]["source"]["input"]["raw"])

    def test_unrepresentable_source_weights_do_not_become_default_weights(self):
        data = rational_box(curve=True, surface=False).replace(b"(2.,5.)", b"(2.,1.0E-9999)")
        run, r = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(r["model"])
        self.assertTrue(r["candidate_discarded"])

    def test_kernel_near_unity_weight_simplification_is_not_silently_accepted(self):
        data = rational_box(curve=False, surface=True, surface_weights="((1.,1.),(1.,1.0000000000000002))")
        run, r = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(r["model"])
        self.assertEqual("GeneralSourceWeightsChanged", r["diagnostics"][-1]["key"])
