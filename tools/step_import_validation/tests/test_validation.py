# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Contract, independent-truth and corruption tests for the standalone T01 tool."""

from __future__ import annotations

from fractions import Fraction as F
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.step_import_validation import controls, corpus, evidence
from tools.step_import_validation.common import InvalidEvidence, inside, load_json, parse_json, write_json
from tools.step_import_validation.oracle import decode, exact, orphan_ids, sqrt_interval
from tools.step_import_validation.records import by_id, inventory, nesting_depth

REPO = Path(__file__).resolve().parents[3]


class ValidationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="step-ticket01-")
        cls.root = Path(cls.temporary.name).resolve()
        assert cls.root.parent == Path(tempfile.gettempdir()).resolve()
        cls.frozen = cls.root / "corpus-a"
        cls.reproduced = cls.root / "corpus-b"
        cls.first_build = corpus.build(REPO, cls.frozen)
        cls.second_build = corpus.build(REPO, cls.reproduced)
        cls.verified = corpus.verify(cls.frozen)
        cls.full_run = evidence.run(cls.frozen, cls.root / "reports")

    @classmethod
    def tearDownClass(cls):
        # Recursive removal is confined to the generated test directory.
        assert cls.root.parent == Path(tempfile.gettempdir()).resolve()
        assert cls.root.name.startswith("step-ticket01-")
        cls.temporary.cleanup()

    def assert_code(self, code, call):
        with self.assertRaises(InvalidEvidence) as raised:
            call()
        self.assertEqual(code, raised.exception.code)

    def candidate(self):
        destination = self.root / self.id().split(".")[-1] / "corpus"
        shutil.copytree(self.frozen, destination)
        return destination

    def row(self, case_id):
        return next(row for row in self.verified["cases"] if row["case_id"] == f"S203-{case_id}")

    def test_exact_case_coverage_and_real_checks(self):
        self.assertEqual(49, len(self.verified["cases"]))
        self.assertEqual(13, sum(row["case_id"][5] == "P" for row in self.verified["cases"]))
        self.assertEqual(36, sum(row["case_id"][5] == "N" for row in self.verified["cases"]))
        self.assertEqual(49, self.verified["material_preparation_passed"])
        self.assertEqual(49, self.verified["oracle_passed"])

    def test_all_material_bytes_reproduce(self):
        self.assertEqual(self.first_build, self.second_build)
        self.assertEqual(controls.snapshot(self.frozen), controls.snapshot(self.reproduced))

    def test_frozen_output_is_never_overwritten(self):
        before = controls.snapshot(self.frozen)
        self.assert_code("Build.FrozenOutputAlreadyExists", lambda: corpus.build(REPO, self.frozen))
        self.assertEqual(before, controls.snapshot(self.frozen))

    def test_duplicate_occurrences_are_retained(self):
        data = (self.frozen / "cases/S203-N02/input.step").read_bytes()
        records = inventory(data)
        duplicate = [r for r in records if r.entity_id == 100]
        self.assertEqual((52, 51, 2), (len(records), len({r.entity_id for r in records}), len(duplicate)))
        self.assertNotEqual(duplicate[0].start, duplicate[1].start)
        self.assert_code("Oracle.AmbiguousEntityIdentity", lambda: by_id(records))

    def test_all_negative_edits_replay_once(self):
        for case_dir in sorted((self.frozen / "cases").glob("S203-N*")):
            with self.subTest(case=case_dir.name):
                definition = load_json(case_dir / "mutation.json")
                self.assertEqual(1, len(definition["edits"]))
                edit = definition["edits"][0]
                parent = (self.frozen / "cases" / definition["parent"] / "input.step").read_bytes()
                self.assertEqual(edit["old_text"].encode("ascii"), parent[edit["start"]:edit["end"]])
                child = parent[:edit["start"]] + edit["new_text"].encode("ascii") + parent[edit["end"]:]
                self.assertEqual(child, (case_dir / "input.step").read_bytes())

    def test_deliberate_orphans_remain(self):
        for key, expected in (("N08", 5), ("N28", 11)):
            records = inventory((self.frozen / f"cases/S203-{key}/input.step").read_bytes())
            self.assertEqual(expected, len(orphan_ids(records)))

    def test_negative_geometric_counterexamples(self):
        n20 = self.row("N20")["oracle"]["input_proof"]["facts"]
        n21 = self.row("N21")["oracle"]["input_proof"]["facts"]
        self.assertIn(exact(64), n20.values())
        self.assertIn(exact(F(1, 1000)), n21.values())
        n24 = self.row("N24")["oracle"]["input_proof"]["facts"]
        self.assertTrue(n24["path_connected"])
        self.assertFalse(n24["loop_closed"])

    def test_cycle_has_reachable_mm_anchor(self):
        facts = self.row("N34")["oracle"]["input_proof"]["facts"]
        self.assertEqual([19, 18, 19], facts["unit_cycle"])
        self.assertTrue(facts["mm_anchor_present"])

    def test_independent_plane_and_box_math(self):
        self.assertEqual(exact(96), self.row("P01")["oracle"]["truth"]["measures"]["area"])
        self.assertEqual(exact(88), self.row("P02")["oracle"]["truth"]["measures"]["area"])
        self.assertEqual(exact(6000), self.row("P04")["oracle"]["truth"]["measures"]["volume"])
        self.assertEqual([exact(F(189, 55)), exact(F(297, 55)), exact(5)],
                         self.row("P02")["oracle"]["truth"]["centroid"])

    def test_shifted_origin_preserves_world_geometry_and_negative_uv(self):
        a = self.row("P02")["oracle"]["truth"]
        b = self.row("P03")["oracle"]["truth"]
        self.assertEqual(a["centroid"], b["centroid"])
        self.assertEqual(a["source_definition"]["world_points_mm"], b["source_definition"]["world_points_mm"])
        self.assertEqual([[exact(-6), exact(-4)], [exact(6), exact(4)]],
                         b["source_definition"]["source_uv_window"])

    def test_unit_restatement_is_not_scaling_physical_shape(self):
        self.assertEqual(exact(9600), self.row("P06")["oracle"]["truth"]["measures"]["area"])
        self.assertEqual(exact(96), self.row("P07")["oracle"]["truth"]["measures"]["area"])
        for key in ("P11", "P13"):
            truth = self.row(key)["oracle"]["truth"]
            self.assertEqual(exact(F("98322.384")), truth["measures"]["volume"])
            self.assertEqual([exact(0), exact(1)], truth["source_definition"]["line_endpoint_parameters"])

    def test_surface_volume_is_not_zero(self):
        truth = self.row("P05")["oracle"]["truth"]
        self.assertNotIn("volume", truth["measures"])
        self.assertEqual("NotApplicable", truth["material_volume"]["applicability"])
        self.assertIsNone(truth["material_volume"]["value"])

    def test_query_inputs_closest_points_and_classifications(self):
        plane = self.row("P02")["oracle"]["truth"]["queries"]
        self.assertEqual(exact(F(37, 4)), plane["hole"]["distance_mm"]["radicand"])
        self.assertEqual([exact(F(16, 5)), exact(F(8, 5)), exact(5)], plane["hole"]["closest_point_mm"])
        self.assertEqual(["InsideFace", "OutsideFace", "OnBoundary", "OutsideFace"],
                         [q["location"] for q in plane["classifications"]])
        box = self.row("P04")["oracle"]["truth"]["queries"]
        self.assertEqual(exact(21), box["corner"]["distance_mm"]["radicand"])
        self.assertEqual(["Inside", "Outside", "OnFace", "OnEdge", "OnVertex"],
                         [q["material_location"] for q in box["classifications"]])

    def test_sqrt_intervals_prove_containment_and_budget(self):
        for radicand in (F(37, 4), F(21), F(0), F(9), F(1, 3)):
            interval = sqrt_interval(radicand)
            lo, hi = decode(interval["lower"]), decode(interval["upper"])
            self.assertLessEqual(lo * lo, radicand)
            self.assertGreaterEqual(hi * hi, radicand)
            self.assertLess(hi - lo, F(1, 100_000_000))
        self.assert_code("Oracle.SqrtDomain", lambda: sqrt_interval(F(-1)))

    def test_noncanonical_exact_encodings_are_rejected(self):
        for value in (96.0, {"kind": "rational", "num": "2", "den": "2"},
                      {"kind": "rational", "num": "1", "den": "0"},
                      {"kind": "rational", "num": "01", "den": "1"}):
            self.assert_code("Contract.ExactValueEncodingInvalid", lambda: decode(value))

    def test_json_duplicates_nonfinite_and_depth(self):
        self.assert_code("Contract.DuplicateJsonKey", lambda: parse_json(b'{"a":1,"a":2}'))
        for text in (b"NaN", b"Infinity", b"1e999"):
            self.assert_code("Contract.NonFiniteJson", lambda: parse_json(text))
        self.assert_code("Contract.MetadataDepthLimit", lambda: parse_json(b"[" * 66 + b"0" + b"]" * 66))

    def test_artifact_paths_cannot_escape_root(self):
        for value in ("../outside", str(self.root / "absolute"), ""):
            self.assert_code("Contract.ArtifactPathInvalid", lambda: inside(self.frozen, value))

    def test_measured_limits_cover_all_fixture_depths(self):
        limits = load_json(self.frozen / "resources/T01-corpus-limits.json")
        self.assertGreater(limits["max_case_bytes"], limits["measured"]["max_input_bytes"])
        data = (self.frozen / "cases/S203-P01/input.step").read_bytes()
        records = inventory(data)
        depth = max(nesting_depth(r.raw) for r in records)
        self.assertEqual(51, len(inventory(data, 51, depth)))
        self.assert_code("Preparation.RecordLimit", lambda: inventory(data, 50, depth))
        self.assert_code("Preparation.DepthLimit", lambda: inventory(data, 51, depth - 1))

    def test_quality_cannot_be_changed_with_resealed_hashes(self):
        candidate = self.candidate()
        path = candidate / "quality/Q-P0-mm-01.json"
        quality = load_json(path)
        quality["consistency_error_mm"] = exact(1)
        write_json(path, quality)
        controls.reseal_corpus(candidate)
        self.assert_code("Contract.QualityConflict", lambda: corpus.verify(candidate, self.frozen))

    def test_expected_diagnostic_cannot_silently_change(self):
        candidate = self.candidate()
        path = candidate / "cases/S203-N20/assertions.json"
        assertions = load_json(path)
        assertions["diagnostic_key"] = "AnyFailure"
        write_json(path, assertions)
        controls.reseal_corpus(candidate)
        self.assert_code("Contract.ExpectedResultConflict",
                         lambda: corpus.verify(candidate, self.frozen, ["S203-N20"]))

    def test_case_selection_rejects_unknown_or_duplicate(self):
        for selected in (["S203-P01", "S203-P01"], ["unknown"], []):
            self.assert_code("Contract.CaseSelectionInvalid",
                             lambda: corpus.verify(self.frozen, selected=selected))

    def test_selected_child_still_checks_its_parent(self):
        candidate = self.candidate()
        path = candidate / "cases/S203-P01/truth.json"
        truth = load_json(path)
        truth["measures"]["area"] = exact(95)
        write_json(path, truth)
        controls.reseal_corpus(candidate)
        self.assert_code("Oracle.ExpectedValueConflict",
                         lambda: corpus.verify(candidate, self.frozen, ["S203-N20"]))

    def test_three_single_case_paths_do_not_count_others(self):
        for case_id in ("S203-P01", "S203-N20", "S203-N02"):
            path = evidence.run(self.frozen, self.root / "singles", [case_id])
            self.assertEqual(1, evidence.check(path)["selected_cases"])
            summary = load_json(path / "summary.json")
            self.assertEqual([case_id], [r["case_id"] for r in summary["case_results"]])
            self.assertTrue((path / f"cases/{case_id}/1/case.html").is_file())

    def test_product_observations_are_not_invented(self):
        self.assertIsNone(self.verified["import_success_count"])
        self.assertEqual(49, self.verified["subject_not_run"])
        for row in self.verified["cases"]:
            self.assertEqual(("NotRun", None, "NotChecked"),
                             (row["subject_execution"], row["observed"], row["comparison"]))
        self.assertNotIn("_omni_solid", sys.modules)
        self.assertNotIn("OCC", sys.modules)

    def test_full_package_is_complete_but_not_product_pass(self):
        self.assertEqual({"evidence_completeness": "Complete", "selected_cases": 49,
                          "subject_execution": "NotRun", "import_success_count": None},
                         evidence.check(self.full_run))

    def test_report_write_failure_remains_partial_and_retry_is_new(self):
        output = self.root / "interrupted"
        original = evidence.write_json

        def fail_oracle(path, value):
            if path.name == "oracle.json":
                raise OSError("injected report write failure")
            original(path, value)

        with patch.object(evidence, "write_json", side_effect=fail_oracle):
            with self.assertRaises(OSError):
                evidence.run(self.frozen, output, ["S203-P01"])
        partial = next((output / "staging").iterdir())
        self.assertFalse((partial / "completion.json").exists())
        self.assertFalse((output / "runs").exists())
        self.assert_code("Evidence.UncommittedLocation", lambda: evidence.check(partial))
        retried = evidence.run(self.frozen, output, ["S203-P01"])
        self.assertNotEqual(partial.name.removesuffix(".partial"), retried.name)
        self.assertEqual("Complete", evidence.check(retried)["evidence_completeness"])
        self.assertTrue(partial.exists())

    def test_runs_are_append_only(self):
        before = controls.snapshot(self.full_run)
        following = evidence.run(self.frozen, self.root / "reports", ["S203-P01"])
        self.assertNotEqual(self.full_run.name, following.name)
        self.assertEqual(before, controls.snapshot(self.full_run))

    def test_all_fourteen_controls_reach_designated_checks(self):
        directory = controls.execute(self.frozen, self.root / "controls")
        summary = load_json(directory / "summary.json")
        self.assertEqual((12, 14, 14), (summary["control_groups"], summary["variants"], summary["passed"]))
        self.assertTrue(summary["all_passed"])
        self.assertTrue(summary["protected_materials_unchanged"])
        for row in summary["results"]:
            self.assertEqual("Passed", row["verdict"], row)
            self.assertEqual(row["expected_tool_diagnostic"], row["actual_tool_diagnostic"])
            self.assertTrue((directory / row["variant"] / "1/setup.receipt.json").is_file())


if __name__ == "__main__":
    unittest.main()
