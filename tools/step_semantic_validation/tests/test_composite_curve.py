# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Ordinary composite source declarations are distinct from native continuity evidence."""
import unittest

from tools.step_semantic_validation.tests import test_wireframe as wireframe_tests
from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX
from tools.step_semantic_validation.tests.test_ap214_entities import append_records


def composite_source(schema=AP214):
    source = wireframe_tests.linked_wire_source(schema).replace(b"(#911,#912)", b"(#911,#940)")
    return append_records(source, b"#940=COMPOSITE_CURVE('ordered source',(#941,#942),.U.);\n"
                          b"#941=COMPOSITE_CURVE_SEGMENT(.CONTINUOUS.,.T.,#912);\n"
                          b"#942=COMPOSITE_CURVE_SEGMENT(.DISCONTINUOUS.,.F.,#950);\n"
                          b"#950=TRIMMED_CURVE('reverse source',#913,(#914),(#915),.F.,.CARTESIAN.);\n")


def shared_source(levels):
    # Each level uses its child twice. The source has O(levels) records, while
    # occurrence expansion would be exponential. No native expansion is requested.
    source = wireframe_tests.linked_wire_source().replace(b"(#911,#912)", b"(#911,#1000)")
    records = []
    for level in range(levels):
        curve, first, last = 1000 + 3 * level, 1001 + 3 * level, 1002 + 3 * level
        child = curve + 3 if level + 1 < levels else 912
        records.extend((f"#{curve}=COMPOSITE_CURVE('',(#{first},#{last}),.U.);\n",
                        f"#{first}=COMPOSITE_CURVE_SEGMENT(.CONTINUOUS.,.T.,#{child});\n",
                        f"#{last}=COMPOSITE_CURVE_SEGMENT(.CONTINUOUS.,.F.,#{child});\n"))
    return append_records(source, "".join(records).encode())


