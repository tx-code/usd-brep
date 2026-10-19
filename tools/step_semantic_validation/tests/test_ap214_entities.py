# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""AP214 source presentation/relationship contracts through the real CLI."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP203, CORPUS, ap214_source


def append_records(data, records):
    position = data.index(b"ENDSEC;", data.index(b"DATA;"))
    return data[:position] + records + data[position:]


def presentation_source():
    # Literal fill-area/RGB chain from the pinned AP214 declarations. The styled
    # target is the existing source shell model; its geometry is never rebuilt.
    data = ap214_source((CORPUS / "S203-P01/input.step").read_bytes())
    return append_records(data, b"""#900=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('display',(#901),#12);
#901=STYLED_ITEM('source style',(#902),#21);
#902=PRESENTATION_STYLE_ASSIGNMENT((#903));
#903=SURFACE_STYLE_USAGE(.BOTH.,#904);
#904=SURFACE_SIDE_STYLE('source side',(#905));
#905=SURFACE_STYLE_FILL_AREA(#906);
#906=FILL_AREA_STYLE('source fill',(#907));
#907=FILL_AREA_STYLE_COLOUR('source colour',#908);
#908=COLOUR_RGB('RGB',0.25,0.5,0.75);
""")


def linked_source():
    data = presentation_source()
    assert data.count(b"SHAPE_DEFINITION_REPRESENTATION(#8,#20)") == 1
    data = data.replace(b"SHAPE_DEFINITION_REPRESENTATION(#8,#20)", b"SHAPE_DEFINITION_REPRESENTATION(#8,#910)")
    return append_records(data, b"""#910=SHAPE_REPRESENTATION('source root',(#914),#12);
#911=SHAPE_REPRESENTATION_RELATIONSHIP('source link',$,#910,#20);
#913=CARTESIAN_POINT('root placement',(1000.,2000.,3000.));
#914=AXIS2_PLACEMENT_3D('source frame',#913,#131,#132);
""")


