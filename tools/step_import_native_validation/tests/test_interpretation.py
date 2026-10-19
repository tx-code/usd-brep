# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Complete interpreted models stay separate from ordinary acceptance."""
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_interpretation import UNREGISTERED, box_source
from tools.step_import_native_validation.tests.test_general import cylinder_source


class InterpretedNativeTests(unittest.TestCase):
    def invoke(self, source, *extra, interpret=True, interop=True):
        with tempfile.TemporaryDirectory(prefix="step-interpreted-native-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(source)
            command = [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                       "Q-AP214-development-mm-01", str(path)]
            if interpret:
                command.append("--interpret-unregistered-ap214-2010")
            if interop:
                command.append("--interop-nongeometric")
            run = subprocess.run(command + list(extra), capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_complete_box_has_distinct_status_and_original_unverified_identity(self):
        run, result = self.invoke(box_source())
        self.assertEqual(11, run.returncode, run.stderr)
        self.assertEqual("AcceptedUnderInterpretation", result["status"])
        self.assertFalse(result["source_binding_verified"])
        self.assertFalse(result["full_schema_conformance"])
        self.assertEqual((6, 12, 8), tuple(result["model"][key] for key in ("faces", "edges", "vertices")))
        self.assertAlmostEqual(6000, result["model"]["material_volume"]["value_mm3"], delta=1e-5)
        self.assertAlmostEqual(2200, result["model"]["area_mm2"], delta=1e-6)
        identity = result["source_inspection"]["schema_selection"]["identifiers"][0]
        self.assertEqual("UnregisteredVersion", identity["match"])
        self.assertIsNone(identity["registry_id"])
        self.assertEqual(UNREGISTERED.decode(), identity["literal"])
        self.assertEqual(box_source().decode(), result["source_inspection"]["source"]["input"]["raw"])

    def test_default_still_refuses_and_incomplete_content_never_publishes(self):
        run, result = self.invoke(box_source(), interpret=False)
        self.assertEqual(2, run.returncode)
        self.assertIsNone(result["model"])
        broken = box_source().replace(b"ENDSEC;\nEND-ISO", b"#9999=UNSUPPORTED_REQUIRED_TYPE();\nENDSEC;\nEND-ISO")
        run, result = self.invoke(broken)
        self.assertEqual(2, run.returncode)
        self.assertIsNone(result["model"])
        run, result = self.invoke(box_source(), interop=False)
        self.assertEqual(2, run.returncode)  # Existing native CLI invalid-argument exit contract.
        self.assertIsNone(result)

    def test_limits_never_publish_a_partial_interpreted_model(self):
        for flags, expected in ((["--max-native-calls", "0"], 3), (["--max-report-bytes", "1"], 5)):
            run, result = self.invoke(box_source(), *flags)
            self.assertEqual(expected, run.returncode, run.stderr)
            if result:
                self.assertIsNone(result["model"])
            else:
                self.assertEqual(b"", run.stdout)

    def test_complete_cylinder_copy_and_archive_keep_separate_qualification(self):
        data = cylinder_source().replace(b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}", UNREGISTERED)
        cli = Path(os.environ["STEP_NATIVE_BINARY"])
        binary = os.environ.get("STEP_NATIVE_GENERAL_BINARY", str(cli.with_name("step_import_general_tests" + cli.suffix)))
        with tempfile.TemporaryDirectory(prefix="step-interpreted-lifecycle-") as directory:
            root = Path(directory)
            source = root / "source.step"
            source.write_bytes(data)
            output = root / "out"
            run = subprocess.run([str(binary), "--development-file", str(source), str(output), "normal",
                                  "--interpret-unregistered-ap214-2010"], capture_output=True, timeout=30)
            self.assertEqual(0, run.returncode, run.stderr)
            checks = json.loads((output / "checks.json").read_bytes())
            report = json.loads((output / "import.json").read_bytes())
        self.assertEqual("AcceptedUnderInterpretation", report["status"])
        self.assertFalse(checks["source_binding_verified"])
        self.assertTrue(checks["defining_coefficients_and_uv_checked"])
        for field in ("volume_mm3", "copied_volume_mm3", "reloaded_volume_mm3"):
            self.assertAlmostEqual(63 * math.pi, checks[field], delta=63 * math.pi * 2e-4)
