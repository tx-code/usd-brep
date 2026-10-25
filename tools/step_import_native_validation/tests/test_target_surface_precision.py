# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit original/face-bound calculation resolution through the real CLI."""
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


FIXTURE = (Path(__file__).resolve().parents[3]
           / "source/STEP_IMPORT/tests/fixtures/source_pcurve_revolution_near_pole_patch.step")
QUALITY = "Q-AP214-pcurve-carrier-precision-study-mm-01"
FACE = "7:21:182:142"
RESOLUTION = "1:100000000000"
RECEIPT = "target_surface_precision"


def summary(run, report):
    """Keep failures readable without repeating the complete source/report."""
    return {"exit": run.returncode, "stderr": run.stderr.decode(errors="replace")[-2000:],
            "status": report.get("status") if report else None,
            "keys": [row["key"] for row in report.get("diagnostics", [])] if report else []}


class TargetSurfacePrecisionTests(unittest.TestCase):
    def source(self):
        # A public conical patch whose full generator ends at radius 1e-6 mm.
        # No private source, parameter, filename or identifying inventory is used.
        return FIXTURE.read_bytes()

    def plan(self, data, *, face=FACE, digest=None, maximum="1", resolution=RESOLUTION):
        # Noncanonical argument order proves that every field is transported
        # explicitly rather than inferred from a filename or earlier argument.
        return ["--target-surface-work-face", face, "--target-surface-work-resolution", resolution,
                "--max-target-surface-work-faces", maximum,
                "--target-surface-work-source-sha256", digest or hashlib.sha256(data).hexdigest()]

    def invoke(self, data, *arguments, quality=QUALITY):
        with tempfile.TemporaryDirectory(prefix="public-target-surface-work-") as folder:
            path = Path(folder) / "input.step"
            path.write_bytes(data)
            run = subprocess.run(
                [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality", quality,
                 *arguments, str(path)], capture_output=True, timeout=120,
            )
            self.assertEqual(data, path.read_bytes())
        return run, json.loads(run.stdout) if run.stdout else None

    def assert_receipt(self, data, report):
        receipt = report[RECEIPT]
        self.assertEqual(hashlib.sha256(data).hexdigest(), receipt["source_sha256"])
        self.assertEqual("ExplicitTargetSurfaceWorkResolution", receipt["interpretation"])
        self.assertEqual(QUALITY, receipt["quality_id"])
        self.assertTrue(receipt["request_complete"])
        self.assertTrue(receipt["geometry_calculation_resolution_only"])
        for key in ("source_definitions_rewritten", "source_uv_rewritten", "topology_tolerance_changed",
                    "physical_budgets_changed", "strict_original_geometry_qualified", "surface_regularity_qualified",
                    "continuous_error_bound_established"):
            self.assertIs(False, receipt[key], key)
        self.assertEqual(1, receipt["maximum_faces"])
        self.assertEqual([1, 100000000000], receipt["work_resolution_mm"])
        self.assertEqual([dict(source_definition="7", source_shape="21", source_face="182", source_surface="142")],
                         receipt["plans"])
        self.assertEqual(1, len(receipt["actions"]))
        action = receipt["actions"][0]
        self.assertEqual(0, action["plan_index"])
        self.assertEqual(0, action["target_face_index"])
        self.assertEqual(1e-11, action["requested_mm"])
        self.assertGreater(action["effective_mm"], 0)
        self.assertLessEqual(action["effective_mm"], action["requested_mm"])
        self.assertTrue(math.isfinite(action["topology_face_tolerance_mm"]))
        self.assertGreaterEqual(action["topology_face_tolerance_mm"], 0)
        self.assertTrue(action["native_checks_passed"])
        return receipt

    def test_explicit_face_resolution_retains_source_budgets_and_all_native_checks(self):
        data = self.source()
        ordinary, before = self.invoke(data)
        self.assertEqual(7, ordinary.returncode, summary(ordinary, before))
        self.assertIsNone(before["model"])
        self.assertNotIn(RECEIPT, before)
        run, report = self.invoke(data, *self.plan(data))
        self.assertEqual(11, run.returncode, summary(run, report))
        self.assertEqual("AcceptedUnderInterpretation", report["status"])
        self.assertEqual("None", report["repair"])
        self.assertIsNotNone(report["model"])
        self.assertEqual(before["quality"], report["quality"])
        self.assert_receipt(data, report)
        self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, report["validations"])
        self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])

    def test_wrong_original_and_face_binding_do_not_publish(self):
        data = self.source()
        for arguments, diagnostic in (
            (self.plan(data, digest="0" * 64), "TargetSurfacePrecisionSourceMismatch"),
            (self.plan(data, face="7:21:182:999"), "TargetSurfacePrecisionBindingMismatch"),
        ):
            with self.subTest(diagnostic=diagnostic):
                run, report = self.invoke(data, *arguments)
                self.assertEqual(2, run.returncode, summary(run, report))
                self.assertIsNone(report["model"])
                self.assertFalse(report[RECEIPT]["request_complete"])
                self.assertIn(diagnostic, {row["key"] for row in report["diagnostics"]})

    def test_precision_does_not_waive_an_independent_near_tip_seam_failure(self):
        exact = FIXTURE.with_name("source_pcurve_revolution_pole_driven_patch.step").read_bytes()
        begin = exact.index(b"#945=CARTESIAN_POINT")
        end = exact.index(b";", begin) + 1
        data = exact[:begin] + b"#945=CARTESIAN_POINT('',(0.,0.000001,3.));" + exact[end:]
        run, report = self.invoke(data, *self.plan(data))
        self.assertEqual(9, run.returncode, summary(run, report))
        self.assertIsNone(report["model"])
        self.assertFalse(report[RECEIPT]["request_complete"])
        self.assertIn("GeneralRegionSeamUnestablished", {row["key"] for row in report["diagnostics"]})

    def test_all_fields_are_required_without_inferred_defaults(self):
        data = self.source()
        complete = self.plan(data)
        # Remove each distinct option/value pair, including the face plan.
        for begin in range(0, len(complete), 2):
            with self.subTest(omitted=complete[begin]):
                run, report = self.invoke(data, *(complete[:begin] + complete[begin + 2:]))
                self.assertEqual(2, run.returncode)
                self.assertIsNone(report)

    def test_malformed_duplicate_and_out_of_capacity_arguments_refuse_before_capture(self):
        data = self.source()
        invalid = [
            self.plan(data, maximum="0"), self.plan(data, maximum="-1"),
            self.plan(data, maximum="184467440737095516160"),
            self.plan(data, face="7:21:182"), self.plan(data, face=FACE + ":999"),
            self.plan(data, face="7:21::142"), self.plan(data, face="7:" + "2" * 513 + ":182:142"),
        ]
        for resolution in ("0:1", "1:0", "-1:1", "1:-1", "1e-11:1", "0.00001:1", "1", "1:2:3", "1:",
                           "+1:2", "9223372036854775808:1", "1:9223372036854775808"):
            invalid.append(self.plan(data, resolution=resolution))
        for flag, value in (("--target-surface-work-source-sha256", hashlib.sha256(data).hexdigest()),
                            ("--max-target-surface-work-faces", "1"),
                            ("--target-surface-work-resolution", RESOLUTION)):
            invalid.append(self.plan(data) + [flag, value])
        for arguments in invalid:
            with self.subTest(arguments=arguments):
                run, report = self.invoke(data, *arguments)
                self.assertEqual(2, run.returncode)
                self.assertIsNone(report)

    def test_unsupported_option_combinations_cannot_turn_into_a_precision_retry(self):
        data = self.source()
        digest = hashlib.sha256(data).hexdigest()
        combinations = [
            ["--normalize-periodic-faces"], ["--restrict-unused-support-domains"],
            ["--selection-source-sha256", digest, "--select-definition", "7"],
            ["--repair-source-sha256", digest, "--max-face-orientation-edits", "1",
             "--max-repair-proof-controls", "16", "--max-repair-proof-products", "16",
             "--repair-face-orientation", "7:21:182:T:F"],
        ]
        for extra in combinations:
            with self.subTest(extra=extra):
                run, report = self.invoke(data, *self.plan(data), *extra)
                self.assertEqual(2, run.returncode, summary(run, report))
                self.assertIsNone(report)
        run, report = self.invoke(data, *self.plan(data), quality="Q-P0-mm-01")
        self.assertEqual(2, run.returncode, summary(run, report))
        self.assertIsNone(report)

    def test_document_report_references_one_complete_root_receipt(self):
        data = self.source()
        run, report = self.invoke(data, *self.plan(data), "--independent-products")
        self.assertEqual(11, run.returncode, summary(run, report))
        self.assert_receipt(data, report)
        references = []

        def visit(value):
            if isinstance(value, dict):
                if RECEIPT in value and value is not report:
                    references.append(value[RECEIPT])
                for item in value.values():
                    visit(item)
            elif isinstance(value, list):
                for item in value:
                    visit(item)

        visit(report)
        self.assertTrue(references)
        self.assertTrue(all(reference == {"source_sha256": hashlib.sha256(data).hexdigest(),
                                         "receipt_scope": "RequestRoot"} for reference in references))

    def test_exact_mandatory_report_budget_does_not_drop_precision_receipt(self):
        data = self.source()
        arguments = self.plan(data)
        run, report = self.invoke(data, *arguments)
        self.assertEqual(11, run.returncode, summary(run, report))
        budget = len(run.stdout)
        # Stabilize the digits of the reported cap, not geometry or policy.
        for _ in range(4):
            exact, report = self.invoke(data, *arguments, "--max-report-bytes", str(budget))
            self.assertEqual(11, exact.returncode, summary(exact, report))
            if len(exact.stdout) == budget:
                break
            budget = len(exact.stdout)
        self.assertEqual(budget, len(exact.stdout))
        self.assert_receipt(data, report)
        repeated, repeated_report = self.invoke(data, *arguments, "--max-report-bytes", str(budget))
        self.assertEqual(11, repeated.returncode, summary(repeated, repeated_report))
        self.assertEqual(exact.stdout, repeated.stdout)
        limited, failure = self.invoke(data, *arguments, "--max-report-bytes", str(budget - 1))
        self.assertEqual(3, limited.returncode)
        self.assertTrue(failure is None or failure["model"] is None)


if __name__ == "__main__":
    unittest.main()