class Ap214EntityTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-ap214-entities-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            command = [os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), *map(str, options)]
            run = subprocess.run(command, capture_output=True, timeout=15)
        return run, json.loads(run.stdout) if run.stdout else None

    def assert_failure(self, data, key, code=2):
        run, result = self.invoke(data)
        self.assertEqual(code, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertIsNone(result["snapshot"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        self.assertEqual(key, result["diagnostics"][0]["key"])
        return result["diagnostics"][0]

    def test_fill_area_presentation_is_preserved_beside_the_body(self):
        data = presentation_source()
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual("Checked", result["status"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        roles = result["snapshot"]["part"]["roles"]
        self.assertEqual("20", roles["representation"])
        self.assertEqual("900", roles["presentation_0"])
        self.assertEqual("12", roles["unit_context"])
        self.assertEqual("OpenSurfaceModel", result["snapshot"]["part"]["source_shape_role"])
        records = {record["id"]: record for record in result["snapshot"]["records"]}
        self.assertIn("FOUNDED_ITEM", records["902"]["types"])
        self.assertEqual([("name", "COLOUR_SPECIFICATION"), ("red", "COLOUR_RGB"),
                          ("green", "COLOUR_RGB"), ("blue", "COLOUR_RGB")],
                         [(a["name"], a["declared_in"]) for a in records["908"]["attributes"]])
        self.assertTrue(records["901"]["used_by_representation"])
        self.assertFalse(records["908"]["used_by_representation"])
        self.assertEqual("NotRun", result["checks"]["global_rules_47"])
        self.assertEqual("NotRun", result["checks"]["native_build"])

    def test_source_root_relationship_and_body_remain_separate_in_both_directions(self):
        original = linked_source()
        for endpoints in (b"#910,#20", b"#20,#910"):
            with self.subTest(endpoints=endpoints):
                data = original.replace(b"'source link',$,#910,#20", b"'source link',$," + endpoints)
                run, result = self.invoke(data)
                self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
                roles = result["snapshot"]["part"]["roles"]
                self.assertEqual("910", roles["definition_representation"])
                self.assertEqual("911", roles["shape_representation_relationship"])
                self.assertEqual("20", roles["representation"])
                raw = next(r for r in result["source"]["records"] if r["id"] == "911")
                self.assertIn(endpoints.decode("ascii"), raw["raw"])
                record = next(r for r in result["snapshot"]["records"] if r["id"] == "911")
                self.assertEqual([("name", "REPRESENTATION_RELATIONSHIP"),
                                  ("description", "REPRESENTATION_RELATIONSHIP"),
                                  ("rep_1", "REPRESENTATION_RELATIONSHIP"), ("rep_2", "REPRESENTATION_RELATIONSHIP")],
                                 [(a["name"], a["declared_in"]) for a in record["attributes"]])
                frame = next(r for r in result["snapshot"]["records"] if r["id"] == "914")
                self.assertTrue(frame["used_by_representation"])
                # The root placement is a retained item, not a transform applied to the body.
                self.assertIn("#100=CARTESIAN_POINT('',(3.,-2.,5.));", result["source"]["input"]["raw"])

    def test_presentation_keeps_its_context_without_overriding_body_units(self):
        data = linked_source()
        context = data[data.index(b"#12="):data.index(b";", data.index(b"#12=")) + 1]
        display_context = context.replace(b"#12=", b"#912=").replace(b"(#10,#11)", b"(#915,#11)")
        data = append_records(data, display_context +
                              b"\n#915=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.CENTI.,.METRE.));\n")
        data = data.replace(b"'display',(#901),#12", b"'display',(#901),#912")
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual("12", result["snapshot"]["part"]["roles"]["unit_context"])
        self.assertEqual({"kind": "rational", "num": "1", "den": "1"}, result["snapshot"]["units"]["length_scale_mm"])
        self.assertEqual({"12", "912"}, set(result["snapshot"]["type_populations"]["REPRESENTATION_CONTEXT"]))
        display_unit = next(u for u in result["snapshot"]["units"]["si_definitions"] if u["source_id"] == "915")
        self.assertEqual({"kind": "rational", "num": "10", "den": "1"}, display_unit["scale"])
        raw = next(r for r in result["source"]["records"] if r["id"] == "900")
        self.assertEqual("912", raw["components"][0]["parameters"][2]["text"])

    def test_new_transport_fields_keep_the_pinned_declarations(self):
        run, result = self.invoke(linked_source())
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        # Independently transcribed from the pinned EXPRESS explicit declarations;
        # this does not read the product catalog or reuse the old AP203 witness.
        expected = {
            "900": [("name", "REPRESENTATION", "LABEL"), ("items", "REPRESENTATION", "REPRESENTATION_ITEM"),
                    ("context_of_items", "REPRESENTATION", "REPRESENTATION_CONTEXT")],
            "901": [("name", "REPRESENTATION_ITEM", "LABEL"),
                    ("styles", "STYLED_ITEM", "PRESENTATION_STYLE_ASSIGNMENT"),
                    ("item", "STYLED_ITEM", "REPRESENTATION_ITEM")],
            "902": [("styles", "PRESENTATION_STYLE_ASSIGNMENT", "PRESENTATION_STYLE_SELECT")],
            "903": [("side", "SURFACE_STYLE_USAGE", "SURFACE_SIDE"),
                    ("style", "SURFACE_STYLE_USAGE", "SURFACE_SIDE_STYLE_SELECT")],
            "904": [("name", "SURFACE_SIDE_STYLE", "LABEL"),
                    ("styles", "SURFACE_SIDE_STYLE", "SURFACE_STYLE_ELEMENT_SELECT")],
            "905": [("fill_area", "SURFACE_STYLE_FILL_AREA", "FILL_AREA_STYLE")],
            "906": [("name", "FILL_AREA_STYLE", "LABEL"), ("fill_styles", "FILL_AREA_STYLE", "FILL_STYLE_SELECT")],
            "907": [("name", "FILL_AREA_STYLE_COLOUR", "LABEL"), ("fill_colour", "FILL_AREA_STYLE_COLOUR", "COLOUR")],
            "908": [("name", "COLOUR_SPECIFICATION", "LABEL"), ("red", "COLOUR_RGB", "REAL"),
                    ("green", "COLOUR_RGB", "REAL"), ("blue", "COLOUR_RGB", "REAL")],
            "911": [("name", "REPRESENTATION_RELATIONSHIP", "LABEL"),
                    ("description", "REPRESENTATION_RELATIONSHIP", "TEXT"),
                    ("rep_1", "REPRESENTATION_RELATIONSHIP", "REPRESENTATION"),
                    ("rep_2", "REPRESENTATION_RELATIONSHIP", "REPRESENTATION")],
        }
        records = {record["id"]: record for record in result["snapshot"]["records"]}
        for record, attributes in expected.items():
            with self.subTest(record=record):
                self.assertEqual(attributes, [(a["name"], a["declared_in"], a["declared_type"])
                                              for a in records[record]["attributes"]])
        styles = next(a for a in records["904"]["attributes"] if a["name"] == "styles")
        self.assertEqual(("SET", 1, 7, True),
                         (styles["aggregate"], styles["minimum"], styles["maximum"], styles["unique_elements"]))

    def test_field_errors_use_ap214_declarations_and_source_locations(self):
        original = linked_source()
        mutations = (
            (b".BOTH.", b".UNKNOWN.", "EnumerationValueInvalid", "903", "side", b".UNKNOWN."),
            (b".BOTH.,#904", b".BOTH.,#908", "ReferenceTypeMismatch", "903", "style", b"#908"),
            (b"'RGB',0.25,", b"'RGB',1,", "AttributeTypeMismatch", "908", "red", b"1"),
            (b"COLOUR_RGB('RGB',", b"COLOUR_RGB($,", "RequiredAttributeMissing", "908", "name", b"$"),
            (b"'source fill',(#907)", b"'source fill',(#907,#907)",
             "SetDuplicateMember", "906", "fill_styles", b"#907"),
            (b"'source side',(#905)", b"'source side',()", "AggregateCardinalityViolation", "904", "styles", b"()"),
            (b"'source link',$,#910,#20", b"'source link',$,#910,#21", "ReferenceTypeMismatch", "911", "rep_2", b"#21"),
            (b"COLOUR_RGB('RGB',0.25,0.5,0.75)", b"COLOUR_RGB('RGB',0.25,0.5)",
             "AttributeCountMismatch", "908", "", b"COLOUR_RGB('RGB',0.25,0.5)"),
            (b"COLOUR_RGB('RGB',0.25,0.5,0.75)", b"COLOUR_RGB('RGB',0.25,0.5,0.75,1.)",
             "AttributeCountMismatch", "908", "", b"COLOUR_RGB('RGB',0.25,0.5,0.75,1.)"),
        )
        for before, after, key, record, field, token in mutations:
            with self.subTest(key=key, record=record):
                self.assertEqual(1, original.count(before))
                data = original.replace(before, after)
                diagnostic = self.assert_failure(data, key, 1)
                self.assertEqual((record, field), (diagnostic["record_id"], diagnostic["field"]))
                begin, end = diagnostic["span"]
                self.assertEqual(token, data[begin:end])
                if key == "SetDuplicateMember":
                    self.assertEqual("FILL_AREA_STYLE.fill_styles", diagnostic["responsibility"])
                else:
                    self.assertIn("AP214E3_2010.exp", diagnostic["responsibility"])

    def test_styles_do_not_grant_ownership_to_unrelated_geometry(self):
        data = append_records(linked_source(), b"#920=CARTESIAN_POINT('detached',(50.,60.,70.));\n")
        data = data.replace(b"(#902),#21", b"(#902),#920")
        error = self.assert_failure(data, "UnsupportedPresentationAttachment")
        self.assertEqual(("901", "item"), (error["record_id"], error["field"]))
        for name in (b"#901", b"#914"):
            with self.subTest(target=name):
                self.assert_failure(linked_source().replace(b"(#902),#21", b"(#902)," + name),
                                    "UnsupportedPresentationAttachment")
        self.assert_failure(append_records(linked_source(), b"#920=COLOUR_RGB('unused',0.,0.,0.);\n"),
                            "UnownedOrOrphanProfileContent")

    def test_ambiguous_shape_graphs_and_context_changes_are_not_guessed(self):
        data = linked_source()
        self.assert_failure(append_records(data, b"#920=SHAPE_REPRESENTATION_RELATIONSHIP('other',$,#910,#20);\n"),
                            "UnsupportedRepresentationGraph")
        for endpoints in (b"#910,#910", b"#900,#20", b"#910,#900"):
            with self.subTest(endpoints=endpoints):
                self.assert_failure(data.replace(b"'source link',$,#910,#20", b"'source link',$," + endpoints),
                                    "UnsupportedRepresentationGraph")
        context = data[data.index(b"#12="):data.index(b";", data.index(b"#12=")) + 1]
        different_context = append_records(data, context.replace(b"#12=", b"#912=") + b"\n")
        self.assert_failure(different_context.replace(b"'source root',(#914),#12", b"'source root',(#914),#912"),
                            "UnsupportedRepresentationGraph")
        self.assert_failure(data.replace(b"'source root',(#914)", b"'source root',(#21)"),
                            "UnsupportedRepresentationGraph")
        direct = data.replace(b"SHAPE_DEFINITION_REPRESENTATION(#8,#910)",
                              b"SHAPE_DEFINITION_REPRESENTATION(#8,#20)")
        self.assert_failure(direct, "UnsupportedRepresentationGraph")

    def test_ap203_and_unknown_presentation_or_transform_branches_stay_closed(self):
        data = presentation_source().replace(b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}", AP203)
        self.assert_failure(data, "UnsupportedEntityType")
        other_colour = presentation_source().replace(b"COLOUR_RGB('RGB',0.25,0.5,0.75)",
                                                     b"EXTERNALLY_DEFINED_COLOUR('red')")
        self.assert_failure(other_colour, "UnsupportedEntityType")
        transformed = linked_source().replace(
            b"SHAPE_REPRESENTATION_RELATIONSHIP('source link',$,#910,#20)",
            b"REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION('source link',$,#910,#20,#914)")
        self.assert_failure(transformed, "UnsupportedEntityType")

    def test_transport_fields_do_not_claim_unexecuted_style_where_rules(self):
        # FILL_AREA_STYLE is a legal PRESENTATION_STYLE_SELECT branch in the pinned
        # source. MDGPR.WR4 further restricts it; this source stage has not run that
        # WHERE rule, just as it has not run COLOUR_RGB's range predicates.
        data = presentation_source().replace(b"PRESENTATION_STYLE_ASSIGNMENT((#903))",
                                             b"PRESENTATION_STYLE_ASSIGNMENT((#903,#906))")
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual("NotRun", result["checks"]["global_rules_47"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])

    def test_forward_source_order_unicode_names_and_bounded_publication(self):
        data = linked_source().replace(b"COLOUR_RGB('RGB',", b"COLOUR_RGB('\\X2\\989C8272\\X0\\',")
        begin = data.index(b"#900=")
        end = data.index(b"ENDSEC;", begin)
        metadata = data[begin:end]
        data = data[:begin] + data[end:]
        position = data.index(b"DATA;") + len(b"DATA;")
        data = data[:position] + b"\n" + metadata + data[position:]
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        colour = next(r for r in result["source"]["records"] if r["id"] == "908")
        self.assertEqual("颜色", colour["components"][0]["parameters"][0]["text"])
        visits = result["statistics"]["dependency_visits"]
        self.assertEqual(0, self.invoke(data, "--max-semantic-visits", visits)[0].returncode)
        stopped, result = self.invoke(data, "--max-semantic-visits", visits - 1)
        self.assertEqual(3, stopped.returncode)
        self.assertIsNone(result["snapshot"])
        self.assertEqual("DependencyVisitLimit", result["diagnostics"][0]["key"])
        size = len(run.stdout)
        for _ in range(4):
            run, _ = self.invoke(data, "--max-report-bytes", size)
            self.assertEqual(0, run.returncode)
            size = len(run.stdout)
        stopped, result = self.invoke(data, "--max-report-bytes", size - 1)
        self.assertEqual(5, stopped.returncode)
        self.assertEqual(b"", stopped.stdout)
        self.assertIsNone(result)
