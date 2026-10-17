# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.step_import_benchmark.tests.test_probes import BOX


class RunnerTests(unittest.TestCase):
    def test_cli_excludes_holdout_and_resumes_only_identical_measurements(self):
        with tempfile.TemporaryDirectory(prefix="step-batch-") as directory:
            root = Path(directory)
            paths = (root/"part.step", root/"holdout.step")
            paths[0].write_bytes(BOX.read_bytes())
            paths[1].write_bytes(BOX.read_bytes()+b"\n/* held out */\n")
            rows = [dict(relative_path=p.name, bytes=p.stat().st_size,
                         sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                    for p in paths]
            census, cohorts = root/"census.json", root/"cohorts.json"
            census.write_text(json.dumps(dict(root=str(root), files=rows)), encoding="utf-8")
            cohorts.write_text(json.dumps(dict(development=[rows[0]], holdout=[rows[1]], large=[])), encoding="utf-8")
            output = root/"run"
            args = [sys.executable, "-m", "tools.step_import_benchmark", "run", "--census", str(census),
                    "--cohorts", str(cohorts), "--output", str(output), "--scope", "source-scan",
                    "--occt-bin", os.environ["STEP_BENCH_OCCT_BIN"], "--repeat", "2", "--warmup", "1"]
            for engine in ("source", "native", "occt"):
                args += ["--"+engine+"-probe", os.environ["STEP_BENCH_"+engine.upper()]]
            actual = subprocess.run(args, capture_output=True, timeout=30)
            self.assertEqual(0, actual.returncode, actual.stderr.decode(errors="replace"))
            result = json.loads((output/"results.json").read_bytes())
            self.assertEqual(1, len(result["rows"]))
            self.assertEqual("part.step", result["rows"][0]["relative_path"])
            self.assertIsNone(result["summary"]["speedup_ratio"])
            group = result["summary"]["groups"]["source:source"]
            self.assertEqual((1, 0, {"Inspected": 1}), (group["files"], group["model_files"], group["outcomes"]))
            observation = output/"cases/F00001/source-source/observation.json"
            previous = observation.read_bytes()
            stamp = observation.stat().st_mtime_ns
            resumed = subprocess.run(args+["--resume"], capture_output=True, timeout=30)
            self.assertEqual(0, resumed.returncode, resumed.stderr.decode(errors="replace"))
            self.assertEqual((previous, stamp), (observation.read_bytes(), observation.stat().st_mtime_ns))
            paths[0].write_bytes(paths[0].read_bytes()+b"\n/* changed */\n")
            changed = subprocess.run(args+["--resume"], capture_output=True, timeout=30)
            self.assertNotEqual(0, changed.returncode)

    def test_timeout_and_process_failure_never_get_an_import_timing(self):
        from tools.step_import_benchmark.runner import execute
        with tempfile.TemporaryDirectory(prefix="step-supervisor-") as directory:
            root = Path(directory)
            binary = Path(os.environ["STEP_BENCH_SOURCE"])
            sha = hashlib.sha256(BOX.read_bytes()).hexdigest()
            stopped = execute(binary, "source", BOX, sha, root/"timeout", repeat=50, warmup=5, timeout=0.000001)
            self.assertEqual("Timeout", stopped["status"])
            self.assertIsNone(stopped["result"])
            failure = execute(binary, "not-a-mode", BOX, sha, root/"failure")
            self.assertEqual("ProbeFailure", failure["status"])
            self.assertIsNone(failure["result"])
