# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Native results retain an explicit, unverified corrected-carrier assumption."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_carrier_level1 import FLAG, level1_box
from tools.step_semantic_validation.tests.test_ap214 import AP214


class NativeCarrierTests(unittest.TestCase):
    def invoke(self, data, *flags, quality="Q-AP214-development-mm-01", interop=True):
        with tempfile.TemporaryDirectory(prefix="step-native-carrier-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            command = [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", str(path), "--quality", quality]
            if interop:
                command.append("--interop-nongeometric")
            run = subprocess.run(command + list(map(str, flags)), capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_explicit_carrier_assumption_has_a_distinct_complete_native_result(self):
        data = level1_box()
        strict, before = self.invoke(data)
        run, report = self.invoke(data, FLAG)
        self.assertEqual(2, strict.returncode)
        self.assertIsNone(before["model"])
        self.assertEqual(0, before["native_calls"])
        self.assertIn("UnsupportedImplementationLevel", [x["key"] for x in before["diagnostics"]])
        self.assertEqual(11, run.returncode, run.stderr or run.stdout)
        self.assertEqual("AcceptedUnderInterpretation", report["status"])
        self.assertFalse(report["source_carrier_verified"])
        self.assertEqual("Level1AsCorrectedEncoding", report["source_carrier_interpretation"])
        raw = report["source_inspection"]["source"]
        self.assertEqual(data.decode(), raw["input"]["raw"])
        self.assertEqual("1", raw["header"][0]["parameters"][1]["text"])
        self.assertEqual(before["source_inspection"]["source"]["diagnostics"],
                         raw["carrier_interpretation"]["original_diagnostics"])
        self.assertEqual("Passed", report["source_inspection"]["checks"]["schema_binding"])
        self.assertNotIn("source_binding_verified", report)
        self.assertEqual((6, 12, 8), tuple(report["model"][k] for k in ("faces", "edges", "vertices")))
        self.assertAlmostEqual(6000, report["model"]["material_volume"]["value_mm3"], delta=1e-5)

    def test_an_unused_option_does_not_create_a_model_assumption(self):
        data = level1_box().replace(b"'1'", b"'2;1'", 1)
        before, old = self.invoke(data)
        run, report = self.invoke(data, FLAG)
        self.assertEqual((0, 0), (before.returncode, run.returncode), run.stderr or run.stdout)
        self.assertEqual("Accepted", report["status"])
        self.assertEqual(old["model"], report["model"])
        self.assertEqual(old["native_calls"], report["native_calls"])
        self.assertTrue(report["source_carrier_verified"])
        self.assertNotIn("source_carrier_interpretation", report)
        self.assertFalse(report["source_inspection"]["source"]["carrier_interpretation"]["applied"])

    def test_schema_and_carrier_assumptions_compose_without_becoming_verified(self):
        data = level1_box().replace(AP214, b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1 1}")
        run, report = self.invoke(data, FLAG, "--interpret-unregistered-ap214-2010")
        self.assertEqual(11, run.returncode, run.stderr or run.stdout)
        self.assertFalse(report["source_binding_verified"])
        self.assertFalse(report["source_carrier_verified"])
        self.assertTrue(report["source_inspection"]["schema_interpretation"]["applied"])
        self.assertTrue(report["source_inspection"]["source"]["carrier_interpretation"]["applied"])

    def test_no_other_carrier_or_schema_becomes_an_alias(self):
        for level in (b"2;2", b"3;1", b"4;1", b"1;1"):
            run, report = self.invoke(level1_box().replace(b"'1'", b"'" + level + b"'", 1), FLAG)
            self.assertEqual(2, run.returncode, run.stderr or run.stdout)
            self.assertIsNone(report["model"])
            self.assertFalse(report["source_carrier_verified"])
        run, report = self.invoke(level1_box().replace(AP214, b"UNREGISTERED_SCHEMA"), FLAG)
        self.assertEqual(2, run.returncode, run.stderr or run.stdout)
        self.assertIsNone(report["model"])
        self.assertEqual("UnsupportedSchemaBinding", report["diagnostics"][0]["key"])

    def test_units_required_types_and_reference_integrity_still_apply(self):
        data = level1_box()
        for changed, expected in (
            (data.replace(b"SI_UNIT(.MILLI.,.METRE.)", b"SI_UNIT(.MILLI.,.RADIAN.)"), 1),
            (data.replace(b"MANIFOLD_SOLID_BREP", b"UNSUPPORTED_SOLID"), 2),
            (data.replace(b"#8,#20", b"#8,#999999"), 1),
        ):
            run, report = self.invoke(changed, FLAG)
            self.assertEqual(expected, run.returncode, run.stderr or run.stdout)
            self.assertIsNone(report["model"])
            self.assertFalse(report["source_carrier_verified"])
            self.assertTrue(report["source_inspection"]["source"]["carrier_interpretation"]["applied"])

    def test_general_interoperability_is_required_even_for_an_unused_option(self):
        for quality, interop in (("Q-P0-mm-01", False), ("Q-AP214-development-mm-01", False)):
            run, report = self.invoke(level1_box(), FLAG, quality=quality, interop=interop)
            self.assertEqual(2, run.returncode)
            self.assertIsNone(report)
            self.assertIn(b"Unverified carrier interpretation", run.stderr)

    def test_resource_and_report_limits_do_not_publish_partial_models(self):
        run, report = self.invoke(level1_box(), FLAG, "--max-native-calls", 1)
        self.assertEqual(3, run.returncode, run.stderr or run.stdout)
        self.assertIsNone(report["model"])
        self.assertFalse(report["source_carrier_verified"])
        run, report = self.invoke(level1_box(), FLAG, "--max-report-bytes", 1)
        self.assertEqual(5, run.returncode)
        self.assertIsNone(report)
