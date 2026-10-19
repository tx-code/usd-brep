# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Native publication keeps a name-only AP214 assumption explicit."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_name_only_interpretation import FLAG, name_only_box


class NativeNameOnlyTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix="step-native-name-only-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run(
                [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", str(path),
                 "--quality", "Q-AP214-development-mm-01", "--interop-nongeometric", *flags],
                capture_output=True, timeout=30,
            )
        return run, json.loads(run.stdout) if run.stdout else None

    def test_complete_native_model_does_not_authenticate_the_family_name(self):
        run, report = self.invoke(name_only_box(), FLAG)
        self.assertEqual(11, run.returncode, run.stderr or run.stdout)
        self.assertEqual("AcceptedUnderInterpretation", report["status"])
        self.assertFalse(report["source_binding_verified"])
        self.assertEqual("Ap214NameOnlyAs2010", report["source_interpretation"])
        self.assertAlmostEqual(6000, report["model"]["material_volume"]["value_mm3"], delta=1e-5)
        self.assertEqual(name_only_box().decode(), report["source_inspection"]["source"]["input"]["raw"])

    def test_carrier_and_name_only_schema_have_independent_evidence(self):
        data = name_only_box().replace(b"'2;1'", b"'1'", 1)
        run, report = self.invoke(data, FLAG, "--interpret-level1-as-corrected")
        self.assertEqual(11, run.returncode, run.stderr or run.stdout)
        self.assertFalse(report["source_binding_verified"])
        self.assertFalse(report["source_carrier_verified"])
        self.assertEqual("Ap214NameOnlyAs2010", report["source_interpretation"])

    def test_resource_failure_still_withholds_the_complete_model(self):
        run, report = self.invoke(name_only_box(), FLAG, "--max-native-calls", "1")
        self.assertEqual(3, run.returncode)
        self.assertIsNone(report["model"])
        self.assertFalse(report["source_binding_verified"])
