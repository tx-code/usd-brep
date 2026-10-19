# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Contracts at actual benchmark executables; geometric expectations are analytic."""

import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[3]
BOX = REPO / "tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P04/input.step"
HOLES = REPO / "source/STEP_IMPORT/tests/fixtures/perforated_shared_sheet.step"


class ProbeTests(unittest.TestCase):
    def test_explicit_interpretation_has_a_distinct_result_and_settings(self):
        from tools.step_semantic_validation.tests.test_interpretation import box_source

        with tempfile.TemporaryDirectory(prefix="step-interpreted-probe-") as directory:
            source = Path(directory) / "source.step"
            source.write_bytes(box_source())
            (before,) = self.invoke("native", "import", source, "--quality", "Q-AP214-development-mm-01",
                                    "--source-policy", "PreserveNonGeometric")["trials"]
            self.assertEqual("Unsupported", before["status"])
            (trial,) = self.invoke("native", "import", source, "--quality", "Q-AP214-development-mm-01",
                                   "--source-policy", "PreserveNonGeometric", "--schema-interpretation",
                                   "UnregisteredAp214As2010")["trials"]
            self.assertEqual("AcceptedUnderInterpretation", trial["status"])
            self.assertTrue(trial["has_model"])
            self.assertTrue(trial["schema_interpretation_applied"])
            self.assertFalse(trial["source_binding_verified"])
            self.assertAlmostEqual(6000, trial["model"]["volume_mm3"], delta=1e-5)

    def test_interpretation_requires_native_general_scope_before_input_budget_skip(self):
        for engine, mode in (("source", "source"), ("occt", "transfer"), ("native", "import")):
            with tempfile.TemporaryDirectory(prefix="step-interpreted-probe-negative-") as directory:
                output = Path(directory) / "result.json"
                run = subprocess.run([os.environ["STEP_BENCH_" + engine.upper()], "--mode", mode, "--input", str(BOX),
                                      "--output", str(output), "--max-input-bytes", "0", "--schema-interpretation",
                                      "UnregisteredAp214As2010"], capture_output=True, timeout=30)
                self.assertNotEqual(0, run.returncode)
                self.assertFalse(output.exists())

    def invoke(self, engine, mode, path=BOX, *options):
        binary = Path(os.environ["STEP_BENCH_" + engine.upper()]).resolve(strict=True)
        with tempfile.TemporaryDirectory(prefix="step-benchmark-") as root:
            output = Path(root) / "result.json"
            actual = subprocess.run(
                [str(binary), "--mode", mode, "--input", str(path), "--output", str(output), *options],
                capture_output=True,
                timeout=40,
            )
            self.assertEqual(0, actual.returncode, actual.stderr.decode(errors="replace"))
            return json.loads(output.read_bytes())

    def test_source_probe_only_accepts_a_complete_graph_without_a_model(self):
        result = self.invoke("source", "source")
        self.assertEqual("step-import-benchmark.v1", result["format"])
        (trial,) = result["trials"]
        self.assertEqual(("Inspected", False), (trial["status"], trial["has_model"]))
        self.assertGreater(trial["records"], 0)
        self.assertGreaterEqual(trial["operation_ms"], 0)
        self.assertIsNone(trial["model"])

    def test_both_model_probes_match_the_independent_box(self):
        for engine, mode in (("native", "import"), ("occt", "transfer")):
            with self.subTest(engine=engine):
                (trial,) = self.invoke(engine, mode)["trials"]
                self.assertTrue(trial["has_model"])
                self.assertEqual((6, 12, 8), tuple(trial["model"][k] for k in ("faces", "edges", "vertices")))
                self.assertAlmostEqual(2200, trial["model"]["area_mm2"], delta=1e-6)
                self.assertAlmostEqual(6000, trial["model"]["volume_mm3"], delta=1e-5)

    def test_both_model_probes_preserve_hole_area(self):
        for engine, mode in (("native", "import"), ("occt", "transfer")):
            with self.subTest(engine=engine):
                (trial,) = self.invoke(engine, mode, HOLES)["trials"]
                self.assertTrue(trial["has_model"])
                self.assertEqual((2, 15, 14), tuple(trial["model"][k] for k in ("faces", "edges", "vertices")))
                self.assertAlmostEqual(16, trial["model"]["area_mm2"], delta=1e-6)
                self.assertIsNone(trial["model"]["volume_mm3"])

    def test_warmup_and_measured_trials_are_separate(self):
        result = self.invoke("source", "source", BOX, "--warmup", "1", "--repeat", "2")
        self.assertEqual([True, False, False], [t["warmup"] for t in result["trials"]])

    def test_explicit_general_probe_uses_public_import_policy_and_records_settings(self):
        from tools.step_import_native_validation.tests.test_general import cylinder_source

        with tempfile.TemporaryDirectory(prefix="step-general-benchmark-") as directory:
            path = Path(directory) / "cylinder.step"
            path.write_bytes(cylinder_source())
            result = self.invoke(
                "native",
                "import",
                path,
                "--quality",
                "Q-AP214-development-mm-01",
                "--source-policy",
                "PreserveNonGeometric",
                "--warmup",
                "1",
                "--repeat",
                "2",
            )
        self.assertEqual([True, False, False], [x["warmup"] for x in result["trials"]])
        for trial in result["trials"]:
            self.assertEqual("Accepted", trial["status"])
            self.assertEqual((3, 3, 2), tuple(trial["model"][k] for k in ("faces", "edges", "vertices")))
            self.assertEqual("Q-AP214-development-mm-01", trial["settings"]["quality_id"])
            self.assertEqual("PreserveNonGeometric", trial["settings"]["source_policy"])
            self.assertEqual(0.001, trial["settings"]["derived_uv_mm"])
            self.assertIn("model_info_after_import", trial["timings_ms"])
            self.assertIn("BeforeReport", trial["milestones_ms_since_start"])

    def test_general_quality_does_not_implicitly_select_interoperability(self):
        from tools.step_semantic_validation.tests.test_ap214_metadata import rendering_source

        with tempfile.TemporaryDirectory(prefix="step-policy-benchmark-") as directory:
            path = Path(directory) / "metadata.step"
            path.write_bytes(rendering_source(b"$"))
            (strict,) = self.invoke("native", "import", path, "--quality", "Q-AP214-development-mm-01")["trials"]
            (interop,) = self.invoke(
                "native",
                "import",
                path,
                "--quality",
                "Q-AP214-development-mm-01",
                "--source-policy",
                "PreserveNonGeometric",
            )["trials"]
        self.assertEqual(("Rejected", False), (strict["status"], strict["has_model"]))
        self.assertEqual(("Accepted", True), (interop["status"], interop["has_model"]))
        self.assertEqual(1, interop["preserved_source_diagnostics"])

    def test_boundary_profile_is_opt_in_and_keeps_public_import_results(self):
        from tools.step_import_native_validation.tests.test_general import cylinder_source

        with tempfile.TemporaryDirectory(prefix="step-native-profile-") as directory:
            path = Path(directory) / "cylinder.step"
            path.write_bytes(cylinder_source())
            flags = ("--quality", "Q-AP214-development-mm-01")
            plain, = self.invoke("native", "import", path, *flags)["trials"]
            profiled, = self.invoke("native", "import", path, *flags, "--native-boundary-timing", "1")["trials"]
        self.assertNotIn("native_boundary_profile", plain)
        self.assertEqual("Accepted", profiled["status"])
        for field in ("model", "source_sha256", "native_calls", "report_bytes", "settings"):
            self.assertEqual(plain[field], profiled[field])
        profile = profiled["native_boundary_profile"]
        self.assertTrue(profile["complete"])
        intervals = profile["intervals"]
        self.assertEqual(list(range(1, profiled["native_calls"] + 1)), [x["call"] for x in intervals])
        self.assertTrue(all(x["elapsed_ms"] >= 0 for x in intervals))
        self.assertLessEqual(sum(x["elapsed_ms"] for x in intervals), profiled["operation_ms"])

    def test_explicit_opaque_data_probe_records_the_real_import_policy(self):
        from tools.step_import_native_validation.tests.test_opaque_data import opaque_source

        with tempfile.TemporaryDirectory(prefix="step-opaque-benchmark-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(opaque_source())
            trial, = self.invoke("native", "import", path, "--quality", "Q-AP214-development-mm-01",
                                 "--source-policy", "PreserveNonGeometric",
                                 "--data-string-policy", "PreserveOpaqueBytes")["trials"]
        self.assertTrue(trial["has_model"])
        self.assertEqual("PreserveOpaqueBytes", trial["data_string_policy"])
        self.assertGreaterEqual(trial["preserved_source_diagnostics"], 1)
        self.assertEqual((3, 3, 2), tuple(trial["model"][k] for k in ("faces", "edges", "vertices")))

    def test_other_readers_and_invalid_flags_cannot_silently_ignore_native_configuration(self):
        with tempfile.TemporaryDirectory(prefix="step-invalid-benchmark-") as directory:
            for engine, mode, flags in [
                ("source", "source", ["--quality", "Q-AP214-development-mm-01"]),
                ("occt", "read", ["--source-policy", "PreserveNonGeometric"]),
                ("native", "import", ["--quality", "unknown"]),
                ("native", "import", ["--source-policy", "unknown"]),
                ("source", "source", ["--native-boundary-timing", "1"]),
                ("source", "source", ["--native-boundary-timing", "0"]),
                ("occt", "read", ["--native-boundary-timing", "0", "--max-input-bytes", "1"]),
                ("occt", "read", ["--header-name-policy", "PreserveOpaqueBytes"]),
                ("native", "import", ["--native-boundary-timing", "2"]),
                ("native", "import", ["--header-name-policy", "unknown"]),
                ("native", "import", ["--header-name-policy", "PreserveOpaqueBytes"]),
                ("source", "source", ["--data-string-policy", "PreserveOpaqueBytes"]),
                ("occt", "transfer", ["--data-string-policy", "StrictAscii"]),
                ("native", "import", ["--data-string-policy", "unknown"]),
                ("native", "import", ["--data-string-policy", "PreserveOpaqueBytes"]),
            ]:
                with self.subTest(engine=engine, flags=flags):
                    output = Path(directory) / (engine + ".json")
                    run = subprocess.run(
                        [
                            os.environ["STEP_BENCH_" + engine.upper()],
                            "--mode",
                            mode,
                            "--input",
                            str(BOX),
                            "--output",
                            str(output),
                            "--max-input-bytes",
                            "1",
                            *flags,
                        ],
                        capture_output=True,
                        timeout=20,
                    )
                    self.assertNotEqual(0, run.returncode)
                    self.assertFalse(output.exists())

    def test_input_budget_is_a_not_run_policy_rather_than_invalid_geometry(self):
        for engine, mode in (("source", "source"), ("occt", "read")):
            with self.subTest(engine=engine):
                (trial,) = self.invoke(engine, mode, BOX, "--max-input-bytes", "1")["trials"]
                self.assertEqual(("NotRunInputBudget", False), (trial["status"], trial["has_model"]))
                self.assertIsNone(trial["operation_ms"])

    def test_unicode_model_paths_work_in_both_native_processes(self):
        with tempfile.TemporaryDirectory(prefix="step-unicode-") as directory:
            path = Path(directory) / "模型 盒体.step"
            path.write_bytes(BOX.read_bytes())
            for engine, mode in (("native", "import"), ("occt", "transfer")):
                with self.subTest(engine=engine):
                    (trial,) = self.invoke(engine, mode, path)["trials"]
                    self.assertTrue(trial["has_model"])
                    self.assertAlmostEqual(6000, trial["model"]["volume_mm3"], delta=1e-5)

    def test_occt_mixed_root_volume_excludes_the_open_face(self):
        from tools.step_import_validation.records import inventory, vector

        plane = BOX.parents[1] / "S203-P01/input.step"
        data = BOX.read_bytes().split(b"ENDSEC;\nEND-ISO-10303-21;")[0]
        for record in inventory(plane.read_bytes()):
            raw = record.raw
            if record.kind == "CARTESIAN_POINT":
                args = list(record.args)
                x, y, z = vector(args[1])
                args[1] = f"({float(x)},{float(y)},{float(z + 100)})"
                raw = record.kind + "(" + ",".join(args) + ")"
            raw = re.sub(r"#(\d+)", lambda m: "#" + str(int(m[1]) + 10000), raw)
            data += f"\n#{record.entity_id + 10000}={raw};".encode("ascii")
        data += b"\nENDSEC;\nEND-ISO-10303-21;\n"
        with tempfile.TemporaryDirectory(prefix="step-mixed-volume-") as directory:
            path = Path(directory) / "mixed.step"
            path.write_bytes(data)
            (trial,) = self.invoke("occt", "transfer", path)["trials"]
            self.assertTrue(trial["has_model"])
            self.assertEqual((1, 7), (trial["model"]["solids"], trial["model"]["faces"]))
            self.assertAlmostEqual(6000, trial["model"]["volume_mm3"], delta=1e-5)
