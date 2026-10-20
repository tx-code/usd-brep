# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""A missing surface is a source failure, never a partial/native role conversion."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_surface_role import CLAUSE, surface_role_source


class SurfaceRoleNativeTests(unittest.TestCase):
    def test_invalid_and_not_yet_supported_surface_roles_never_enter_native_build(self):
        for surface, status, code in ((False, "Rejected", 1), (True, "Unsupported", 2)):
            with self.subTest(surface=surface), tempfile.TemporaryDirectory(prefix="step-native-surface-role-") as directory:
                path = Path(directory) / "synthetic.step"
                data = surface_role_source(surface=surface, sphere=True)
                path.write_bytes(data)
                run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                                      "Q-AP214-development-mm-01", str(path)], capture_output=True, timeout=30)
                self.assertTrue(run.stdout, run.stderr)
                report = json.loads(run.stdout)
                self.assertEqual(code, run.returncode, run.stderr or report["diagnostics"])
                self.assertEqual(status, report["status"])
                self.assertIsNone(report["model"])
                self.assertEqual(0, report["native_calls"])
                self.assertEqual(data, path.read_bytes())
                if not surface:
                    self.assertEqual(CLAUSE, report["diagnostics"][0]["key"])
