# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Pinned AP214 appearance fields, exact predicates and ownership through the real CLI."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214, CORPUS, ap214_source
from tools.step_semantic_validation.tests.test_ap214_entities import append_records
from tools.step_semantic_validation.tests.test_ap214_entities import presentation_source

MBX = b"AUTOMOTIVE_DESIGN {1 0 10303 214 1 1 1 1}"
INTEROP = ("--general-brep-source", "--interop-nongeometric")


def curve_style_source(width=b"POSITIVE_LENGTH_MEASURE(+0.125E0)", font=b"'continuous'", schema=AP214):
    data = ap214_source((CORPUS / "S203-P04/input.step").read_bytes()).replace(AP214, schema)
    return append_records(data, b"""#950=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('display',(#951),#12);
#951=STYLED_ITEM('outline',(#952),#21);
#952=PRESENTATION_STYLE_ASSIGNMENT((#953));
#953=CURVE_STYLE('source width',#954,""" + width + b""",#955);
#954=DRAUGHTING_PRE_DEFINED_CURVE_FONT(""" + font + b""");
#955=COLOUR_RGB('RGB',0.2,0.4,0.6);
""")


class CurveStyleTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-curve-style-") as directory:
            path = Path(directory) / "input.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), *map(str, options)],
                                 capture_output=True, timeout=20)
        self.assertTrue(run.stdout, run.stderr)
        return run, json.loads(run.stdout)

    def checked(self, data, *options):
        run, report = self.invoke(data, *options)
        self.assertEqual(0, run.returncode, run.stderr or report.get("diagnostics"))
        self.assertEqual(data.decode("ascii"), report["source"]["input"]["raw"])
        return report

    def rejected(self, data, key, code=1, *options):
        run, report = self.invoke(data, *options)
        self.assertEqual(code, run.returncode, run.stderr or report.get("diagnostics"))
        self.assertIsNone(report["snapshot"])
        self.assertEqual(key, report["diagnostics"][0]["key"])
        self.assertEqual([], report.get("preserved_diagnostics", []))
        return report

    def test_both_bindings_keep_exact_font_names_and_inherited_field_identity(self):
        for schema in (AP214, MBX):
            for name in (b"continuous", b"chain", b"chain double dash", b"dashed", b"dotted"):
                with self.subTest(schema=schema, name=name):
                    data = curve_style_source(font=b"'" + name + b"'", schema=schema)
                    report = self.checked(data)
                    font = next(r for r in report["snapshot"]["records"] if r["id"] == "954")
                    (attribute,) = font["attributes"]
                    self.assertEqual(("name", "PRE_DEFINED_ITEM", "LABEL"),
                                     (attribute["name"], attribute["declared_in"], attribute["declared_type"]))
                    self.assertEqual("NotRun", report["checks"]["global_rules_47"])

    def test_size_select_retains_each_actual_branch_and_nominal_unit(self):
        for width, definition in (
            (b"POSITIVE_LENGTH_MEASURE(1.E1000000)", b""),
            (b"DESCRIPTIVE_MEASURE('by context')", b""),
            (b"#956", b"#956=MEASURE_WITH_UNIT(POSITIVE_LENGTH_MEASURE(0.25),#10);\n"),
            (b"#956", b"#956=MEASURE_WITH_UNIT(RATIO_MEASURE(0.25),#11);\n"),
            (b"#956", b"#956=MEASURE_WITH_UNIT(NON_NEGATIVE_LENGTH_MEASURE(0.),#10);\n"),
        ):
            with self.subTest(width=width, definition=definition):
                data = curve_style_source(width)
                if definition:
                    data = append_records(data, definition)
                report = self.checked(data)
                record = next(r for r in report["source"]["records"] if r["id"] == "953")
                value = record["components"][0]["parameters"][2]
                begin, end = value["span"]
                self.assertEqual(width, data[begin:end])
                style = next(r for r in report["snapshot"]["records"] if r["id"] == "953")
                bound = next(a for a in style["attributes"] if a["name"] == "curve_width")
                self.assertEqual("SIZE_SELECT", bound["declared_type"])
                if definition:
                    self.assertIn("956", report["snapshot"]["type_populations"]["MEASURE_WITH_UNIT"])
                    self.assertNotIn("956", report["snapshot"]["type_populations"]["LENGTH_MEASURE_WITH_UNIT"])

    def test_required_fields_and_exact_font_rule_are_not_defaults(self):
        for before in (b"'source width'", b"#954", b"POSITIVE_LENGTH_MEASURE(+0.125E0)", b"#955"):
            data = curve_style_source().replace(before, b"$", 1)
            self.rejected(data, "RequiredAttributeMissing")
        for name in (b"'Continuous'", b"'unknown'", b"''"):
            self.rejected(curve_style_source(font=name), "SourceRuleViolation:DRAUGHTING_PRE_DEFINED_CURVE_FONT.WR1")
        self.rejected(curve_style_source(font=b"$"), "RequiredAttributeMissing", 1, *INTEROP)
        self.checked(curve_style_source(font=b"'\\X2\\006400610073006800650064\\X0\\'"))

    def test_wrong_width_reference_and_nonmember_typed_branches_are_rejected(self):
        report = self.rejected(curve_style_source(b"#955"), "ReferenceTypeMismatch")
        self.assertEqual("955", report["diagnostics"][0]["target_id"])
        for width in (b"1.", b"LENGTH_MEASURE(1.)", b"DESCRIPTIVE_MEASURE(1.)", b"POSITIVE_LENGTH_MEASURE('x')"):
            self.rejected(curve_style_source(width), "AttributeTypeMismatch")
        self.rejected(curve_style_source().replace(b"#954,", b"#955,", 1), "ReferenceTypeMismatch")

    def test_width_sign_and_referenced_measure_units_are_checked_without_float_rounding(self):
        for value in (b"0.", b"-0.", b"+0.E-99999", b"-1.E-99999"):
            self.rejected(curve_style_source(b"POSITIVE_LENGTH_MEASURE(" + value + b")"),
                          "SourceRuleViolation:POSITIVE_LENGTH_MEASURE.WR1")
        for value, unit, key in ((b"-1.", b"#10", "SourceRuleViolation:POSITIVE_LENGTH_MEASURE.WR1"),
                                 (b"1.", b"#11", "SourceRuleViolation:MEASURE_WITH_UNIT.WR1")):
            data = append_records(curve_style_source(b"#956"),
                                  b"#956=MEASURE_WITH_UNIT(POSITIVE_LENGTH_MEASURE(" + value + b")," + unit + b");\n")
            self.rejected(data, key)

    def test_curve_style_colour_and_assignment_constraints_are_checked(self):
        self.rejected(curve_style_source().replace(b"0.2,0.4,0.6", b"0.2,1.1,0.6"), "SourceRuleViolation:COLOUR_RGB.WR2")
        data = append_records(curve_style_source(), b"#956=CURVE_STYLE('second',#954,POSITIVE_LENGTH_MEASURE(1.),#955);\n")
        self.rejected(data.replace(b"((#953))", b"((#953,#956))"), "SourceRuleViolation:PRESENTATION_STYLE_ASSIGNMENT.WR1")
        data = append_records(curve_style_source(), b"#956=PRESENTATION_STYLE_ASSIGNMENT((#953));\n")
        data = data.replace(b"'outline',(#952),#21", b"'outline',(#952,#956),#202")
        self.rejected(data, "SourceRuleViolation:styled_curve.WR1")

    def test_styled_curve_requires_a_curve_assignment_even_with_no_curve_style_records(self):
        data = presentation_source().replace(b"'source style',(#902),#21", b"'source style',(#902),#202")
        self.rejected(data, "SourceRuleViolation:styled_curve.WR1")

    def test_invalid_string_width_is_preserved_only_with_explicit_selected_presentation_proof(self):
        data = curve_style_source(b"'POSITIVE_LENGTH_MEASURE(0.)'")
        strict = self.rejected(data, "AttributeTypeMismatch")
        report = self.checked(data, *INTEROP)
        self.assertEqual("CheckedWithDiagnostics", report["status"])
        self.assertEqual("CheckedWithDiagnostics", report["checks"]["type_fields"])
        self.assertEqual("Passed", report["checks"]["unit_dependencies"])
        self.assertEqual(strict["diagnostics"], report["preserved_diagnostics"])
        raw = next(r for r in report["source"]["records"] if r["id"] == "953")["components"][0]["parameters"][2]
        self.assertEqual("String", raw["kind"])
        self.rejected(data, "AttributeTypeMismatch", 1, "--interop-nongeometric")
        self.rejected(data, "AttributeTypeMismatch", 1, "--general-brep-source")
        detached = data.replace(b"#950=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('display',(#951),#12);\n", b"")
        self.rejected(detached, "AttributeTypeMismatch", 1, *INTEROP)
        foreign = append_records(data.replace(b"'outline',(#952),#21", b"'outline',(#952),#980"),
                                 b"#980=CARTESIAN_POINT('not in selected body',(100.,200.,300.));\n")
        self.rejected(foreign, "UnsupportedPresentationAttachment", 2, *INTEROP)
        self.rejected(data, "PreservedDiagnosticLimit", 3, *INTEROP, "--max-preserved-diagnostics", 0)

    def test_new_font_declarations_do_not_escape_the_ap214_profile(self):
        self.rejected(curve_style_source().replace(b"DRAUGHTING_PRE_DEFINED_CURVE_FONT", b"PRE_DEFINED_CURVE_FONT"),
                      "UnsupportedEntityMapping", 2)
        for type_name in (b"CURVE_STYLE_FONT", b"CURVE_STYLE_FONT_AND_SCALING", b"EXTERNALLY_DEFINED_CURVE_FONT"):
            self.rejected(curve_style_source().replace(b"DRAUGHTING_PRE_DEFINED_CURVE_FONT", type_name),
                          "UnsupportedEntityType", 2)

    def test_shared_catalog_does_not_add_curve_style_to_p0_or_n2635(self):
        raw = (CORPUS / "S203-P04/input.step").read_bytes()
        for options in ((), ("--general-brep-source",)):
            with self.subTest(options=options):
                report = self.checked(raw, *options)
                self.assertNotIn("CURVE_STYLE", report["snapshot"]["type_populations"])
                data = curve_style_source().replace(AP214, AP203).replace(
                    b"'automotive_design',2010",
                    b"'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011")
                self.rejected(data, "UnsupportedEntityType", 2, *options)
