# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Typed source admission requires exact nondefining fields and proven owners."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_import_native_validation.tests.test_opaque_data import opaque_source


class OpaqueSemanticTests(unittest.TestCase):
    def invoke(self, data, *args):
        with tempfile.TemporaryDirectory(prefix="step-opaque-semantics-") as folder:
            path = Path(folder) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path),
                                  "--preserve-opaque-data-strings", *args], capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_selected_product_and_attached_layer_keep_unknown_text(self):
        for field, count in [("product", 2), ("layer", 1)]:
            with self.subTest(field=field):
                data = opaque_source(field)
                run, result = self.invoke(data, "--interop-nongeometric", "--general-brep-source")
                self.assertEqual(0, run.returncode, run.stderr or result)
                self.assertEqual("CheckedWithDiagnostics", result["status"])
                self.assertEqual("CheckedWithDiagnostics", result["checks"]["type_fields"])
                self.assertEqual("Passed", result["checks"]["unit_dependencies"])
                diagnostics = [d for d in result["preserved_diagnostics"] if d["key"] == "OpaqueDataTextUnestablished"]
                self.assertEqual(count, len(diagnostics))
                self.assertTrue(all(d["classification"] == "ProfileUnsupported" for d in diagnostics))
                self.assertEqual(data, result["source"]["input"]["raw"].encode("latin1"))
                self.assertIsNotNone(result["snapshot"])

    def test_strict_semantics_does_not_turn_unavailable_text_into_a_source_value(self):
        run, result = self.invoke(opaque_source(), "--general-brep-source")
        self.assertEqual(2, run.returncode, run.stderr or result)
        self.assertEqual("OpaqueDataTextUnestablished", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])

    def test_descriptions_predefined_names_and_protocol_claims_are_not_whitelisted(self):
        original = opaque_source("product").replace(b"'\x80\xfe','\x81\xff'", b"'P0-PART','P0 analytic fixture'")
        variants = [opaque_source("defining"), opaque_source("representation"),
                    original.replace(b"'P0-PART','P0 analytic fixture',$", b"'P0-PART','P0 analytic fixture','\x80'"),
                    original.replace(b"#100=CARTESIAN_POINT(''", b"#100=CARTESIAN_POINT('\x80'")]
        for source in variants:
            with self.subTest(kind="unapproved field"):
                run, result = self.invoke(source, "--interop-nongeometric", "--general-brep-source")
                self.assertEqual(2, run.returncode, run.stderr or result)
                self.assertEqual("OpaqueDataTextUnestablished", result["diagnostics"][0]["key"])
                self.assertIsNone(result["snapshot"])

    def test_preserved_diagnostic_and_visit_limits_still_bound_opaque_text(self):
        for args, key in [(("--max-preserved-diagnostics", "0"), "PreservedDiagnosticLimit"),
                          (("--max-semantic-visits", "1"), "DependencyVisitLimit")]:
            run, result = self.invoke(opaque_source(), "--interop-nongeometric", "--general-brep-source", *args)
            self.assertNotEqual(0, run.returncode)
            self.assertEqual(key, result["diagnostics"][0]["key"])
            self.assertIsNone(result["snapshot"])