class CompositeSourceTests(unittest.TestCase):
    invoke = wireframe_tests.WireframeSourceTests.invoke
    checked = wireframe_tests.WireframeSourceTests.checked

    def rejected(self, data, key, *flags):
        run, report = self.invoke(data, "--general-brep-source", *flags)
        self.assertEqual(1, run.returncode, run.stderr or report["diagnostics"])
        self.assertIsNone(report["snapshot"])
        self.assertEqual(key, report["diagnostics"][0]["key"])
        return report["diagnostics"][0]

    def test_all_general_profiles_preserve_order_direction_and_unknown_logical(self):
        for schema in (AP214, MBX, AP203, b"CONFIG_CONTROL_DESIGN"):
            with self.subTest(schema=schema):
                report = self.checked(composite_source(schema))
                typed = {x["id"]: x for x in report["snapshot"]["records"]}
                self.assertEqual(["name", "segments", "self_intersect"],
                                 [x["name"] for x in typed["940"]["attributes"]])
                self.assertEqual(["transition", "same_sense", "parent_curve"],
                                 [x["name"] for x in typed["941"]["attributes"]])
                self.assertEqual({"FOUNDED_ITEM", "COMPOSITE_CURVE_SEGMENT"}, set(typed["941"]["types"]))
                self.assertIn("BOUNDED_CURVE", typed["940"]["types"])
                for parent in ("912", "950", "913", "914", "915", "916", "917"):
                    self.assertTrue(typed[parent]["used_by_representation"], parent)
                self.assertFalse(typed["941"]["used_by_representation"])  # Public query is RI-only.
                raw = {x["id"]: x for x in report["source"]["records"]}
                cc = raw["940"]["components"][0]["parameters"]
                self.assertEqual(["941", "942"], [x["text"] for x in cc[1]["children"]])
                self.assertEqual("U", cc[2]["text"])
                self.assertEqual("F", raw["942"]["components"][0]["parameters"][1]["text"])
                self.assertEqual("MaterialSolid", report["snapshot"]["part"]["source_shape_role"])
                self.assertEqual("NotRun", report["checks"]["native_build"])

    def test_list_repetition_is_legal_and_not_deduplicated(self):
        data = composite_source().replace(b"(#941,#942)", b"(#941,#941,#942)")
        report = self.checked(data)
        raw = next(x for x in report["source"]["records"] if x["id"] == "940")
        self.assertEqual(["941", "941", "942"],
                         [x["text"] for x in raw["components"][0]["parameters"][1]["children"]])

    def test_parent_presentations_use_geometry_dependencies_without_owning_foreign_curves(self):
        for schema in (AP214, MBX):
            for target, style in ((b"#950", b"CURVE_STYLE('',#964,POSITIVE_LENGTH_MEASURE(1.),#965)"),
                                  (b"#915", b"POINT_STYLE('',MARKER_TYPE(.DOT.),POSITIVE_LENGTH_MEASURE(1.),#965)")):
                data = append_records(composite_source(schema),
                                      b"#960=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('',(#961),#12);\n"
                                      b"#961=STYLED_ITEM('',(#962)," + target + b");\n"
                                      b"#962=PRESENTATION_STYLE_ASSIGNMENT((#963));\n"
                                      b"#963=" + style + b";\n#965=COLOUR_RGB('',0.2,0.4,0.6);\n" +
                                      (b"#964=DRAUGHTING_PRE_DEFINED_CURVE_FONT('continuous');\n"
                                       if target == b"#950" else b""))
                self.checked(data)
                if target == b"#950":
                    foreign = append_records(data.replace(b"(#962),#950", b"(#962),#980"),
                                             b"#980=TRIMMED_CURVE('',#913,(#914),(#915),.T.,.CARTESIAN.);\n")
                    run, report = self.invoke(foreign, "--general-brep-source", "--interop-nongeometric")
                    self.assertEqual(2, run.returncode)
                    self.assertEqual("UnsupportedPresentationAttachment", report["diagnostics"][0]["key"])

    def test_transition_declarations_check_open_and_closed_sequences(self):
        source = composite_source()
        for transition in (b"CONTINUOUS", b"CONT_SAME_GRADIENT", b"CONT_SAME_GRADIENT_SAME_CURVATURE"):
            self.checked(source.replace(b".CONTINUOUS.", b"." + transition + b"."))
        self.checked(source.replace(b".DISCONTINUOUS.", b".CONTINUOUS."))
        self.rejected(source.replace(b"#941=COMPOSITE_CURVE_SEGMENT(.CONTINUOUS.",
                                     b"#941=COMPOSITE_CURVE_SEGMENT(.DISCONTINUOUS."),
                      "SourceRuleViolation:COMPOSITE_CURVE.WR1")
        changed = source.replace(b"#941=COMPOSITE_CURVE_SEGMENT(.CONTINUOUS.",
                                 b"#941=COMPOSITE_CURVE_SEGMENT(.DISCONTINUOUS.")
        changed = changed.replace(b"#942=COMPOSITE_CURVE_SEGMENT(.DISCONTINUOUS.",
                                  b"#942=COMPOSITE_CURVE_SEGMENT(.CONTINUOUS.")
        self.rejected(changed, "SourceRuleViolation:COMPOSITE_CURVE.WR1")

    def test_required_slots_enum_and_boolean_are_not_derived_or_defaulted(self):
        source = composite_source()
        cases = ((source.replace(b"(#941,#942)", b"()"), "AggregateCardinalityViolation"),
                 (source.replace(b".CONTINUOUS.", b".UNKNOWN."), "EnumerationValueInvalid"),
                 (source.replace(b".CONTINUOUS.,.T.", b".CONTINUOUS.,.U."), "BooleanValueInvalid"),
                 (source.replace(b"(#941,#942),.U.", b"(#941,#942),$"), "RequiredAttributeMissing"),
                 (source.replace(b"(#941,#942),.U.", b"(#941,#942),.U.,*"), "AttributeCountMismatch"))
        for data, key in cases:
            with self.subTest(key=key):
                self.rejected(data, key)

    def test_segment_parent_requires_bounded_membership_even_for_a_circle(self):
        for record in (b"#960=CIRCLE('',#904,2.);\n", b"#960=ELLIPSE('',#904,2.,1.);\n",
                       b"#960=LINE('',#914,#916);\n"):
            data = append_records(composite_source().replace(b".CONTINUOUS.,.T.,#912", b".CONTINUOUS.,.T.,#960"),
                                  record)
            issue = self.rejected(data, "SourceRuleViolation:COMPOSITE_CURVE_SEGMENT.WR1", "--interop-nongeometric")
            self.assertEqual(("941", "parent_curve", [3]),
                             tuple(issue[k] for k in ("record_id", "field", "parameter_path")))

    def test_unused_segment_violates_inverse_minimum_before_orphan_policy(self):
        data = append_records(composite_source(), b"#960=COMPOSITE_CURVE_SEGMENT(.CONTINUOUS.,.T.,#912);\n")
        for flags in ((), ("--interop-nongeometric",)):
            self.rejected(data, "SourceRuleViolation:COMPOSITE_CURVE_SEGMENT.using_curves", *flags)

    def test_mixed_definition_cycles_fail_as_project_dependencies(self):
        source = composite_source()
        for data in (source.replace(b".CONTINUOUS.,.T.,#912", b".CONTINUOUS.,.T.,#940"),
                     source.replace(b"TRIMMED_CURVE('reverse source',#913", b"TRIMMED_CURVE('reverse source',#940")):
            issue = self.rejected(data, "SourceGeometryDependencyViolation")
            self.assertEqual("project necessary geometry dependency closure", issue["responsibility"])
        self.checked(shared_source(120))

    def test_shared_graph_work_is_linear_and_dependency_budget_is_enforced(self):
        small = self.checked(shared_source(8))["statistics"]["dependency_visits"]
        data = shared_source(32)
        large = self.checked(data)["statistics"]["dependency_visits"]
        self.assertLess(large, small * 5)
        self.assertLess(large, 100000)
        run, report = self.invoke(data, "--general-brep-source", "--max-semantic-visits", str(large - 1))
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(report["snapshot"])
        self.assertEqual("DependencyVisitLimit", report["diagnostics"][0]["key"])

    def test_p0_and_unimplemented_composite_subtypes_remain_closed(self):
        run, report = self.invoke(composite_source(AP203))
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedEntityType", report["diagnostics"][0]["key"])
        data = composite_source().replace(b"#941=COMPOSITE_CURVE_SEGMENT", b"#941=REPARAMETRISED_COMPOSITE_CURVE_SEGMENT")
        run, report = self.invoke(data, "--general-brep-source")
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedEntityType", report["diagnostics"][0]["key"])
