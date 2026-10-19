# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Typed property values retain their literal and every using unit context."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214, CORPUS, ap214_source
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX
from tools.step_semantic_validation.tests.test_ap214_entities import append_records


def value_source(value=b"COUNT_MEASURE(0)", schema=AP214):
    data = (CORPUS / "S203-P04/input.step").read_bytes()
    if schema != AP203:
        data = ap214_source(data).replace(AP214, schema)
    return append_records(data, b"#900=PROPERTY_DEFINITION('source quantity',$,#7);\n"
                          b"#901=VALUE_REPRESENTATION_ITEM('raw value'," + value + b");\n"
                          b"#902=REPRESENTATION('source values',(#901),#12);\n"
                          b"#903=PROPERTY_DEFINITION_REPRESENTATION(#900,#902);\n")


class ValueItemTests(unittest.TestCase):
    def invoke(self, data, *flags, general=True):
        with tempfile.TemporaryDirectory(prefix="step-value-item-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            command = [os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path)]
            if general:
                command.append("--general-brep-source")
            run = subprocess.run([*command, *map(str, flags)], capture_output=True, timeout=20)
        self.assertTrue(run.stdout, run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(data.decode(), report["source"]["input"]["raw"])
        return run, report

    def checked(self, data):
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        self.assertEqual("Checked", report["status"])
        return report

    def test_two_ap214_profiles_preserve_number_lexemes_and_owner_context(self):
        for schema in (AP214, MBX):
            for number in (b"0", b"-7", b"+0.000E+0", b"123456789012345678901234567890"):
                with self.subTest(schema=schema, number=number):
                    report = self.checked(value_source(b"COUNT_MEASURE(" + number + b")", schema))
                    record = next(x for x in report["snapshot"]["records"] if x["id"] == "901")
                    self.assertEqual(["name", "value_component"], [x["name"] for x in record["attributes"]])
                    self.assertEqual("VALUE_REPRESENTATION_ITEM", record["attributes"][1]["declared_in"])
                    raw = next(x for x in report["source"]["records"] if x["id"] == "901")
                    binding = record["attributes"][1]
                    scalar = raw["components"][binding["component_index"]]["parameters"][binding["parameter_index"]]
                    self.assertEqual(binding["span"], scalar["span"])
                    self.assertEqual("COUNT_MEASURE", scalar["text"])
                    self.assertEqual(number.decode(), scalar["children"][0]["text"])
                    part = report["snapshot"]["part"]
                    self.assertEqual("12", part["property_attachments"][0]["context"])
                    self.assertEqual("12", part["roles"]["unit_context"])

    def test_full_fixed_measure_select_preserves_scalar_kind_without_conversion(self):
        real_types = ("AMOUNT_OF_SUBSTANCE_MEASURE", "AREA_MEASURE", "CELSIUS_TEMPERATURE_MEASURE",
                      "CONTEXT_DEPENDENT_MEASURE", "ELECTRIC_CURRENT_MEASURE", "LENGTH_MEASURE",
                      "LUMINOUS_INTENSITY_MEASURE", "MASS_MEASURE", "NON_NEGATIVE_LENGTH_MEASURE",
                      "PARAMETER_VALUE", "PLANE_ANGLE_MEASURE", "POSITIVE_LENGTH_MEASURE",
                      "POSITIVE_PLANE_ANGLE_MEASURE", "POSITIVE_RATIO_MEASURE", "RATIO_MEASURE",
                      "SOLID_ANGLE_MEASURE", "THERMODYNAMIC_TEMPERATURE_MEASURE", "TIME_MEASURE", "VOLUME_MEASURE")
        for tag in real_types:
            with self.subTest(tag=tag):
                self.checked(value_source((tag + "(+1.2500E-2)").encode()))
        for value in (b"COUNT_MEASURE(-1)", b"NUMERIC_MEASURE(-3)", b"NUMERIC_MEASURE(-3.)",
                      b"DESCRIPTIVE_MEASURE('literal source text')"):
            self.checked(value_source(value))

    def test_scalar_shape_errors_and_unknown_select_branches_are_distinct(self):
        for value, status, key in ((b"COUNT_MEASURE('x')", 1, "AttributeTypeMismatch"),
                                   (b"LENGTH_MEASURE(1)", 1, "AttributeTypeMismatch"),
                                   (b"DESCRIPTIVE_MEASURE(1.)", 1, "AttributeTypeMismatch"),
                                   (b"UNKNOWN_MEASURE(1.)", 2, "UnsupportedMeasureBranch")):
            run, report = self.invoke(value_source(value))
            self.assertEqual(status, run.returncode, report["diagnostics"])
            self.assertEqual(key, report["diagnostics"][0]["key"])
            self.assertIsNone(report["snapshot"])

    def test_positive_and_nonnegative_type_predicates_apply_without_explicit_unit_slot(self):
        for schema in (AP214, MBX, AP203):
            tags = ("POSITIVE_LENGTH_MEASURE", "NON_NEGATIVE_LENGTH_MEASURE")
            for tag in tags:
                run, report = self.invoke(value_source((tag + "(-1.)").encode(), schema))
                self.assertEqual(1, run.returncode, report["diagnostics"])
                self.assertEqual("SourceRuleViolation:" + tag + ".WR1", report["diagnostics"][0]["key"])
        for tag in ("POSITIVE_RATIO_MEASURE", "POSITIVE_PLANE_ANGLE_MEASURE"):
            run, report = self.invoke(value_source((tag + "(-0.)").encode()))
            self.assertEqual(1, run.returncode, report["diagnostics"])
            self.assertEqual("SourceRuleViolation:" + tag + ".WR1", report["diagnostics"][0]["key"])

    def test_direct_and_shared_uses_require_global_assignment_even_in_interop(self):
        original = value_source()
        direct = append_records(original.replace(b"'source values',(#901),#12", b"'source values',(#901),#904"),
                                b"#904=REPRESENTATION_CONTEXT('metadata','units absent');\n")
        shared = append_records(original, b"#904=REPRESENTATION_CONTEXT('metadata','units absent');\n"
                                b"#905=REPRESENTATION('another use',(#901),#904);\n")
        for data in (direct, shared):
            run, report = self.invoke(data, "--interop-nongeometric")
            self.assertEqual(1, run.returncode, report["diagnostics"])
            self.assertEqual("SourceRuleViolation:VALUE_REPRESENTATION_ITEM.WR1", report["diagnostics"][0]["key"])
            self.assertIsNone(report["snapshot"])

    def test_indirect_ri_use_is_included_in_fixed_using_representations_rule(self):
        data = append_records(value_source(), b"""#904=REPRESENTATION_CONTEXT('metadata','units absent');
#905=REPRESENTATION('indirect use',(#910),#904);
#910=STYLED_ITEM('',(#911),#901);
#911=PRESENTATION_STYLE_ASSIGNMENT((#912));
#912=FILL_AREA_STYLE('',(#913));
#913=FILL_AREA_STYLE_COLOUR('',#914);
#914=COLOUR_RGB('',0.1,0.2,0.3);
""")
        run, report = self.invoke(data)
        self.assertEqual(1, run.returncode, report["diagnostics"])
        self.assertEqual("SourceRuleViolation:VALUE_REPRESENTATION_ITEM.WR1", report["diagnostics"][0]["key"])

    def test_unit_context_is_preserved_without_guessing_property_scale(self):
        data = append_records(value_source(b"LENGTH_MEASURE(+0.025400E+3)").replace(
            b"'source values',(#901),#12", b"'source values',(#901),#904"),
            b"#904=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#905,#11)) "
            b"REPRESENTATION_CONTEXT('property','cm'));\n"
            b"#905=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.CENTI.,.METRE.));\n")
        report = self.checked(data)
        self.assertEqual("904", report["snapshot"]["part"]["property_attachments"][0]["context"])
        self.assertEqual({"kind": "rational", "num": "1", "den": "1"}, report["snapshot"]["units"]["length_scale_mm"])

    def test_exact_visit_budget_remains_bounded(self):
        data = value_source()
        report = self.checked(data)
        visits = report["statistics"]["dependency_visits"]
        run, report = self.invoke(data, "--max-semantic-visits", visits)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        run, report = self.invoke(data, "--max-semantic-visits", visits - 1)
        self.assertEqual("DependencyVisitLimit", report["diagnostics"][0]["key"])
        self.assertIsNone(report["snapshot"])

    def test_original_p0_scope_does_not_acquire_value_items(self):
        data = append_records((CORPUS / "S203-P04/input.step").read_bytes(),
                              b"#901=VALUE_REPRESENTATION_ITEM('count',COUNT_MEASURE(0));\n")
        run, report = self.invoke(data, general=False)
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedEntityType", report["diagnostics"][0]["key"])
        self.assertIsNone(report["snapshot"])
