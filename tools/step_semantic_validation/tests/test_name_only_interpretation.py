# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""A family name is not a verified AP edition."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_interpretation import UNREGISTERED, box_source

FLAG = "--interpret-ap214-name-only-as-2010"
REGISTERED = b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}"


def name_only_box():
    return box_source().replace(UNREGISTERED, b"AUTOMOTIVE_DESIGN")


class NameOnlyInterpretationTests(unittest.TestCase):
    def invoke(self, data, *flags, general=True, interop=True):
        with tempfile.TemporaryDirectory(prefix="step-name-only-source-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            args = [os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path)]
            if general:
                args.append("--general-brep-source")
            if interop:
                args.append("--interop-nongeometric")
            run = subprocess.run(args + list(map(str, flags)), capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_exact_name_only_requires_its_own_explicit_policy(self):
        data = name_only_box()
        strict, before = self.invoke(data)
        old, old_report = self.invoke(data, "--interpret-unregistered-ap214-2010")
        run, report = self.invoke(data, FLAG)
        self.assertEqual((2, 2, 0), (strict.returncode, old.returncode, run.returncode), run.stderr or run.stdout)
        self.assertFalse(old_report["schema_interpretation"]["applied"])
        self.assertEqual("CheckedWithDiagnostics", report["status"])
        self.assertEqual("Ap214NameOnlyAs2010", report["schema_interpretation"]["requested"])
        self.assertEqual(before["diagnostics"], report["schema_interpretation"]["original_binding_diagnostics"])
        identity = report["schema_selection"]["identifiers"][0]
        self.assertEqual("NameOnly", identity["match"])
        self.assertEqual([], identity["oid"])
        self.assertEqual("AUTOMOTIVE_DESIGN", identity["literal"])
        self.assertFalse(report["schema_selection"]["qualified_profile"]["header_identity_verified"])
        self.assertEqual("214-MBx2010-Source-0.1", report["schema_selection"]["qualified_profile"]["profile"])
        self.assertEqual(data.decode(), report["source"]["input"]["raw"])

    def test_no_other_identity_or_mapping_becomes_an_alias(self):
        for identifier in (UNREGISTERED, b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1 2}",
                           b"UNRELATED_SCHEMA", b"AP242_MANAGED_MODEL_BASED_3D_ENGINEERING_MIM_LF"):
            run, report = self.invoke(name_only_box().replace(b"'AUTOMOTIVE_DESIGN'", b"'" + identifier + b"'"), FLAG)
            self.assertEqual(2, run.returncode, run.stderr or run.stdout)
            self.assertFalse(report["schema_interpretation"]["applied"])
        data = name_only_box().replace(b"'AUTOMOTIVE_DESIGN'", b"'AUTOMOTIVE_DESIGN','OTHER_SCHEMA'")
        run, report = self.invoke(data, FLAG)
        self.assertEqual(2, run.returncode)
        self.assertFalse(report["schema_interpretation"]["applied"])

    def test_registered_identity_does_not_acquire_an_assumption(self):
        data = name_only_box().replace(b"'AUTOMOTIVE_DESIGN'", b"'" + REGISTERED + b"'")
        run, report = self.invoke(data, FLAG)
        self.assertEqual(0, run.returncode, run.stderr or run.stdout)
        self.assertFalse(report["schema_interpretation"]["applied"])
        self.assertEqual([], report["schema_interpretation"]["original_binding_diagnostics"])

    def test_scope_and_diagnostic_capacity_still_apply(self):
        for general, interop in ((False, False), (True, False), (False, True)):
            run, report = self.invoke(name_only_box(), FLAG, general=general, interop=interop)
            self.assertEqual(64, run.returncode)
            self.assertIsNone(report)
        run, report = self.invoke(name_only_box(), FLAG, "--max-preserved-diagnostics", 0)
        self.assertEqual(3, run.returncode)
        self.assertIsNone(report["snapshot"])
        self.assertFalse(report["schema_interpretation"]["applied"])

    def test_required_units_are_not_a_preservable_binding_exception(self):
        data = name_only_box().replace(b"SI_UNIT(.MILLI.,.METRE.)", b"SI_UNIT(.MILLI.,.RADIAN.)")
        run, report = self.invoke(data, FLAG)
        self.assertEqual(1, run.returncode)
        self.assertTrue(report["schema_interpretation"]["applied"])
        self.assertIsNone(report["snapshot"])

    def test_conflicting_policies_are_not_last_argument_wins(self):
        for flags in ((FLAG, "--interpret-unregistered-ap214-2010"),
                      ("--interpret-unregistered-ap214-2010", FLAG)):
            run, report = self.invoke(name_only_box(), *flags)
            self.assertEqual(64, run.returncode)
            self.assertIsNone(report)
