# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Linked BRep/wireframe declarations retain roles without inferring composition."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap203_n916 import legacy_source
from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214, CORPUS, ap214_source
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX
from tools.step_semantic_validation.tests.test_ap214_entities import append_records


def linked_wire_source(schema=AP214, *, direct=False, reverse=False):
    data = (CORPUS / "S203-P04/input.step").read_bytes()
    if schema == b"CONFIG_CONTROL_DESIGN":
        data = legacy_source(data)
    elif schema != AP203:
        data = ap214_source(data).replace(AP214, schema)
    records = b"""#904=AXIS2_PLACEMENT_3D('',#905,#906,#907);
#905=CARTESIAN_POINT('',(0.,0.,0.));
#906=DIRECTION('',(0.,0.,1.));
#907=DIRECTION('',(1.,0.,0.));
#910=GEOMETRIC_CURVE_SET('',(#911,#912));
#911=CARTESIAN_POINT('',(7.,8.,9.));
#912=TRIMMED_CURVE('',#913,(#914),(#915),.T.,.CARTESIAN.);
#913=LINE('',#914,#916);
#914=CARTESIAN_POINT('',(0.,0.,0.));
#915=CARTESIAN_POINT('',(5.,0.,0.));
#916=VECTOR('',#917,1.);
#917=DIRECTION('',(1.,0.,0.));
#920=GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION('',(#910,#904),#12);
"""
    root = 20 if direct else 900
    if not direct:
        data = data.replace(b"SHAPE_DEFINITION_REPRESENTATION(#8,#20)",
                            b"SHAPE_DEFINITION_REPRESENTATION(#8,#900)")
        records += b"#900=SHAPE_REPRESENTATION('',(#904),#12);\n"
        a, b = (20, root) if reverse else (root, 20)
        records += f"#930=SHAPE_REPRESENTATION_RELATIONSHIP('','',#{a},#{b});\n".encode()
    a, b = (920, root) if reverse else (root, 920)
    records += f"#931=SHAPE_REPRESENTATION_RELATIONSHIP('','',#{a},#{b});\n".encode()
    return append_records(data, records)


class WireframeSourceTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix="step-wire-source-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), *flags],
                                 capture_output=True, timeout=20)
        self.assertTrue(run.stdout, run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(data.decode(), report["source"]["input"]["raw"])
        return run, report

    def checked(self, data):
        run, report = self.invoke(data, "--general-brep-source")
        self.assertEqual(0, run.returncode, run.stderr or report["diagnostics"])
        self.assertEqual("Checked", report["status"])
        return report

    def test_fixed_profiles_preserve_three_explicit_types_fields_and_separate_roles(self):
        for schema in (AP214, MBX, AP203, b"CONFIG_CONTROL_DESIGN"):
            with self.subTest(schema=schema):
                report = self.checked(linked_wire_source(schema))
                roles = report["snapshot"]["part"]["roles"]
                self.assertEqual(("900", "20", "920", "930", "931"), tuple(roles[k] for k in
                                 ("definition_representation", "representation", "wireframe_representation",
                                  "shape_representation_relationship", "wireframe_shape_relationship")))
                self.assertEqual("MaterialSolid", report["snapshot"]["part"]["source_shape_role"])
                records = {x["id"]: x for x in report["snapshot"]["records"]}
                self.assertEqual({"GEOMETRIC_CURVE_SET", "GEOMETRIC_SET", "GEOMETRIC_REPRESENTATION_ITEM",
                                  "REPRESENTATION_ITEM"}, set(records["910"]["types"]))
                fields = records["910"]["attributes"]
                self.assertEqual([("name", "REPRESENTATION_ITEM"), ("elements", "GEOMETRIC_SET")],
                                 [(x["name"], x["declared_in"]) for x in fields])
                self.assertEqual(["name", "items", "context_of_items"],
                                 [x["name"] for x in records["920"]["attributes"]])
                self.assertEqual("NotRun", report["checks"]["native_build"])

    def test_relationship_order_is_source_data_and_a_direct_body_can_have_a_link(self):
        for direct, reverse in ((False, True), (True, False), (True, True)):
            with self.subTest(direct=direct, reverse=reverse):
                report = self.checked(linked_wire_source(direct=direct, reverse=reverse))
                self.assertEqual("920", report["snapshot"]["part"]["roles"]["wireframe_representation"])
                raw = next(x for x in report["source"]["records"] if x["id"] == "931")
                ends = [x["text"] for x in raw["components"][0]["parameters"][2:4]]
                self.assertEqual(["920", "20" if direct else "900"] if reverse else
                                 ["20" if direct else "900", "920"], ends)

    def test_explicit_outer_scope_and_original_p0_stay_closed(self):
        run, report = self.invoke(linked_wire_source())
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedRepresentationGraph", report["diagnostics"][0]["key"])
        run, report = self.invoke(linked_wire_source(AP203))
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedEntityType", report["diagnostics"][0]["key"])

    def test_surface_member_is_a_curve_set_source_error_even_with_interoperability(self):
        data = append_records(linked_wire_source().replace(b"(#911,#912)", b"(#911,#912,#940)"),
                              b"#940=PLANE('',#904);\n")
        for flags in ((), ("--interop-nongeometric",)):
            with self.subTest(flags=flags):
                run, report = self.invoke(data, "--general-brep-source", *flags)
                self.assertEqual(1, run.returncode)
                issue = report["diagnostics"][0]
                self.assertEqual(("SourceRuleViolation:GEOMETRIC_CURVE_SET.WR1", "elements", [2, 3]),
                                 tuple(issue[k] for k in ("key", "field", "parameter_path")))
                self.assertEqual("SourceInvalid", issue["classification"])
                self.assertIsNone(report["snapshot"])

    def test_wireframe_rejects_bare_line_and_preserves_the_member_source_location(self):
        data = linked_wire_source().replace(b"(#911,#912)", b"(#911,#912,#913)")
        run, report = self.invoke(data, "--general-brep-source")
        self.assertEqual(1, run.returncode)
        issue = report["diagnostics"][0]
        self.assertEqual("SourceRuleViolation:GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION.WR3", issue["key"])
        self.assertEqual(("910", "elements", [2, 3]),
                         tuple(issue[k] for k in ("record_id", "field", "parameter_path")))

    def test_wireframe_members_and_nonempty_geometry_are_checked_by_their_own_rules(self):
        wrong = linked_wire_source().replace(b"(#910,#904)", b"(#910,#904,#911)")
        placements = re.sub(rb"(?m)^#91[0-7]=[^\n]*\n", b"", linked_wire_source()).replace(
            b"(#910,#904)", b"(#904)")
        for data, clause in ((wrong, "WR1"), (placements, "WR2")):
            with self.subTest(clause=clause):
                run, report = self.invoke(data, "--general-brep-source")
                self.assertEqual(1, run.returncode)
                self.assertEqual("SourceRuleViolation:GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION." + clause,
                                 report["diagnostics"][0]["key"])

    def test_unknown_maps_and_surface_sets_do_not_gain_wireframe_semantics(self):
        cases = ((linked_wire_source().replace(b"#910=GEOMETRIC_CURVE_SET", b"#910=GEOMETRIC_SET"),
                  "SourceRuleViolation:GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION.WR1", 1),
                 (append_records(linked_wire_source(), b"#950=MAPPED_ITEM('',#951,#904);\n"
                                 b"#951=REPRESENTATION_MAP(#904,#920);\n"), "UnsupportedEntityType", 2))
        for data, key, code in cases:
            run, report = self.invoke(data, "--general-brep-source")
            self.assertEqual(code, run.returncode)
            self.assertEqual(key, report["diagnostics"][0]["key"])

    def test_context_mismatch_extra_relationships_and_rootless_wireframes_are_not_guessed(self):
        mismatch = append_records(linked_wire_source().replace(b"(#910,#904),#12", b"(#910,#904),#921"),
                                  b"#921=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11)) "
                                  b"REPRESENTATION_CONTEXT('other','3D'));\n")
        duplicate = append_records(linked_wire_source(),
                                   b"#932=SHAPE_REPRESENTATION_RELATIONSHIP('','',#900,#920);\n")
        rootless = linked_wire_source().replace(b"#900,#920", b"#20,#920")
        for data in (mismatch, duplicate, rootless):
            run, report = self.invoke(data, "--general-brep-source")
            self.assertEqual(2, run.returncode)
            self.assertEqual("UnsupportedRepresentationGraph", report["diagnostics"][0]["key"])
            self.assertIsNone(report["snapshot"])

    def test_coincident_points_keep_distinct_source_identities(self):
        data = append_records(linked_wire_source().replace(b"(#911,#912)", b"(#911,#912,#918)"),
                              b"#918=CARTESIAN_POINT('',(7.,8.,9.));\n")
        report = self.checked(data)
        records = {x["id"]: x for x in report["snapshot"]["records"]}
        self.assertIn("911", records)
        self.assertIn("918", records)
        raw_set = next(x for x in report["source"]["records"] if x["id"] == "910")
        elements = raw_set["components"][0]["parameters"][1]["children"]
        self.assertEqual(["911", "912", "918"], [x["text"] for x in elements])

    def test_trimmed_dependency_cycles_are_rejected_before_wireframe_predicates(self):
        data = linked_wire_source().replace(b"TRIMMED_CURVE('',#913", b"TRIMMED_CURVE('',#912")
        run, report = self.invoke(data, "--general-brep-source")
        self.assertEqual(1, run.returncode)
        self.assertEqual("SourceGeometryDependencyViolation", report["diagnostics"][0]["key"])
        self.assertEqual(("basis_curve", "cycle"),
                         tuple(report["diagnostics"][0][key] for key in ("field", "actual")))
        self.assertIsNone(report["snapshot"])

    def test_wireframe_visits_obey_exact_request_budget(self):
        data = linked_wire_source()
        report = self.checked(data)
        maximum = report["statistics"]["dependency_visits"]
        run, exact = self.invoke(data, "--general-brep-source", "--max-semantic-visits", str(maximum))
        self.assertEqual(0, run.returncode, exact["diagnostics"])
        run, limited = self.invoke(data, "--general-brep-source", "--max-semantic-visits", str(maximum - 1))
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(limited["snapshot"])
        self.assertEqual("DependencyVisitLimit", limited["diagnostics"][0]["key"])
