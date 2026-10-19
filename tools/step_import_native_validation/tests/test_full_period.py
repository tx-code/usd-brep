# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent full-revolution controls with seams away from the axis directions."""
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_import_native_validation.tests.test_general import conical_frustum_source, cylinder_source


def seam_source(theta, *, implicit=False, cone=False):
    data = conical_frustum_source() if cone else cylinder_source(implicit_seam=implicit)
    height, top_radius = (4, 6) if cone else (7, 3)
    for ident, radius, z in ((102, 3, 0), (103, top_radius, height)):
        old = f"#{ident}=CARTESIAN_POINT('',(0.,{radius}.,{z}.));".encode()
        new = (f"#{ident}=CARTESIAN_POINT('',"
               f"({radius * math.cos(theta):.17g},{radius * math.sin(theta):.17g},{z}.));").encode()
        assert data.count(old) == 1
        data = data.replace(old, new)
    if cone:
        # Keep the exact 3-4-5 cone generatrix direction consistent with its
        # rotated endpoints. The source surface frame is still the global frame.
        data = data.replace(b"#114=DIRECTION('',(0.,3.,4.));",
                            f"#114=DIRECTION('',({3 * math.cos(theta):.17g},{3 * math.sin(theta):.17g},4.));".encode())
    return data


class FullPeriodTests(unittest.TestCase):
    def check_import(self, theta, *, implicit=False, cone=False):
        data = seam_source(theta, implicit=implicit, cone=cone)
        with tempfile.TemporaryDirectory(prefix="step-full-period-") as directory:
            path = Path(directory) / "synthetic.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                                  "Q-AP214-development-mm-01", str(path)], capture_output=True, timeout=15)
        self.assertEqual(0, run.returncode, run.stderr or run.stdout)
        report = json.loads(run.stdout)
        self.assertEqual("Accepted", report["status"])
        self.assertEqual("None", report["repair"])
        self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])
        self.assertEqual((3, 3, 2), tuple(report["model"][key] for key in ("faces", "edges", "vertices")))
        # Analytic cylinder R=3,H=7, or 3-4-5 frustum R=3/6,H=4.
        area, volume = (90 * math.pi, 84 * math.pi) if cone else (60 * math.pi, 63 * math.pi)
        self.assertAlmostEqual(area, report["model"]["area_mm2"], delta=area * 2e-4)
        self.assertAlmostEqual(volume, report["model"]["material_volume"]["value_mm3"], delta=volume * 2e-4)
        changes = report["representation_normalizations"]
        self.assertEqual(1 if implicit else 0, len(changes))
        return report

    def test_explicit_cylinder_seams_keep_a_constructible_full_period(self):
        for theta in (0.2, 4.5):
            with self.subTest(theta=theta):
                self.check_import(theta)

    def test_implicit_cylinder_seams_keep_the_source_rings_and_one_target_cut(self):
        for theta in (0.2, 4.5):
            with self.subTest(theta=theta):
                self.check_import(theta, implicit=True)

    def test_cone_seams_preserve_the_analytic_frustum(self):
        for theta in (0.2, 4.5):
            with self.subTest(theta=theta):
                self.check_import(theta, cone=True)

    def test_query_copy_coefficients_and_archive_survive_a_rotated_full_period(self):
        with tempfile.TemporaryDirectory(prefix="step-full-period-copy-") as directory:
            root = Path(directory)
            path, output = root / "synthetic.step", root / "out"
            path.write_bytes(seam_source(0.2, implicit=True))
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
                self.assertAlmostEqual(63 * math.pi, checks[field], delta=63 * math.pi * 2e-4)
