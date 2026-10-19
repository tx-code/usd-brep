# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Foreign property scalars never change an analytic solid or gain source qualification."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX
from tools.step_semantic_validation.tests.test_ap214_foreign_real import real_item_source
from tools.step_semantic_validation.tests.test_interpretation import UNREGISTERED


class NativeForeignRealTests(unittest.TestCase):
    def test_explicit_foreign_property_preserves_box_geometry_and_source_diagnostics(self):
        for schema in (MBX, UNREGISTERED):
            with self.subTest(schema=schema), tempfile.TemporaryDirectory(prefix="native-foreign-real-") as directory:
                data = real_item_source(b"-3.50E+4", schema=schema)
                path = Path(directory) / "source.step"
                path.write_bytes(data)
                command = [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                           "Q-AP214-development-mm-01", "--interop-nongeometric", str(path)]
                if schema == UNREGISTERED:
                    command.append("--interpret-unregistered-ap214-2010")
                run = subprocess.run(command, capture_output=True, timeout=30)
                self.assertEqual(11 if schema == UNREGISTERED else 0, run.returncode, run.stderr)
                report = json.loads(run.stdout)
                self.assertEqual("AcceptedUnderInterpretation" if schema == UNREGISTERED else "Accepted",
                                 report["status"])
                self.assertEqual((6, 12, 8), tuple(report["model"][k] for k in ("faces", "edges", "vertices")))
                self.assertAlmostEqual(6000, report["model"]["material_volume"]["value_mm3"], delta=1e-5)
                self.assertAlmostEqual(2200, report["model"]["area_mm2"], delta=1e-6)
                self.assertEqual("UnsupportedEntityType",
                                 report["source_inspection"]["preserved_diagnostics"][0]["key"])
                self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])

    def test_query_copy_definitions_and_archive_do_not_depend_on_a_property_scalar(self):
        with tempfile.TemporaryDirectory(prefix="native-foreign-real-copy-") as directory:
            root = Path(directory)
            path, output = root / "source.step", root / "out"
            path.write_bytes(real_item_source())
            cli = Path(os.environ["STEP_NATIVE_BINARY"])
            binary = os.environ.get("STEP_NATIVE_GENERAL_BINARY",
                                    str(cli.with_name("step_import_general_tests" + cli.suffix)))
            run = subprocess.run([binary, "--development-file", str(path), str(output), "normal"],
                                 capture_output=True, timeout=30)
            self.assertEqual(0, run.returncode, run.stderr)
            checks = json.loads((output / "checks.json").read_bytes())
            self.assertEqual("Passed", checks["status"])
            self.assertTrue(checks["defining_coefficients_and_uv_checked"])
            for field in ("volume_mm3", "copied_volume_mm3", "reloaded_volume_mm3"):
                self.assertAlmostEqual(6000, checks[field], delta=1e-5)

    def test_failed_borrowed_field_publishes_no_model_or_native_candidate(self):
        with tempfile.TemporaryDirectory(prefix="native-foreign-real-invalid-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(real_item_source(b"3"))
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                                  "Q-AP214-development-mm-01", "--interop-nongeometric", str(path)],
                                 capture_output=True, timeout=30)
            self.assertEqual(2, run.returncode, run.stderr)
            report = json.loads(run.stdout)
            self.assertIsNone(report["model"])
            self.assertEqual(0, report["native_calls"])
