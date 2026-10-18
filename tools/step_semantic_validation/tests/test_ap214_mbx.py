# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Literal metadata attachment and override contracts for the MBx binding."""
import json
import os
import subprocess
import tempfile
from pathlib import Path
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP214, CORPUS, ap214_source
from tools.step_semantic_validation.tests.test_ap214_entities import append_records, presentation_source
from tools.step_semantic_validation.tests.test_ap214_property import property_source

MBX = b"AUTOMOTIVE_DESIGN {1 0 10303 214 1 1 1 1}"


def properties():
    return append_records(property_source().replace(AP214, MBX), b"""#950=GENERAL_PROPERTY('original id','material property',$);
#951=GENERAL_PROPERTY_ASSOCIATION('original association',$,#950,#900);
""")


def override():
    data = presentation_source().replace(AP214, MBX)
    data = data.replace(b"#901=STYLED_ITEM('source style',(#902),#21);",
                        b"#901=OVER_RIDING_STYLED_ITEM('source style',(#902),#21,#950);")
    return append_records(data, b"#950=STYLED_ITEM('original style',(#902),#21);\n")


class MbxMetadataTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-mbx-metadata-") as folder:
            path = Path(folder) / "input.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), *options],
                                 capture_output=True, timeout=20)
        self.assertTrue(run.stdout, run.stderr)
        return run.returncode, json.loads(run.stdout)

    def failure(self, data, key, code=1):
        actual, result = self.invoke(data)
        self.assertEqual(code, actual, result.get("diagnostics"))
        self.assertEqual(key, result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])

    def test_general_property_preserves_original_values_and_selected_owner(self):
        data = properties()
        code, result = self.invoke(data)
        self.assertEqual(0, code, result.get("diagnostics"))
        self.assertEqual(data.decode(), result["source"]["input"]["raw"])
        part = result["snapshot"]["part"]
        self.assertEqual("7", part["property_attachments"][0]["product_definition"])
        self.assertIn("951", part["roles"].values())
        self.assertEqual("12", part["roles"]["unit_context"])

    def test_name_conflict_is_a_source_rule_failure(self):
        self.failure(properties().replace(b"'original id','material property'", b"'original id','other name'"),
                     "SourceRuleViolation:GENERAL_PROPERTY_ASSOCIATION.WR2")

    def test_duplicate_derived_associations_are_not_silently_deduplicated(self):
        self.failure(append_records(properties(), b"#952=GENERAL_PROPERTY_ASSOCIATION('duplicate',$,#950,#900);\n"),
                     "SourceRuleViolation:GENERAL_PROPERTY_ASSOCIATION.WR1")

    def test_property_select_rejects_product_as_derived_property(self):
        self.failure(properties().replace(b"#950,#900)", b"#950,#4)"), "ReferenceTypeMismatch")

    def test_unattached_property_cannot_become_owned_through_general_association(self):
        data = append_records(properties().replace(b"#950,#900)", b"#950,#952)"),
                              b"#952=PROPERTY_DEFINITION('material property',$,#7);\n")
        self.failure(data, "UnsupportedGeneralPropertyAttachment", 2)

    def test_style_override_keeps_both_original_records(self):
        data = override()
        code, result = self.invoke(data)
        self.assertEqual(0, code, result.get("diagnostics"))
        self.assertEqual(data.decode(), result["source"]["input"]["raw"])
        self.assertEqual(["901"], result["snapshot"]["type_populations"]["OVER_RIDING_STYLED_ITEM"])
        self.assertEqual(["901", "950"], result["snapshot"]["type_populations"]["STYLED_ITEM"])

    def test_override_ancestor_cannot_introduce_unowned_geometry(self):
        data = append_records(override().replace(b"#950=STYLED_ITEM('original style',(#902),#21)",
                                                b"#950=STYLED_ITEM('original style',(#902),#951)"),
                              b"#951=CARTESIAN_POINT('unowned',(0.,0.,0.));\n")
        self.failure(data, "UnsupportedPresentationAttachment", 2)

    def test_override_cycle_is_explicitly_unsupported(self):
        self.failure(override().replace(b"#21,#950)", b"#21,#901)"), "UnsupportedPresentationOverrideCycle", 2)

    def test_detached_override_cycles_are_not_accepted_as_metadata_residue(self):
        data = override()
        line = next(x for x in data.splitlines(keepends=True) if x.startswith(b"#900=MECHANICAL_DESIGN_"))
        data = data.replace(line, b"")
        data = data.replace(b"#21,#950)", b"#21,#901)")
        data = data.replace(b"#950=STYLED_ITEM('original style',(#902),#21);\n", b"")
        code, result = self.invoke(data, "--general-brep-source", "--interop-nongeometric")
        self.assertEqual(2, code, result.get("diagnostics"))
        self.assertEqual("UnsupportedPresentationOverrideCycle", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])

    def test_layer_is_attached_only_to_already_owned_items(self):
        data = append_records(properties(), b"#952=PRESENTATION_LAYER_ASSIGNMENT('source layer','unchanged',(#21));\n")
        code, result = self.invoke(data)
        self.assertEqual(0, code, result.get("diagnostics"))
        self.assertIn("952", result["snapshot"]["part"]["roles"].values())
        self.assertEqual(data.decode(), result["source"]["input"]["raw"])
        bad = append_records(data.replace(b"'unchanged',(#21)", b"'unchanged',(#953)"),
                             b"#953=CARTESIAN_POINT('outside body',(0.,0.,0.));\n")
        self.failure(bad, "UnsupportedAdministrativeAttachment", 2)

    def test_invalid_shell_override_remains_diagnostic_under_explicit_interoperability(self):
        old = override()
        added = old[old.index(b"#900=MECHANICAL_DESIGN_"):old.index(b"ENDSEC;", old.index(b"#900="))]
        added = added.replace(b"#21,#950)", b"#21,#22)")
        added = added.replace(b"#950=STYLED_ITEM('original style',(#902),#21);\n", b"")
        data = append_records(ap214_source((CORPUS / "S203-P04/input.step").read_bytes()).replace(AP214, MBX), added)
        for options in ((), ("--general-brep-source",), ("--interop-nongeometric",)):
            with self.subTest(options=options):
                code, result = self.invoke(data, *options)
                self.assertEqual(1, code)
                self.assertEqual("ReferenceTypeMismatch", result["diagnostics"][0]["key"])
                self.assertIsNone(result["snapshot"])
        code, result = self.invoke(data, "--general-brep-source", "--interop-nongeometric")
        self.assertEqual(0, code, result.get("diagnostics"))
        self.assertEqual(data.decode(), result["source"]["input"]["raw"])
        issue, = result["preserved_diagnostics"]
        self.assertEqual(("ReferenceTypeMismatch", "901", "over_ridden_style", "22"),
                         tuple(issue[k] for k in ("key", "record_id", "field", "target_id")))
        self.assertEqual("MaterialSolid", result["snapshot"]["part"]["source_shape_role"])
        self.assertEqual("CheckedWithDiagnostics", result["checks"]["type_fields"])
        code, rejected = self.invoke(data.replace(b"#21,#22)", b"#21,#7)"),
                                     "--general-brep-source", "--interop-nongeometric")
        self.assertEqual(1, code)
        self.assertEqual("ReferenceTypeMismatch", rejected["diagnostics"][0]["key"])

    def test_missing_apd_is_unavailable_metadata_not_an_invented_declaration(self):
        data = properties()
        line = next(x for x in data.splitlines(keepends=True) if x.startswith(b"#2=APPLICATION_PROTOCOL_DEFINITION("))
        data = data.replace(line, b"")
        code, strict = self.invoke(data, "--general-brep-source")
        self.assertEqual(2, code)
        self.assertEqual("UnsupportedSinglePartGraph", strict["diagnostics"][0]["key"])
        self.assertEqual("APPLICATION_PROTOCOL_DEFINITION count=1", strict["diagnostics"][0]["expected"])
        code, result = self.invoke(data, "--general-brep-source", "--interop-nongeometric")
        self.assertEqual(0, code, result.get("diagnostics"))
        self.assertEqual(data.decode(), result["source"]["input"]["raw"])
        self.assertEqual(strict["diagnostics"], result["preserved_diagnostics"])
        part = result["snapshot"]["part"]
        self.assertNotIn("application_protocol", part["roles"])
        self.assertIsNone(part["apd_status"])
        self.assertIsNone(part["apd_schema_claim"])
        self.assertIsNone(part["apd_year_lexeme"])
        self.assertEqual("NotRun", part["apd_global_rule"])
        # Multiple declarations are ambiguous, and missing physical units remain fatal.
        duplicated = data.replace(b"DATA;", b"DATA;\n" + line + line.replace(b"#2=", b"#990="))
        code, rejected = self.invoke(duplicated, "--general-brep-source", "--interop-nongeometric")
        self.assertEqual(2, code)
        self.assertEqual("UnsupportedSinglePartGraph", rejected["diagnostics"][0]["key"])
        self.assertIsNone(rejected["snapshot"])
