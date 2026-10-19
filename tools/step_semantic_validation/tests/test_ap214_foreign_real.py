# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Borrowed N2635 scalar transport requires complete property/nonphysical proof."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP214, CORPUS, ap214_source
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX
from tools.step_semantic_validation.tests.test_ap214_entities import append_records
from tools.step_semantic_validation.tests.test_interpretation import UNREGISTERED

INTEROP = ("--general-brep-source", "--interop-nongeometric")


def real_item_source(value=b"3.5", label=b"'metric'", schema=MBX):
    data = ap214_source((CORPUS / "S203-P04/input.step").read_bytes()).replace(AP214, schema)
    return append_records(data, b"""#900=PROPERTY_DEFINITION('source scalar',$,#7);
#901=PROPERTY_DEFINITION_REPRESENTATION(#900,#902);
#902=REPRESENTATION('raw scalar',(#903),#904);
#903=REAL_REPRESENTATION_ITEM(""" + label + b"," + value + b""");
#904=REPRESENTATION_CONTEXT('property context','source metadata');
""")


class ForeignRealItemTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix="step-foreign-real-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), *flags],
                                 capture_output=True, timeout=20)
        self.assertTrue(run.stdout, run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(data.decode("ascii"), report["source"]["input"]["raw"])
        return run, report

    def checked(self, data, *extra):
        run, report = self.invoke(data, *INTEROP, *extra)
        self.assertEqual(0, run.returncode, run.stderr or report.get("diagnostics"))
        self.assertEqual("CheckedWithDiagnostics", report["status"])
        return report

    def test_explicit_property_preserves_foreign_type_and_effective_real_slot(self):
        for value, label in ((b"3.5", b"'metric'"), (b"-2.0", b"''"), (b"0.0", b"'metric'"),
                             (b"+0.3500E+1", b"'metric'"), (b"1.E99999", b"'large scalar'")):
            with self.subTest(value=value, label=label):
                data = real_item_source(value, label)
                strict, before = self.invoke(data)
                self.assertEqual(2, strict.returncode)
                report = self.checked(data)
                self.assertEqual(before["diagnostics"], report["preserved_diagnostics"])
                record = next(x for x in report["snapshot"]["records"] if x["id"] == "903")
                self.assertEqual({"REAL_REPRESENTATION_ITEM", "REPRESENTATION_ITEM", "REAL_LITERAL", "LITERAL_NUMBER",
                                  "SIMPLE_NUMERIC_EXPRESSION", "NUMERIC_EXPRESSION", "EXPRESSION", "GENERIC_EXPRESSION",
                                  "SIMPLE_GENERIC_EXPRESSION", "GENERIC_LITERAL"}, set(record["types"]))
                name, number = record["attributes"]
                self.assertEqual(("name", "REPRESENTATION_ITEM", "LABEL"),
                                 tuple(name[k] for k in ("name", "declared_in", "declared_type")))
                self.assertEqual(("the_value", "LITERAL_NUMBER", "REAL", "REAL_LITERAL", False),
                                 tuple(number[k] for k in
                                       ("name", "declared_in", "declared_type", "redeclared_by", "derived_slot")))
                self.assertEqual("904", report["snapshot"]["part"]["property_attachments"][0]["context"])
                raw = next(x for x in report["source"]["records"] if x["id"] == "903")
                self.assertEqual(value.decode(), raw["components"][0]["parameters"][1]["text"])

    def test_effective_real_does_not_accept_base_number_integer_or_derived_markers(self):
        for value, key in ((b"3", "AttributeTypeMismatch"), (b"$", "RequiredAttributeMissing"),
                           (b"*", "AttributeTypeMismatch"), (b"'3.5'", "AttributeTypeMismatch"),
                           (b"#904", "AttributeTypeMismatch"), (b"LENGTH_MEASURE(3.5)", "AttributeTypeMismatch"),
                           (b"3.5,*,*", "AttributeCountMismatch")):
            with self.subTest(value=value):
                run, report = self.invoke(real_item_source(value), *INTEROP)
                self.assertEqual(2, run.returncode)
                self.assertIsNone(report["snapshot"])
                issue = report["diagnostics"][0]
                self.assertEqual(key, issue["key"])
                self.assertEqual("ProfileUnsupported", issue["classification"])
                self.assertIn("N2635 borrowed", issue["responsibility"])

    def test_missing_label_is_not_fabricated_and_needs_the_same_property_proof(self):
        report = self.checked(real_item_source(label=b"$"))
        self.assertEqual(["UnsupportedEntityType", "RequiredAttributeMissing"],
                         [x["key"] for x in report["preserved_diagnostics"]])
        self.assertEqual("Unset", next(x for x in report["source"]["records"] if x["id"] == "903")
                         ["components"][0]["parameters"][0]["kind"])

    def test_only_explicit_general_interoperability_in_the_mbx_profile_can_admit_it(self):
        for flags in ((), ("--general-brep-source",), ("--interop-nongeometric",)):
            run, report = self.invoke(real_item_source(), *flags)
            self.assertEqual(2, run.returncode)
            self.assertIsNone(report["snapshot"])
            self.assertEqual("UnsupportedEntityType", report["diagnostics"][0]["key"])
        run, report = self.invoke(real_item_source(schema=AP214), *INTEROP)
        self.assertEqual(2, run.returncode)
        self.assertIsNone(report["snapshot"])

    def test_unowned_or_nonproperty_scalar_never_acquires_a_part_root(self):
        data = real_item_source().replace(b"#901=PROPERTY_DEFINITION_REPRESENTATION(#900,#902);\n", b"")
        run, report = self.invoke(data, *INTEROP)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["snapshot"])
        data = real_item_source().replace(b"'source scalar',$,#7", b"'source scalar',$,#10")
        run, report = self.invoke(data, *INTEROP)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["snapshot"])

    def test_numeric_math_parents_and_complex_mappings_stay_closed(self):
        for name in (b"REAL_LITERAL", b"LITERAL_NUMBER", b"NUMERIC_EXPRESSION"):
            run, report = self.invoke(real_item_source().replace(b"REAL_REPRESENTATION_ITEM", name), *INTEROP)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(report["snapshot"])

    def test_interpretation_and_foreign_diagnostics_share_the_existing_quota(self):
        data = real_item_source(schema=UNREGISTERED)
        report = self.checked(data, "--interpret-unregistered-ap214-2010")
        self.assertFalse(report["schema_interpretation"]["source_binding_verified"])
        self.assertEqual("UnsupportedSchemaBinding",
                         report["schema_interpretation"]["original_binding_diagnostics"][0]["key"])
        self.assertEqual("UnsupportedEntityType", report["preserved_diagnostics"][0]["key"])
        run, report = self.invoke(data, *INTEROP, "--interpret-unregistered-ap214-2010",
                                  "--max-preserved-diagnostics", "1")
        self.assertEqual(3, run.returncode)
        self.assertIsNone(report["snapshot"])
