# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Known linked geometry cannot be silently dropped at native publication."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_wireframe import linked_wire_source


class LinkedWireframeNativeTests(unittest.TestCase):
    def test_unbuilt_linked_geometry_blocks_the_whole_request_before_native_calls(self):
        with tempfile.TemporaryDirectory(prefix="step-wire-publication-") as directory:
            path = Path(directory) / "source.step"
            data = linked_wire_source()
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                                  "Q-AP214-development-mm-01", str(path)], capture_output=True, timeout=20)
        self.assertEqual(2, run.returncode, run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual("Checked", report["source_inspection"]["status"])
        self.assertEqual("GeneralLinkedWireframeUnsupported", report["diagnostics"][0]["key"])
        self.assertEqual("920", report["diagnostics"][0]["record_id"])
        self.assertEqual(0, report["native_calls"])
        self.assertIsNone(report["model"])
        self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])
