# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Real process controls; never infer instrumentation success from zero peaks."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.step_catalog_validation.process_measure import measure

# Windows venv redirectors start another process. Use the real interpreter for
# the fixtures whose independent truth is exactly a root and one child; the
# CLI argument/receipt test below still exercises the caller's actual launcher.
WORKER_PYTHON = getattr(sys, "_base_executable", sys.executable)


@unittest.skipUnless(os.name == "nt", "Windows Job Object measurement")
class ProcessMeasurementTests(unittest.TestCase):
    def test_committed_memory_and_short_lived_child_are_accounted(self):
        baseline = measure([WORKER_PYTHON, "-c", "pass"])
        allocated = measure([WORKER_PYTHON, "-c", "buffer = bytearray(64 * 1024 * 1024)"])
        child = measure([WORKER_PYTHON, "-c", (
            "import subprocess, sys; "
            "subprocess.run([sys.executable, '-c', 'buffer = bytearray(96 * 1024 * 1024)'], check=True)"
        )])
        self.assertEqual((0, 0, 0), tuple(row["exit_code"] for row in (baseline, allocated, child)))
        self.assertFalse(baseline["timed_out"])
        self.assertEqual(2, child["tree_processes"])
        self.assertGreater(allocated["peak_process_commit_bytes"], baseline["peak_process_commit_bytes"] + 60 * 1048576)
        self.assertGreater(child["peak_process_commit_bytes"], baseline["peak_process_commit_bytes"] + 90 * 1048576)
        self.assertGreaterEqual(child["peak_job_commit_bytes"], child["peak_process_commit_bytes"])

    def test_failure_and_timeout_are_not_successful_measurements(self):
        failed = measure([WORKER_PYTHON, "-c", "raise SystemExit(7)"])
        timeout = measure([WORKER_PYTHON, "-c", "import time; time.sleep(30)"], timeout=0.1)
        self.assertEqual(7, failed["exit_code"])
        self.assertFalse(failed["timed_out"])
        self.assertEqual(124, timeout["exit_code"])
        self.assertTrue(timeout["timed_out"])
        self.assertLess(timeout["wall_seconds"], 10)

    def test_child_outliving_root_remains_bounded_and_accounted(self):
        row = measure([WORKER_PYTHON, "-c", (
            "import subprocess, sys; "
            "subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])"
        )], timeout=0.3)
        self.assertEqual(2, row["tree_processes"])
        self.assertTrue(row["timed_out"])
        self.assertEqual(124, row["exit_code"])
        self.assertLess(row["wall_seconds"], 10)

    def test_cli_preserves_arguments_output_and_each_failed_receipt(self):
        with tempfile.TemporaryDirectory(prefix="catalog-process-measure-") as temporary:
            root = Path(temporary)
            script = root / "command with spaces.py"
            script.write_text("import sys; print(repr(sys.argv[1:])); raise SystemExit(7)", encoding="utf-8")
            arguments = ["a b", 'a"b', "中文", "trailing\\", "$(literal)"]
            command = [sys.executable, "-m", "tools.step_catalog_validation.process_measure",
                       "--output-directory", str(root / "receipts"), "--", sys.executable, str(script), *arguments]
            for _ in range(2):
                run = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                                     env={**os.environ, "PYTHONIOENCODING": "utf-8"}, timeout=15)
                self.assertEqual(7, run.returncode, run.stderr)
                self.assertEqual(repr(arguments), run.stdout.strip())
            receipts = list((root / "receipts").glob("*.json"))
            self.assertEqual(2, len(receipts))
            for path in receipts:
                row = json.loads(path.read_text(encoding="utf-8"))
                self.assertEqual(arguments, row["argv"][2:])
                self.assertEqual(7, row["exit_code"])


class ProcessMeasurementArguments(unittest.TestCase):
    def test_invalid_bounds_are_rejected_before_launch(self):
        for timeout in (0, -1, 3601, float("nan"), float("inf")):
            with self.subTest(timeout=timeout), self.assertRaises(ValueError):
                measure(["unused"], timeout)
        with self.assertRaises(ValueError):
            measure([])


if __name__ == "__main__":
    unittest.main()
