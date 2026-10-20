# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Synthetic cylinder/frustum controls: source curves survive target seam normalization."""
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_import_native_validation.tests.test_general import conical_frustum_source, cylinder_source

FLAG = "--normalize-periodic-faces"
WORK = "Q-AP214-work-tolerance-study-mm-01"


def offset_seam(cone=False, delta=1e-5):
    # Analytic surfaces and ring radii remain exact. Only the seam endpoints
    # and its LINE origin have a known radial offset inside this explicit
    # study quality. Import must retain those bytes/vertices, never heal them.
    data = conical_frustum_source() if cone else cylinder_source()
    height, radius = (4, 6) if cone else (7, 3)
    for ident, y, z in ((102, 3, 0), (103, radius, height)):
        old = f"#{ident}=CARTESIAN_POINT('',(0.,{y}.,{z}.));".encode()
        new = f"#{ident}=CARTESIAN_POINT('',(0.,{y + delta:.17g},{z}.));".encode()
        assert data.count(old) == 1
        data = data.replace(old, new)
    return data


class PeriodicNormalizationTests(unittest.TestCase):
    def invoke(self, data, *flags, quality=WORK):
        with tempfile.TemporaryDirectory(prefix="step-periodic-") as folder:
            path = Path(folder) / "synthetic.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality", quality,
                                  str(path), *flags], capture_output=True, timeout=45)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_default_refusal_and_explicit_source_preservation(self):
        for cone in (False, True):
            with self.subTest(cone=cone):
                data = offset_seam(cone)
                old, before = self.invoke(data)
                self.assertEqual(9, old.returncode, old.stderr or old.stdout)
                self.assertEqual("GeneralLevel2Validation", before["diagnostics"][0]["key"])
                self.assertNotIn("periodic_normalization_requested", before)
                run, report = self.invoke(data, FLAG)
                self.assertEqual(0, run.returncode, run.stderr or run.stdout)
                self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])
                self.assertEqual("None", report["repair"])
                origins = report["topology_normalization"]
                for key in ("completed", "source_curve_intervals_covered", "source_vertices_preserved"):
                    self.assertTrue(origins[key])
                self.assertEqual("OriginalNativeBasis", origins["parameter_convention"])
                for kind in ("faces", "edges", "vertices"):
                    self.assertEqual(report["model"][kind], len(origins[kind]))
                    self.assertEqual(list(range(len(origins[kind]))), [x["target"] for x in origins[kind]])
                self.assertEqual({"130", "131", "132"}, {x["source_edge_id"] for x in origins["edges"]
                                                           if "source_edge_id" in x})
                self.assertEqual({"120", "121"}, {x["source_vertex_id"] for x in origins["vertices"]
                                                    if "source_vertex_id" in x})
                for face in origins["faces"]:
                    self.assertIn("original_native_domain", face)
                    self.assertLessEqual(face["native_surface_difference_bound_mm"], 1e-9)
                # Independent analytic truth, with the existing numerical screen;
                # this is not a continuous source-to-model accuracy certificate.
                area, volume = (90 * math.pi, 84 * math.pi) if cone else (60 * math.pi, 63 * math.pi)
                self.assertAlmostEqual(area, report["model"]["area_mm2"], delta=area * 2e-4)
                self.assertAlmostEqual(volume, report["model"]["material_volume"]["value_mm3"], delta=volume * 2e-4)

    def test_clean_request_does_not_invent_a_normalization(self):
        run, report = self.invoke(cylinder_source(), FLAG)
        self.assertEqual(0, run.returncode, run.stderr or run.stdout)
        self.assertTrue(report["periodic_normalization_requested"])
        self.assertIsNone(report["topology_normalization"])

    def test_strict_path_refuses_general_option(self):
        run, _ = self.invoke(cylinder_source(), FLAG, quality="Q-P0-mm-01")
        self.assertEqual(2, run.returncode)

    def test_original_gap_is_not_repaired_or_hidden(self):
        run, report = self.invoke(offset_seam(delta=.1), FLAG)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["model"])

    def test_budget_and_complete_report_are_still_required(self):
        run, report = self.invoke(offset_seam(), FLAG, "--max-native-calls", "1")
        self.assertEqual(3, run.returncode, run.stderr or run.stdout)
        self.assertEqual("ResourceLimit", report["status"])
        self.assertIsNone(report["model"])
        run, report = self.invoke(offset_seam(), FLAG, "--max-report-bytes", "1")
        self.assertEqual(5, run.returncode, run.stderr or run.stdout)
        self.assertIsNone(report)

    def test_query_copy_and_archive_keep_all_defining_data(self):
        cli = Path(os.environ["STEP_NATIVE_BINARY"])
        binary = os.environ.get("STEP_NATIVE_GENERAL_BINARY",
                                str(cli.with_name("step_import_general_tests" + cli.suffix)))
        for cone in (False, True):
            with self.subTest(cone=cone), tempfile.TemporaryDirectory(prefix="step-periodic-copy-") as folder:
                root = Path(folder)
                path, output = root / "synthetic.step", root / "out"
                path.write_bytes(offset_seam(cone))
                run = subprocess.run([binary, "--development-file", str(path), str(output), "normal",
                                      "--work-tolerance-study", FLAG], capture_output=True, timeout=60)
                self.assertEqual(0, run.returncode, run.stderr)
                checks = json.loads((output / "checks.json").read_bytes())
                self.assertEqual("Passed", checks["status"])
                self.assertTrue(checks["defining_coefficients_and_uv_checked"])
