# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Fixed AP214 directed construction links and physical supporting members."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX
from tools.step_semantic_validation.tests.test_ap214_entities import append_records


def supporting_source(schema=AP214, *, wireframe=False):
    fixture = Path(__file__).resolve().parents[3] / "source/STEP_IMPORT/tests/fixtures/linked_wireframe.step"
    data = fixture.read_bytes().replace(b"\r\n", b"\n").replace(AP214, schema)
    if not wireframe:
        data = data.replace(b"#910=GEOMETRIC_CURVE_SET('',(#911,#912,#940));\n", b"")
        data = data.replace(b"#920=GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION('',(#910,#904),#12);\n", b"")
        data = data.replace(b"#931=SHAPE_REPRESENTATION_RELATIONSHIP('','',#900,#920);\n", b"")
    return append_records(data, b"""#960=CONSTRUCTIVE_GEOMETRY_REPRESENTATION('',(#904,#970,#972,#911,#912,#940),#12);
#961=CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP('',$,#900,#960);
#970=AXIS2_PLACEMENT_3D('',#971,#973,#974);
#971=CARTESIAN_POINT('',(30.,40.,50.));
#973=DIRECTION('',(1.,0.,0.));
#974=DIRECTION('',(0.,1.,0.));
#972=PLANE('',#970);
""")


class SupportingSourceTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix="step-supporting-source-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path),
                                  "--general-brep-source", *map(str, flags)], capture_output=True, timeout=20)
        self.assertTrue(run.stdout, run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(data.decode(), report["source"]["input"]["raw"])
        return run, report

    def test_ap214_fields_members_and_distinct_roles(self):
        for schema in (AP214, MBX):
            with self.subTest(schema=schema):
                run, report = self.invoke(supporting_source(schema))
                self.assertEqual(0, run.returncode, report["diagnostics"])
                self.assertEqual("Checked", report["status"])
                roles = report["snapshot"]["part"]["roles"]
                self.assertEqual(("960", "961", "20"), tuple(roles[k] for k in
                                 ("construction_representation", "construction_relationship", "representation")))
                records = {x["id"]: x for x in report["snapshot"]["records"]}
                self.assertEqual(["name", "items", "context_of_items"],
                                 [x["name"] for x in records["960"]["attributes"]])
                self.assertEqual(["name", "description", "rep_1", "rep_2"],
                                 [x["name"] for x in records["961"]["attributes"]])
                self.assertNotIn("wireframe_representation", roles)

    def test_wrong_context_direction_and_first_endpoint_have_distinct_source_clauses(self):
        original = supporting_source()
        mismatch = append_records(original.replace(b"#940),#12", b"#940),#980"),
            b"#980=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11)) "
            b"REPRESENTATION_CONTEXT('other','3D'));\n")
        reversed_link = original.replace(b"'',$,#900,#960", b"'',$,#960,#900")
        wrong_first = append_records(original.replace(b"'',$,#900,#960", b"'',$,#980,#960"),
                                     b"#980=REPRESENTATION('',(#904),#12);\n")
        for data, clause in ((mismatch, "WR1"), (reversed_link, "WR2"), (wrong_first, "WR3")):
            with self.subTest(clause=clause):
                run, report = self.invoke(data)
                self.assertEqual(1, run.returncode, report["diagnostics"])
                self.assertEqual("SourceRuleViolation:CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP." + clause,
                                 report["diagnostics"][0]["key"])

    def test_fixed_member_category_requires_exactly_one(self):
        for member, extra in ((b"#910", b"#910=GEOMETRIC_CURVE_SET('',(#911));\n"),
                              (b"#507", b""), (b"#21", b"")):
            with self.subTest(member=member):
                data = append_records(supporting_source().replace(b"(#904,#970,#972,#911,#912,#940)",
                                      b"(#904,#970,#972,#911,#912,#940," + member + b")"), extra)
                run, report = self.invoke(data, "--interop-nongeometric")
                self.assertEqual(1, run.returncode, report["diagnostics"])
                issue = report["diagnostics"][0]
                self.assertEqual("SourceRuleViolation:CONSTRUCTIVE_GEOMETRY_REPRESENTATION.WR2", issue["key"])
                self.assertEqual(("960", "items"), (issue["record_id"], issue["field"]))

    def test_no_link_multiple_links_and_n2635_never_borrow_the_ap214_contract(self):
        missing = supporting_source().replace(
            b"#961=CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP('',$,#900,#960);\n", b"")
        duplicate = append_records(supporting_source(),
            b"#962=CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP('',$,#20,#960);\n")
        for data in (missing, duplicate):
            run, report = self.invoke(data)
            self.assertEqual(2, run.returncode)
            self.assertEqual("UnsupportedConstructionGeometryGraph", report["diagnostics"][0]["key"])
        run, report = self.invoke(supporting_source(AP203))
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedEntityType", report["diagnostics"][0]["key"])

    def test_existing_wireframe_and_supporting_roles_coexist_with_shared_source_members(self):
        run, report = self.invoke(supporting_source(wireframe=True))
        self.assertEqual(0, run.returncode, report["diagnostics"])
        roles = report["snapshot"]["part"]["roles"]
        self.assertEqual(("920", "960"), (roles["wireframe_representation"], roles["construction_representation"]))

    def test_new_dependency_walk_obeys_exact_budget(self):
        data = supporting_source()
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        visits = report["statistics"]["dependency_visits"]
        run, report = self.invoke(data, "--max-semantic-visits", visits)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        run, report = self.invoke(data, "--max-semantic-visits", visits - 1)
        self.assertIsNone(report["snapshot"])
        self.assertEqual("DependencyVisitLimit", report["diagnostics"][0]["key"])

    def test_parametric_context_marker_preserves_explicit_units_and_context_fields(self):
        for schema in (AP214, MBX):
            data = supporting_source(schema).replace(
                b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11)) REPRESENTATION_CONTEXT",
                b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11)) PARAMETRIC_REPRESENTATION_CONTEXT() REPRESENTATION_CONTEXT")
            run, report = self.invoke(data)
            self.assertEqual(0, run.returncode, report["diagnostics"])
            context = next(x for x in report["snapshot"]["records"] if x["id"] == "12")
            self.assertIn("PARAMETRIC_REPRESENTATION_CONTEXT", context["types"])
            self.assertEqual(["coordinate_space_dimension", "units", "context_identifier", "context_type"],
                             [x["name"] for x in context["attributes"]])

    def test_unitless_parametric_context_does_not_supply_physical_units(self):
        data = supporting_source().replace(b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11))",
                                           b"PARAMETRIC_REPRESENTATION_CONTEXT()")
        # Remove now-unused unit records so the older orphan guard does not
        # precede the physical-context prerequisite being exercised here.
        data = data.replace(b"#10=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.));\n", b"")
        data = data.replace(b"#11=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));\n", b"")
        run, report = self.invoke(data)
        self.assertEqual(2, run.returncode, report["diagnostics"])
        self.assertIsNone(report["snapshot"])
        self.assertEqual("UnsupportedPhysicalUnitContext", report["diagnostics"][0]["key"])
