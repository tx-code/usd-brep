# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Point appearance retains literal SELECT branches and cannot own geometry."""
import unittest

from tools.step_semantic_validation.tests import test_value_item as value_item_tests
from tools.step_semantic_validation.tests.test_wireframe import linked_wire_source
from tools.step_semantic_validation.tests.test_ap214 import AP214
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX
from tools.step_semantic_validation.tests.test_ap214_entities import append_records


def point_style_source(schema=AP214, marker=b"#961", size=b"POSITIVE_LENGTH_MEASURE(1.)"):
    data = linked_wire_source(schema)
    return append_records(data, b"#960=POINT_STYLE('source point'," + marker + b"," + size + b",#964);\n"
                          b"#961=PRE_DEFINED_POINT_MARKER_SYMBOL('circle');\n"
                          b"#964=COLOUR_RGB('',0.1,0.2,0.3);\n"
                          b"#965=PRESENTATION_STYLE_ASSIGNMENT((#960));\n"
                          b"#966=STYLED_ITEM('',(#965),#911);\n"
                          b"#967=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('',(#966),#12);\n")


class PointStyleTests(unittest.TestCase):
    invoke = value_item_tests.ValueItemTests.invoke
    checked = value_item_tests.ValueItemTests.checked

    def test_predefined_diamond_name_and_required_point_style_fields(self):
        for schema in (AP214, MBX):
            report = self.checked(point_style_source(schema))
            records = {x["id"]: x for x in report["snapshot"]["records"]}
            self.assertEqual(["name", "marker", "marker_size", "marker_colour"],
                             [x["name"] for x in records["960"]["attributes"]])
            self.assertEqual(["name"], [x["name"] for x in records["961"]["attributes"]])
            self.assertEqual({"PRE_DEFINED_ITEM", "PRE_DEFINED_SYMBOL", "PRE_DEFINED_MARKER",
                              "PRE_DEFINED_POINT_MARKER_SYMBOL"}, set(records["961"]["types"]))

    def test_all_marker_enum_values_are_typed_and_ring_is_not_a_symbol_name(self):
        for value in (b"DOT", b"X", b"PLUS", b"ASTERISK", b"RING", b"SQUARE", b"TRIANGLE"):
            data = point_style_source(marker=b"MARKER_TYPE(." + value + b".)")
            # Remove the unused predefined marker so the orphan guard does not
            # obscure the actual SELECT branch exercised here.
            data = data.replace(b"#961=PRE_DEFINED_POINT_MARKER_SYMBOL('circle');\n", b"")
            self.checked(data)
        run, report = self.invoke(point_style_source().replace(b"SYMBOL('circle')", b"SYMBOL('ring')"))
        self.assertEqual(1, run.returncode)
        self.assertEqual("SourceRuleViolation:PRE_DEFINED_POINT_MARKER_SYMBOL.WR1", report["diagnostics"][0]["key"])

    def test_positive_size_colour_and_duplicate_assignment_are_checked(self):
        bad_size = point_style_source(size=b"POSITIVE_LENGTH_MEASURE(0.)")
        bad_colour = point_style_source().replace(b"COLOUR_RGB('',0.1,0.2,0.3)", b"COLOUR_RGB('',1.01,0.2,0.3)")
        duplicate = append_records(point_style_source().replace(b"ASSIGNMENT((#960))", b"ASSIGNMENT((#960,#970))"),
                                   b"#970=POINT_STYLE('',#961,POSITIVE_LENGTH_MEASURE(2.),#964);\n")
        for data, key in ((bad_size, "POSITIVE_LENGTH_MEASURE.WR1"), (bad_colour, "COLOUR_RGB.WR1"),
                          (duplicate, "PRESENTATION_STYLE_ASSIGNMENT.WR1")):
            run, report = self.invoke(data, "--interop-nongeometric")
            self.assertEqual(1, run.returncode, report["diagnostics"])
            self.assertEqual("SourceRuleViolation:" + key, report["diagnostics"][0]["key"])

    def test_geometric_presentation_has_stricter_size_rule_than_generic_style(self):
        for size in (b"DESCRIPTIVE_MEASURE('source text')", b"#970"):
            data = point_style_source(size=size)
            if size == b"#970":
                data = append_records(data, b"#970=MEASURE_WITH_UNIT(LENGTH_MEASURE(1.),#10);\n")
            run, report = self.invoke(data)
            self.assertEqual(1, run.returncode, report["diagnostics"])
            self.assertEqual("SourceRuleViolation:MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION.WR6",
                             report["diagnostics"][0]["key"])

    def test_marker_reference_and_foreign_point_do_not_acquire_geometry_ownership(self):
        run, report = self.invoke(point_style_source(marker=b"#911"))
        self.assertEqual(1, run.returncode)
        self.assertEqual("ReferenceTypeMismatch", report["diagnostics"][0]["key"])
        data = append_records(point_style_source().replace(b"(#965),#911", b"(#965),#980"),
                              b"#980=CARTESIAN_POINT('',(1.,2.,3.));\n")
        run, report = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedPresentationAttachment", report["diagnostics"][0]["key"])

    def test_point_style_dependency_visits_are_bounded(self):
        data = point_style_source()
        report = self.checked(data)
        visits = report["statistics"]["dependency_visits"]
        run, report = self.invoke(data, "--max-semantic-visits", visits)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        run, report = self.invoke(data, "--max-semantic-visits", visits - 1)
        self.assertIsNone(report["snapshot"])
        self.assertEqual("DependencyVisitLimit", report["diagnostics"][0]["key"])
