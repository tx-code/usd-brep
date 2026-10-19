# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent fixed-EXP palette, field provenance and appearance-ownership controls."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX, curve_style_source
from tools.step_semantic_validation.tests.test_ap214_entities import append_records, presentation_source
from tools.step_semantic_validation.tests.test_interpretation import UNREGISTERED


def colour_source(name=b"'red'", schema=AP214, style="fill"):
    if style == "curve":
        return curve_style_source(schema=schema).replace(
            b"COLOUR_RGB('RGB',0.2,0.4,0.6)", b"DRAUGHTING_PRE_DEFINED_COLOUR(" + name + b")")
    data = presentation_source().replace(AP214, schema).replace(
        b"COLOUR_RGB('RGB',0.25,0.5,0.75)", b"DRAUGHTING_PRE_DEFINED_COLOUR(" + name + b")")
    if style == "render":
        data = data.replace(b"SURFACE_STYLE_FILL_AREA(#906)", b"SURFACE_STYLE_RENDERING(.NORMAL_SHADING.,#908)")
        data = data.replace(b"#906=FILL_AREA_STYLE('source fill',(#907));\n", b"")
        data = data.replace(b"#907=FILL_AREA_STYLE_COLOUR('source colour',#908);\n", b"")
    return data


class PredefinedColourTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-predefined-colour-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), *options],
                                 capture_output=True, timeout=20)
        self.assertTrue(run.stdout, run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(data.decode("ascii"), report["source"]["input"]["raw"])
        return run, report

    def checked(self, data, *options):
        run, report = self.invoke(data, *options)
        self.assertEqual(0, run.returncode, run.stderr or report.get("diagnostics"))
        return report

    def failed(self, data, key, code=1, *options):
        run, report = self.invoke(data, *options)
        self.assertEqual(code, run.returncode, run.stderr or report.get("diagnostics"))
        self.assertIsNone(report["snapshot"])
        self.assertEqual(key, report["diagnostics"][0]["key"])
        return report

    def test_eight_exact_names_and_multiple_inheritance_keep_one_original_name_slot(self):
        # Transcribed from AP214E3_2010.exp DRAUGHTING_PRE_DEFINED_COLOUR.WR1,
        # independently of product tables. No RGB conversion is implied.
        for schema in (AP214, MBX):
            for name in (b"red", b"green", b"blue", b"yellow", b"magenta", b"cyan", b"black", b"white"):
                with self.subTest(schema=schema, name=name):
                    report = self.checked(colour_source(b"'" + name + b"'", schema))
                    colour = next(x for x in report["snapshot"]["records"] if x["id"] == "908")
                    self.assertEqual({"COLOUR", "PRE_DEFINED_ITEM", "PRE_DEFINED_COLOUR",
                                      "DRAUGHTING_PRE_DEFINED_COLOUR"}, set(colour["types"]))
                    (field,) = colour["attributes"]
                    self.assertEqual(("name", "PRE_DEFINED_ITEM", "LABEL", False, False),
                                     (field["name"], field["declared_in"], field["declared_type"],
                                      field["optional"], field["derived_slot"]))
                    self.assertFalse(colour["used_by_representation"])
                    self.assertEqual("NotRun", report["checks"]["global_rules_47"])
                    self.assertEqual("20", report["snapshot"]["part"]["roles"]["representation"])

    def test_fill_rendering_and_curve_fields_retain_their_colour_reference_type(self):
        for style, owner, field, colour_id in (("fill", "907", "fill_colour", "908"),
                                                ("render", "905", "surface_colour", "908"),
                                                ("curve", "953", "curve_colour", "955")):
            with self.subTest(style=style):
                report = self.checked(colour_source(style=style))
                records = {x["id"]: x for x in report["snapshot"]["records"]}
                attribute = next(x for x in records[owner]["attributes"] if x["name"] == field)
                self.assertEqual("COLOUR", attribute["declared_type"])
                self.assertIn(colour_id, report["snapshot"]["type_populations"]["PRE_DEFINED_COLOUR"])

    def test_shared_colour_and_decoded_escape_preserve_original_source(self):
        data = append_records(colour_source(b"'\\X2\\007200650064\\X0\\'"),
                              b"#909=FILL_AREA_STYLE_COLOUR('second use',#908);\n")
        data = data.replace(b"'source fill',(#907)", b"'source fill',(#907,#909)")
        self.checked(data)

    def test_palette_predicate_is_exact_and_cannot_be_isolated_as_invalid_metadata(self):
        for name in (b"'RED'", b"'grey'", b"''"):
            for options in ((), ("--general-brep-source", "--interop-nongeometric")):
                with self.subTest(name=name, options=options):
                    data = colour_source(name)
                    report = self.failed(data, "SourceRuleViolation:DRAUGHTING_PRE_DEFINED_COLOUR.WR1", 1, *options)
                    diagnostic = report["diagnostics"][0]
                    self.assertEqual(("908", "name", "PRE_DEFINED_ITEM", "LABEL"),
                                     tuple(diagnostic[k] for k in
                                           ("record_id", "field", "declared_in", "declared_type")))
                    begin, end = diagnostic["span"]
                    self.assertEqual(name, data[begin:end])
                    self.assertEqual("AP214E3_2010.exp DRAUGHTING_PRE_DEFINED_COLOUR.WR1",
                                     diagnostic["responsibility"])

    def test_missing_wrong_type_and_arity_never_create_a_default_colour(self):
        for value, key in ((b"$", "RequiredAttributeMissing"), (b"*", "AttributeTypeMismatch"),
                           (b".RED.", "AttributeTypeMismatch"), (b"'red','extra'", "AttributeCountMismatch")):
            with self.subTest(value=value):
                self.failed(colour_source(value), key, 1, "--general-brep-source", "--interop-nongeometric")

    def test_bare_parents_and_unregistered_complex_mappings_stay_unsupported(self):
        for parent in (b"PRE_DEFINED_ITEM", b"PRE_DEFINED_COLOUR"):
            data = colour_source().replace(b"DRAUGHTING_PRE_DEFINED_COLOUR('red')", parent + b"('red')")
            self.failed(data, "UnsupportedEntityMapping", 2)
        data = colour_source().replace(b"DRAUGHTING_PRE_DEFINED_COLOUR('red')",
                                      b"(COLOUR() DRAUGHTING_PRE_DEFINED_COLOUR() "
                                      b"PRE_DEFINED_COLOUR() PRE_DEFINED_ITEM('red'))")
        self.failed(data, "UnsupportedComplexSignature", 2)

    def test_colour_is_not_a_presentation_style_or_a_geometry_root(self):
        data = colour_source().replace(b"PRESENTATION_STYLE_ASSIGNMENT((#903))",
                                       b"PRESENTATION_STYLE_ASSIGNMENT((#908))")
        self.failed(data, "ReferenceTypeMismatch")
        detached = colour_source().replace(b"#900=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION",
                                           b"#900=SHAPE_REPRESENTATION")
        self.failed(detached, "UnsupportedRepresentationGraph", 2)

    def test_orphan_policy_keeps_the_original_ownership_failure_and_adds_no_part_role(self):
        data = append_records(presentation_source(), b"#909=DRAUGHTING_PRE_DEFINED_COLOUR('red');\n")
        strict = self.failed(data, "UnownedOrOrphanProfileContent", 2)
        report = self.checked(data, "--general-brep-source", "--interop-nongeometric")
        self.assertEqual(strict["diagnostics"], report["preserved_diagnostics"])
        self.assertNotIn("909", report["snapshot"]["part"]["roles"].values())
        self.assertEqual("NotRun", report["checks"]["global_rules_47"])

    def test_existing_rgb_colour_name_is_not_subject_to_the_predefined_palette(self):
        self.checked(presentation_source())  # RGB's arbitrary label is 'RGB'.
        self.failed(curve_style_source().replace(b"0.2,0.4,0.6", b"0.2,1.1,0.6"),
                    "SourceRuleViolation:COLOUR_RGB.WR2")

    def test_interpretation_keeps_its_original_binding_diagnostic_and_default_refusal(self):
        data = colour_source(schema=UNREGISTERED)
        strict = self.failed(data, "UnsupportedSchemaBinding", 2, "--general-brep-source", "--interop-nongeometric")
        report = self.checked(data, "--general-brep-source", "--interop-nongeometric",
                              "--interpret-unregistered-ap214-2010")
        self.assertFalse(report["schema_interpretation"]["source_binding_verified"])
        self.assertEqual(strict["diagnostics"], report["schema_interpretation"]["original_binding_diagnostics"])

    def test_planar_profile_does_not_gain_the_new_declarations(self):
        data = colour_source().replace(AP214, AP203).replace(
            b"'automotive_design',2010",
            b"'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011")
        self.failed(data, "UnsupportedEntityType", 2)
