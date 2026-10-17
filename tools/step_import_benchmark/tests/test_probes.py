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
    def invoke(self, engine, mode, path=BOX, *options):
        binary = Path(os.environ["STEP_BENCH_" + engine.upper()]).resolve(strict=True)
        with tempfile.TemporaryDirectory(prefix="step-benchmark-") as root:
            output = Path(root) / "result.json"
            actual = subprocess.run([str(binary), "--mode", mode, "--input", str(path), "--output", str(output),
                                     *options], capture_output=True, timeout=40)
            self.assertEqual(0, actual.returncode, actual.stderr.decode(errors="replace"))
            return json.loads(output.read_bytes())

    def test_source_probe_only_accepts_a_complete_graph_without_a_model(self):
        result = self.invoke("source", "source")
        self.assertEqual("step-import-benchmark.v1", result["format"])
        trial, = result["trials"]
        self.assertEqual(("Inspected", False), (trial["status"], trial["has_model"]))
        self.assertGreater(trial["records"], 0)
        self.assertGreaterEqual(trial["operation_ms"], 0)
        self.assertIsNone(trial["model"])

    def test_both_model_probes_match_the_independent_box(self):
        for engine, mode in (("native", "import"), ("occt", "transfer")):
            with self.subTest(engine=engine):
                trial, = self.invoke(engine, mode)["trials"]
                self.assertTrue(trial["has_model"])
                self.assertEqual((6, 12, 8), tuple(trial["model"][k] for k in ("faces", "edges", "vertices")))
                self.assertAlmostEqual(2200, trial["model"]["area_mm2"], delta=1e-6)
                self.assertAlmostEqual(6000, trial["model"]["volume_mm3"], delta=1e-5)

    def test_both_model_probes_preserve_hole_area(self):
        for engine, mode in (("native", "import"), ("occt", "transfer")):
            with self.subTest(engine=engine):
                trial, = self.invoke(engine, mode, HOLES)["trials"]
                self.assertTrue(trial["has_model"])
                self.assertEqual((2, 15, 14), tuple(trial["model"][k] for k in ("faces", "edges", "vertices")))
                self.assertAlmostEqual(16, trial["model"]["area_mm2"], delta=1e-6)
                self.assertIsNone(trial["model"]["volume_mm3"])

    def test_warmup_and_measured_trials_are_separate(self):
        result = self.invoke("source", "source", BOX, "--warmup", "1", "--repeat", "2")
        self.assertEqual([True, False, False], [t["warmup"] for t in result["trials"]])

    def test_input_budget_is_a_not_run_policy_rather_than_invalid_geometry(self):
        for engine, mode in (("source", "source"), ("occt", "read")):
            with self.subTest(engine=engine):
                trial, = self.invoke(engine, mode, BOX, "--max-input-bytes", "1")["trials"]
                self.assertEqual(("NotRunInputBudget", False), (trial["status"], trial["has_model"]))
                self.assertIsNone(trial["operation_ms"])

    def test_unicode_model_paths_work_in_both_native_processes(self):
        with tempfile.TemporaryDirectory(prefix="step-unicode-") as directory:
            path = Path(directory)/"模型 盒体.step"
            path.write_bytes(BOX.read_bytes())
            for engine, mode in (("native", "import"), ("occt", "transfer")):
                with self.subTest(engine=engine):
                    trial, = self.invoke(engine, mode, path)["trials"]
                    self.assertTrue(trial["has_model"])
                    self.assertAlmostEqual(6000, trial["model"]["volume_mm3"], delta=1e-5)

    def test_occt_mixed_root_volume_excludes_the_open_face(self):
        from tools.step_import_validation.records import inventory, vector
        plane = BOX.parents[1]/"S203-P01/input.step"
        data = BOX.read_bytes().split(b"ENDSEC;\nEND-ISO-10303-21;")[0]
        for record in inventory(plane.read_bytes()):
            raw = record.raw
            if record.kind == "CARTESIAN_POINT":
                args = list(record.args)
                x, y, z = vector(args[1])
                args[1] = f"({float(x)},{float(y)},{float(z+100)})"
                raw = record.kind+"("+",".join(args)+")"
            raw = re.sub(r"#(\d+)", lambda m: "#"+str(int(m[1])+10000), raw)
            data += f"\n#{record.entity_id+10000}={raw};".encode("ascii")
        data += b"\nENDSEC;\nEND-ISO-10303-21;\n"
        with tempfile.TemporaryDirectory(prefix="step-mixed-volume-") as directory:
            path = Path(directory)/"mixed.step"
            path.write_bytes(data)
            trial, = self.invoke("occt", "transfer", path)["trials"]
            self.assertTrue(trial["has_model"])
            self.assertEqual((1, 7), (trial["model"]["solids"], trial["model"]["faces"]))
            self.assertAlmostEqual(6000, trial["model"]["volume_mm3"], delta=1e-5)
