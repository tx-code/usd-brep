# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Real CLI transport of exact original/edge-bound source-3D choices."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


FIXTURE = Path(__file__).resolve().parents[3] / "source/STEP_IMPORT/tests/fixtures/source_pcurve_box.step"
USE = "7:21:203:920:202:PCURVE_S1:921"


class SurfaceCurveInterpretationTests(unittest.TestCase):
    def source(self, conflict=False):
        data = FIXTURE.read_bytes()
        if conflict:
            for old, new in ((b"(0.,0.)", b"(0.1,0.)"), (b"(0.,2.)", b"(0.1,2.)"), (b"(0.,10.)", b"(0.1,10.)")):
                self.assertEqual(1, data.count(old))
                data = data.replace(old, new)
        return data

    def plan(self, data, *, use=USE, digest=None, maximum=1):
        # Use-before-budget/hash proves parser transport does not infer scope
        # from ordering or from the selected filename. The core binds all fields.
        return ["--surface-curve-3d-use", use, "--max-surface-curve-3d-uses", str(maximum),
                "--surface-curve-3d-source-sha256", digest or hashlib.sha256(data).hexdigest()]

    def invoke(self, data, *args):
        with tempfile.TemporaryDirectory(prefix="public-source-curve-choice-") as folder:
            path = Path(folder) / "input.step"
            path.write_bytes(data)
            run = subprocess.run(
                [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality", "Q-AP214-development-mm-01",
                 "--integration-relative-accuracy", "1e-8", *args, str(path)],
                capture_output=True, timeout=40,
            )
            self.assertEqual(data, path.read_bytes())
        return run, json.loads(run.stdout) if run.stdout else None

    def test_explicit_cli_choice_preserves_original_and_conditional_qualification(self):
        for conflict in (False, True):
            with self.subTest(conflict=conflict):
                data = self.source(conflict)
                run, report = self.invoke(data, *self.plan(data))
                self.assertEqual(11, run.returncode, run.stderr or report)
                self.assertEqual("AcceptedUnderInterpretation", report["status"])
                self.assertEqual("None", report["repair"])
                self.assertAlmostEqual(6000, report["model"]["material_volume"]["value_mm3"], delta=.001)
                self.assertAlmostEqual(2200, report["model"]["area_mm2"], delta=.001)
                receipt = report["surface_curve_3d_interpretation"]
                self.assertEqual(hashlib.sha256(data).hexdigest(), receipt["source_sha256"])
                self.assertTrue(receipt["request_complete"])
                self.assertFalse(receipt["source_definitions_rewritten"])
                self.assertFalse(receipt["source_priority_declaration_rewritten"])
                self.assertFalse(receipt["source_pcurve_consistency_qualified"])
                self.assertFalse(receipt["strict_original_geometry_qualified"])
                self.assertEqual(1, receipt["maximum_uses"])
                self.assertEqual(1, len(receipt["plans"]))
                self.assertEqual(["921"], receipt["plans"][0]["source_pcurves"])
                self.assertEqual("PCURVE_S1", receipt["plans"][0]["original_master"])
                action = receipt["actions"][0]
                self.assertTrue(action["native_checks_passed"])
                self.assertEqual([0, 1], action["native_edge_interval"])
                self.assertEqual(conflict, bool(action["original_conflicts"]))
                self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])

    def test_default_still_refuses_source_conflict_without_any_plan(self):
        run, report = self.invoke(self.source(True))
        self.assertEqual(9, run.returncode)
        self.assertIsNone(report["model"])
        self.assertNotIn("surface_curve_3d_interpretation", report)

    def test_original_binding_and_unused_or_wrong_entity_scope_refuse(self):
        data = self.source(True)
        wrong = "0" * 64
        for args in (self.plan(data, digest=wrong), self.plan(data, use=USE.replace(":202:", ":999:"))):
            with self.subTest(args=args):
                run, report = self.invoke(data, *args)
                self.assertEqual(2, run.returncode)
                self.assertIsNone(report["model"])
                self.assertFalse(report["surface_curve_3d_interpretation"]["request_complete"])
        consistent = self.source()
        run, report = self.invoke(consistent, *self.plan(consistent, use=USE.replace(":203:", ":999:")))
        self.assertEqual(2, run.returncode)
        self.assertIsNone(report["model"])
        self.assertFalse(report["surface_curve_3d_interpretation"]["request_complete"])

    def test_two_occurrences_and_s2_are_preserved_in_cli_order(self):
        data = self.source(True).replace(b"(#921),.PCURVE_S1.", b"(#921,#921),.PCURVE_S2.")
        use = USE.replace("PCURVE_S1:921", "PCURVE_S2:921:921")
        run, report = self.invoke(data, *self.plan(data, use=use))
        self.assertEqual(11, run.returncode, run.stderr or report)
        receipt = report["surface_curve_3d_interpretation"]
        self.assertEqual(["921", "921"], receipt["plans"][0]["source_pcurves"])
        self.assertEqual({1, 2}, {item["association_occurrence"] for item in receipt["actions"][0]["original_conflicts"]})

    def test_argument_errors_never_capture_or_publish_a_model(self):
        data = self.source()
        digest = hashlib.sha256(data).hexdigest()
        invalid = [
            ["--surface-curve-3d-use", USE],  # Missing original identity and explicit budget.
            self.plan(data, maximum=0),
            self.plan(data, use=USE + ":922:923"),
            self.plan(data, use=USE.replace("PCURVE_S1", "CURVE_3D")),
            self.plan(data, use=USE.replace("203", "")),
            self.plan(data) + ["--max-surface-curve-3d-uses", "1"],
            self.plan(data) + ["--surface-curve-3d-source-sha256", digest],
            self.plan(data) + ["--surface-curve-3d-use", USE],  # Duplicate exact use.
        ]
        for args in invalid:
            with self.subTest(args=args):
                run, report = self.invoke(data, *args)
                self.assertEqual(2, run.returncode)
                self.assertIsNone(report)

    def test_exact_report_budget_keeps_mandatory_choice_evidence(self):
        data = self.source(True)
        args = self.plan(data)
        run, report = self.invoke(data, *args)
        self.assertEqual(11, run.returncode)
        # CLI intentionally applies one explicit cap to source and outer JSON.
        # The source cap is itself reported, so first stabilize its digit count;
        # this is a formatting fixed point, with no geometry/policy retry.
        budget = len(run.stdout)
        for _ in range(4):
            exact, same = self.invoke(data, *args, "--max-report-bytes", str(budget))
            self.assertEqual(11, exact.returncode, exact.stderr or same)
            if len(exact.stdout) == budget:
                break
            budget = len(exact.stdout)
        self.assertEqual(budget, len(exact.stdout))
        repeated, repeated_report = self.invoke(data, *args, "--max-report-bytes", str(budget))
        self.assertEqual(11, repeated.returncode, repeated.stderr or repeated_report)
        self.assertEqual(11, exact.returncode, exact.stderr or same)
        self.assertEqual(repeated.stdout, exact.stdout)
        limited, result = self.invoke(data, *args, "--max-report-bytes", str(budget - 1))
        self.assertEqual(3, limited.returncode)
        self.assertTrue(result is None or result["model"] is None)

    def test_terminal_domain_and_source_priority_requests_keep_independent_receipts(self):
        from tools.step_import_native_validation.tests.test_terminal_domain import source
        data = source()
        digest = hashlib.sha256(data).hexdigest()
        args = self.plan(data, use="7:21:132:960:112:PCURVE_S1:961")
        args += ["--terminal-domain-source-sha256", digest, "--terminal-domain-surface", "142:-:-:-2:8",
                 "--terminal-domain-pcurve", "142:961", "--max-terminal-domain-surfaces", "1",
                 "--max-terminal-domain-proof-controls", "4096", "--max-terminal-domain-proof-work", "1000000"]
        run, report = self.invoke(data, *args)
        self.assertEqual(11, run.returncode, run.stderr or report)
        self.assertTrue(report["source_domain_interpretation"]["request_complete"])
        self.assertTrue(report["surface_curve_3d_interpretation"]["request_complete"])
        self.assertEqual(digest, report["source_domain_interpretation"]["source_sha256"])
        self.assertEqual(digest, report["surface_curve_3d_interpretation"]["source_sha256"])
        self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])

    def test_same_source_request_traverses_the_explicit_assembly_entry(self):
        path = FIXTURE.parents[3] / "STEP_SEMANTICS/tests/fixtures/assembly_repeated_box.step"
        data = path.read_bytes().replace(b"#203=EDGE_CURVE('',#110,#111,#202,.T.);",
                                         b"#203=EDGE_CURVE('',#110,#111,#20020,.T.);")
        extra = b"""#20020=SURFACE_CURVE('',#202,(#20021),.PCURVE_S1.);
#20021=PCURVE('',#504,#20022);
#20022=DEFINITIONAL_REPRESENTATION('',(#20023),#20030);
#20023=B_SPLINE_CURVE_WITH_KNOTS('',1,(#20024,#20028),.UNSPECIFIED.,.F.,.F.,(2,2),(0.,1.),.UNSPECIFIED.);
#20024=CARTESIAN_POINT('',(0.1,0.));
#20028=CARTESIAN_POINT('',(0.1,10.));
#20030=(GEOMETRIC_REPRESENTATION_CONTEXT(2) PARAMETRIC_REPRESENTATION_CONTEXT() REPRESENTATION_CONTEXT('','2D'));
"""
        end = data.rindex(b"ENDSEC;")
        data = data[:end] + extra + data[end:]
        run, report = self.invoke(data, *self.plan(data, use="7:21:203:20020:202:PCURVE_S1:20021"), "--assembly", "--max-assembly-usages", "100",
                                  "--max-assembly-instances", "100", "--max-assembly-depth", "16")
        self.assertEqual(11, run.returncode, run.stderr or (report or {}).get("diagnostics"))
        self.assertEqual("AcceptedUnderInterpretation", report["status"])
        self.assertEqual(2, len(report["assembly"]["definitions"]))
        self.assertTrue(report["surface_curve_3d_interpretation"]["request_complete"])
        self.assertIsNone(report["model"])


if __name__ == "__main__":
    unittest.main()
