# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit report-only assembly CLI follows the owning native assembly API."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


FIXTURE = Path(__file__).resolve().parents[3] / "source/STEP_SEMANTICS/tests/fixtures/assembly_repeated_box.step"
LIMITS = ["--max-assembly-usages", "100", "--max-assembly-instances", "100", "--max-assembly-depth", "16"]


class AssemblyCliTests(unittest.TestCase):
    def invoke(self, *args):
        source = FIXTURE.read_bytes()
        with tempfile.TemporaryDirectory(prefix="public-assembly-cli-") as folder:
            path = Path(folder) / "assembly.step"
            path.write_bytes(source)
            run = subprocess.run(
                [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality", "Q-AP214-development-mm-01",
                 str(path), *args], capture_output=True, timeout=40,
            )
            self.assertEqual(source, path.read_bytes())
        return run, json.loads(run.stdout) if run.stdout else None

    def test_complete_roots_definitions_and_repeated_instances_are_reported(self):
        run, report = self.invoke("--assembly", *LIMITS)
        self.assertEqual(0, run.returncode, run.stderr or report)
        self.assertEqual("Accepted", report["status"])
        assembly = report["assembly"]
        self.assertEqual(2, len(assembly["definitions"]))
        self.assertEqual(3, len(assembly["instances"]))
        self.assertEqual(1, len(assembly["roots"]))
        self.assertFalse(assembly["source_rewritten"])
        self.assertIsNone(report["model"])
        self.assertEqual({"MaterialSolid", "AssemblyPlacementWrapper"}, {item["role"] for item in assembly["definitions"]})
        self.assertTrue(all(item["checks_passed"] for item in report["product_document"]["definitions"]))

    def test_graph_resource_failure_never_becomes_a_partial_assembly(self):
        run, report = self.invoke("--assembly", "--max-assembly-usages", "100", "--max-assembly-instances", "1",
                                  "--max-assembly-depth", "16")
        self.assertEqual(3, run.returncode, run.stderr or report)
        self.assertEqual("ResourceLimit", report["status"])
        self.assertIsNone(report["model"])

    def test_missing_caps_and_incompatible_singular_modes_are_argument_errors(self):
        variants = [["--assembly"], LIMITS, ["--assembly", *LIMITS, "--assembly"],
                    ["--assembly", *LIMITS, "--query", "0", "0", "0"],
                    ["--assembly", *LIMITS, "--independent-products"],
                    ["--assembly", *LIMITS, "--material-collection"],
                    ["--assembly", *LIMITS, "--unreferenced-placement-policy", "Guess"],
                    ["--unreferenced-placement-policy", "RetainDisconnected"]]
        for args in variants:
            with self.subTest(args=args):
                run, report = self.invoke(*args)
                self.assertEqual(2, run.returncode)
                self.assertIsNone(report)


if __name__ == "__main__":
    unittest.main()
