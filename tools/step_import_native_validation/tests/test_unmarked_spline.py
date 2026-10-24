# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Unmarked spline regions retain source bounds and independently known geometry."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_import_native_validation.tests.test_general import CORPUS, bulged_box_source
from tools.step_import_native_validation.tests.test_spline_surface_projection import rational_surface_cylinder


def rational_plane_box():
    data = (CORPUS / "S203-P04/input.step").read_bytes()
    # S(u,v)=(2+20u/(1+u), -3+20v, 35). The image is exactly the
    # original top rectangle despite its nonaffine rational U parameter.
    surface = (b"(BOUNDED_SURFACE() B_SPLINE_SURFACE(1,1,((#104,#107),(#105,#106)),"
               b".UNSPECIFIED.,.F.,.F.,.F.) B_SPLINE_SURFACE_WITH_KNOTS((2,2),(2,2),(0.,1.),(0.,1.),"
               b".UNSPECIFIED.) GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_SURFACE(((1.,1.),(2.,2.))) "
               b"REPRESENTATION_ITEM('public rational plane') SURFACE())")
    return data.replace(b"PLANE('',#513)", surface).replace(b"'body',(#21),#12", b"'body',(#21,#513),#12")


def perforated_plane():
    data = (CORPUS / "S203-P02/input.step").read_bytes()
    # Rotated 12x8 rectangle with a 4x2 hole. The four existing corner
    # points define an affine tensor surface; physical area is 96-8=88.
    surface = (b"B_SPLINE_SURFACE_WITH_KNOTS('',1,1,((#100,#103),(#101,#102)),"
               b".UNSPECIFIED.,.F.,.F.,.F.,(2,2),(2,2),(0.,1.),(0.,1.),.UNSPECIFIED.)")
    return data.replace(b"PLANE('',#133)", surface).replace(b"'body',(#21),#12", b"'body',(#21,#133),#12")


class UnmarkedSplineTests(unittest.TestCase):
    def invoke(self, data):
        with tempfile.TemporaryDirectory(prefix="public-unmarked-spline-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                                  "Q-AP214-development-mm-01", str(path)], capture_output=True, timeout=30)
            self.assertEqual(data, path.read_bytes())
        self.assertTrue(run.stdout, run.stderr)
        return run, json.loads(run.stdout)

    def accepted(self, data, face, bound):
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])
        self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, report["validations"])
        self.assertTrue(any(row["source_face"] == face and row["source_bound"] == bound
                            for row in report["classified_outer_bounds"]))
        self.assertTrue(all(row["method"] == "NativeSplineCurveIntersectionContainment" and not row["source_rewritten"]
                            for row in report["classified_outer_bounds"] if row["source_face"] == face))
        self.assertTrue(any(row["kind"] == "NativeSplineCurveIntersectionContainment"
                            for row in report["representation_normalizations"]))
        return report

    def test_curved_polynomial_roof_keeps_independent_volume(self):
        data = bulged_box_source().replace(b"#516=FACE_OUTER_BOUND", b"#516=FACE_BOUND")
        report = self.accepted(data, "517", "516")
        self.assertAlmostEqual(6000 + 4000 / 9, report["model"]["material_volume"]["value_mm3"], delta=.02)

    def test_nonconstant_rational_weights_keep_rectangular_image(self):
        data = rational_plane_box().replace(b"#516=FACE_OUTER_BOUND", b"#516=FACE_BOUND")
        report = self.accepted(data, "517", "516")
        self.assertAlmostEqual(6000, report["model"]["material_volume"]["value_mm3"], delta=.02)
        self.assertAlmostEqual(2200, report["model"]["area_mm2"], delta=.02)

    def test_hole_first_in_source_set_keeps_open_surface_role(self):
        data = perforated_plane().replace(b"#301=FACE_OUTER_BOUND", b"#301=FACE_BOUND")
        for variant in (data, data.replace(b"(#301,#311)", b"(#311,#301)")):
            with self.subTest(hole_first=variant != data):
                report = self.accepted(variant, "302", "301")
                self.assertAlmostEqual(88, report["model"]["area_mm2"], delta=.001)
                self.assertEqual("NotApplicable", report["model"]["material_volume"])
                self.assertEqual(0, report["native_role"]["material_regions"])

    def test_unknown_source_closure_still_defers(self):
        data = bulged_box_source().replace(b"#516=FACE_OUTER_BOUND", b"#516=FACE_BOUND")
        data = data.replace(b".UNSPECIFIED.,.F.,.F.,.F.,(3,3)", b".UNSPECIFIED.,.U.,.F.,.F.,(3,3)")
        run, report = self.invoke(data)
        self.assertEqual(7, run.returncode)
        self.assertIsNone(report["model"])
        self.assertEqual("GeneralRegionClassificationPending", report["diagnostics"][-1]["key"])

    def test_wrong_hole_orientation_never_publishes(self):
        data = perforated_plane().replace(b"#301=FACE_OUTER_BOUND", b"#301=FACE_BOUND")
        data = data.replace(b"#311=FACE_BOUND('',#310,.T.)", b"#311=FACE_BOUND('',#310,.F.)")
        run, report = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["model"])

    def test_native_closed_chart_does_not_trust_false_source_flag(self):
        data = rational_surface_cylinder().replace(b"#172=FACE_OUTER_BOUND", b"#172=FACE_BOUND")
        self.assertEqual(1, data.count(b".UNSPECIFIED.,.T.,.F.,.F."))
        data = data.replace(b".UNSPECIFIED.,.T.,.F.,.F.", b".UNSPECIFIED.,.F.,.F.,.F.")
        run, report = self.invoke(data)
        self.assertEqual(7, run.returncode, report["diagnostics"])
        self.assertIsNone(report["model"])
        self.assertEqual("GeneralSplineRegionChartUnestablished", report["diagnostics"][-1]["key"])

    def test_copy_and_archive_keep_defining_geometry_and_uv(self):
        cli = Path(os.environ["STEP_NATIVE_BINARY"])
        binary = os.environ.get("STEP_NATIVE_GENERAL_BINARY", str(cli.with_name("step_import_general_tests" + cli.suffix)))
        variants = ((bulged_box_source(), 6000 + 4000 / 9), (rational_plane_box(), 6000))
        for data, volume in variants:
            with self.subTest(volume=volume), tempfile.TemporaryDirectory(prefix="public-unmarked-copy-") as directory:
                path, output = Path(directory) / "source.step", Path(directory) / "out"
                path.write_bytes(data.replace(b"#516=FACE_OUTER_BOUND", b"#516=FACE_BOUND"))
                run = subprocess.run([binary, "--development-file", str(path), str(output), "normal"],
                                     capture_output=True, timeout=30)
                self.assertEqual(0, run.returncode, run.stderr)
                checks = json.loads((output / "checks.json").read_bytes())
                self.assertEqual("Passed", checks["status"])
                self.assertTrue(checks["defining_coefficients_and_uv_checked"])
                self.assertTrue(checks["unmarked_spline_publication_checked"])
                for field in ("volume_mm3", "copied_volume_mm3", "reloaded_volume_mm3"):
                    self.assertAlmostEqual(volume, checks[field], delta=.02)
