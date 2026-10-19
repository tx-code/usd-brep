# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit corrected-encoding assumption never rewrites a historical declaration."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP214, CORPUS, ap214_source

FLAG = "--interpret-level1-as-corrected"


def level1_box():
    data = ap214_source((CORPUS / "S203-P04/input.step").read_bytes())
    assert data.count(b"'2;1'") == 1
    return data.replace(b"'2;1'", b"'1'")


class CarrierLevel1Tests(unittest.TestCase):
    def invoke(self, data, *flags, source_only=False, general=True, interop=True):
        with tempfile.TemporaryDirectory(prefix="step-carrier-source-") as directory:
            source = Path(directory) / "source.step"
            source.write_bytes(data)
            variable = "STEP_INSPECT_BINARY" if source_only else "STEP_SEMANTIC_BINARY"
            command = [os.environ[variable], "--fixture-limits", str(source)]
            if not source_only:
                if general:
                    command.append("--general-brep-source")
                if interop:
                    command.append("--interop-nongeometric")
            run = subprocess.run(command + list(map(str, flags)), capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_same_original_needs_explicit_assumption_and_keeps_original_refusal(self):
        data = level1_box()
        strict, before = self.invoke(data, source_only=True)
        run, report = self.invoke(data, FLAG, source_only=True)
        self.assertEqual((2, 0), (strict.returncode, run.returncode), run.stderr)
        carrier = report["carrier_interpretation"]
        self.assertTrue(carrier["applied"])
        self.assertEqual("InterpretedUnverified", carrier["status"])
        self.assertEqual("1", carrier["declared_level"])
        self.assertFalse(carrier["declaration_verified"])
        self.assertEqual(before["diagnostics"], carrier["original_diagnostics"])
        self.assertEqual(data.decode(), report["input"]["raw"])
        self.assertEqual("1", report["header"][0]["parameters"][1]["text"])
        self.assertEqual("NotRun", report["native_build"])

    def test_semantic_success_is_qualified_and_schema_identity_is_independent(self):
        data = level1_box()
        run, report = self.invoke(data, FLAG)
        self.assertEqual(0, run.returncode, run.stderr or report)
        self.assertEqual("CheckedWithDiagnostics", report["status"])
        self.assertEqual("InterpretedUnverified", report["source"]["carrier_interpretation"]["status"])
        self.assertEqual(AP214.decode(), report["schema_selection"]["identifiers"][0]["literal"])
        self.assertEqual(data.decode(), report["source"]["input"]["raw"])
        self.assertEqual("NotRun", report["checks"]["native_build"])

    def test_corrected_declaration_does_not_gain_an_applied_assumption(self):
        data = level1_box().replace(b"'1'", b"'2;1'", 1)
        normal, before = self.invoke(data)
        run, report = self.invoke(data, FLAG)
        self.assertEqual((0, 0), (normal.returncode, run.returncode), run.stderr)
        self.assertEqual(before["status"], report["status"])
        self.assertEqual(before["snapshot"], report["snapshot"])
        self.assertFalse(report["source"]["carrier_interpretation"]["applied"])
        self.assertEqual([], report["source"]["carrier_interpretation"]["original_diagnostics"])

    def test_semantic_optin_requires_general_interoperability(self):
        for general, interop in ((False, False), (True, False), (False, True)):
            run, report = self.invoke(level1_box(), FLAG, general=general, interop=interop)
            self.assertEqual(64, run.returncode)
            self.assertIsNone(report)
            self.assertIn(b"Unverified carrier interpretation", run.stderr)

    def test_no_other_declaration_is_an_alias(self):
        for level in (b"2;2", b"3;1", b"4;1", b"", b"1;1", b"1 "):
            run, report = self.invoke(level1_box().replace(b"'1'", b"'" + level + b"'", 1), FLAG, source_only=True)
            self.assertEqual(2, run.returncode)
            self.assertFalse(report["carrier_interpretation"]["applied"])
            self.assertEqual("UnsupportedImplementationLevel", report["diagnostics"][0]["key"])

    def test_later_failure_keeps_assumption_without_a_partial_graph(self):
        data = level1_box().replace(b"#8,#20", b"#8,#999999")
        run, report = self.invoke(data, FLAG, source_only=True)
        self.assertEqual(1, run.returncode)
        self.assertFalse(report["source_complete"])
        self.assertTrue(report["carrier_interpretation"]["applied"])
        self.assertEqual("UnresolvedRequiredReference", report["diagnostics"][0]["key"])

    def test_unsupported_carrier_structure_and_raw_strings_remain_refused(self):
        data = level1_box()
        for changed, key in ((data.replace(b"DATA;", b"DATA('named',('AUTOMOTIVE_DESIGN'));"), "UnsupportedDataSection"),
                             (data.replace(b"END-ISO-10303-21;", b"DATA;ENDSEC;END-ISO-10303-21;"), "UnsupportedSection"),
                             (data.replace(b"ISO-10303-21;", b"ISO-10303-21;/*\x80*/", 1), "UnsupportedEncoding")):
            run, report = self.invoke(changed, FLAG, source_only=True)
            self.assertEqual(2, run.returncode)
            self.assertEqual(key, report["diagnostics"][0]["key"])

    def test_schema_units_types_and_report_budgets_still_apply(self):
        data = level1_box()
        run, report = self.invoke(data.replace(AP214, b"UNREGISTERED_SCHEMA"), FLAG)
        self.assertEqual(2, run.returncode)
        self.assertIsNone(report["snapshot"])
        self.assertEqual("UnsupportedSchemaBinding", report["diagnostics"][0]["key"])
        self.assertIn(b"SI_UNIT(.MILLI.,.METRE.)", data)
        units = data.replace(b"SI_UNIT(.MILLI.,.METRE.)", b"SI_UNIT(.MILLI.,.RADIAN.)")
        run, report = self.invoke(units, FLAG)
        self.assertEqual(1, run.returncode)
        self.assertIsNone(report["snapshot"])
        self.assertTrue(report["diagnostics"][0]["key"].startswith("SourceRuleViolation:"))
        bad = data.replace(b"MANIFOLD_SOLID_BREP", b"UNSUPPORTED_SOLID")
        run, report = self.invoke(bad, FLAG)
        self.assertEqual(2, run.returncode)
        self.assertIsNone(report["snapshot"])
        self.assertEqual("UnsupportedEntityType", report["diagnostics"][0]["key"])
        run, report = self.invoke(data, FLAG, "--max-report-bytes", 1, source_only=True)
        self.assertEqual(5, run.returncode)
        self.assertIsNone(report)
