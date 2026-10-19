# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Unused kink in a support surface: independent exact box truth."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

CORPUS = (Path(__file__).resolve().parents[2]
          / "step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P04/input.step")
FLAG = "--restrict-unused-support-domains"
NORMAL = "Q-AP214-development-mm-01"
WORK = "Q-AP214-work-tolerance-study-mm-01"


def support_box():
    data = CORPUS.read_bytes()
    source = b"PLANE('',#513)"
    replacement = (
        b"B_SPLINE_SURFACE_WITH_KNOTS('unused-support-kink',1,1,"
        b"((#600,#601),(#602,#603),(#604,#605)),.UNSPECIFIED.,.F.,.F.,.F.,"
        b"(2,1,2),(2,2),(-1.,1.5,2.),(-1.,2.),.UNSPECIFIED.)"
    )
    assert data.count(source) == 1
    data = data.replace(source, replacement).replace(b"'body',(#21),#12", b"'body',(#21,#513),#12")
    controls = b"""#600=CARTESIAN_POINT('',(-8.,-23.,35.));
#601=CARTESIAN_POINT('',(-8.,37.,35.));
#602=CARTESIAN_POINT('',(17.,-23.,35.));
#603=CARTESIAN_POINT('',(17.,37.,35.));
#604=CARTESIAN_POINT('',(22.,-23.,40.));
#605=CARTESIAN_POINT('',(22.,37.,40.));
"""
    ending = b"ENDSEC;\r\nEND-ISO" if b"ENDSEC;\r\nEND-ISO" in data else b"ENDSEC;\nEND-ISO"
    return data.replace(ending, controls + ending)


class SupportDomainTests(unittest.TestCase):
    def invoke(self, data, *flags, quality=NORMAL):
        with tempfile.TemporaryDirectory(prefix="step-support-domain-") as folder:
            path = Path(folder) / "synthetic.step"
            path.write_bytes(data)
            run = subprocess.run(
                [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality", quality, str(path), *flags],
                capture_output=True, timeout=45,
            )
        return run, json.loads(run.stdout) if run.stdout else None

    def test_default_keeps_original_refusal_and_opt_in_retains_exact_box(self):
        data = support_box()
        old, report = self.invoke(data)
        self.assertEqual(9, old.returncode, old.stderr or old.stdout)
        self.assertEqual("GeneralLevel2Validation", report["diagnostics"][0]["key"])
        self.assertNotIn("support_domain_restriction_requested", report)
        new, result = self.invoke(data, FLAG)
        self.assertEqual(0, new.returncode, new.stderr or new.stdout)
        self.assertEqual("Accepted", result["status"])
        self.assertTrue(result["support_domain_restriction_requested"])
        model = result["model"]
        self.assertEqual((6, 12, 8), (model["faces"], model["edges"], model["vertices"]))
        self.assertAlmostEqual(6000, model["material_volume"]["value_mm3"], delta=1e-4)
        self.assertAlmostEqual(2200, model["area_mm2"], delta=1e-4)
        rows = [r for r in result["representation_normalizations"]
                if r["kind"] == "UnusedSupportDomainRestriction"]
        self.assertEqual(1, len(rows))
        sources = result["source_inspection"]["source"]["records"]
        original = next(r for r in sources if r["id"] == "514")
        self.assertIn("(-1.,1.5,2.)", original["raw"])
        point = next(r for r in sources if r["id"] == "604")
        self.assertIn("(22.,-23.,40.)", point["raw"])
        bounds = [r for r in result["measurements"] if r["kind"] == "ContinuousNativeSurfaceDifferenceUpper"]
        self.assertEqual(1, len(bounds))
        self.assertLessEqual(bounds[0]["value_mm"], 1e-9)

    def test_quality_never_implicitly_requests_restriction(self):
        run, report = self.invoke(support_box(), quality=WORK)
        self.assertEqual(9, run.returncode, run.stderr or run.stdout)
        self.assertNotIn("support_domain_restriction_requested", report)
        quality = report["quality"]
        self.assertEqual({"kind": "rational", "num": "1", "den": "100"}, quality["consistency_mm"])
        self.assertEqual({"kind": "rational", "num": "1", "den": "50"}, quality["maximum_work_tolerance_mm"])
        self.assertEqual({"kind": "rational", "num": "1", "den": "100"}, quality["tolerance_growth_mm"])

    def test_strict_planar_path_refuses_option(self):
        run, _ = self.invoke(CORPUS.read_bytes(), FLAG, quality="Q-P0-mm-01")
        self.assertEqual(2, run.returncode)

    def test_budget_still_blocks_publication(self):
        run, report = self.invoke(support_box(), FLAG, "--max-native-calls", "1")
        self.assertEqual(3, run.returncode, run.stderr or run.stdout)
        self.assertIsNone(report["model"])
        self.assertEqual("ResourceLimit", report["status"])

    def test_complete_report_is_required_after_successful_normalization(self):
        run, report = self.invoke(support_box(), FLAG, "--max-report-bytes", "1")
        self.assertEqual(5, run.returncode, run.stderr or run.stdout)
        self.assertIn(b"Mandatory import report was not published", run.stderr)
        self.assertIsNone(report)
