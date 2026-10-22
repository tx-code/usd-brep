# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Declared product graph evidence never grants a partial semantic/native model."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import CORPUS, ap214_source
from tools.step_semantic_validation.tests.test_ap203_n916 import legacy_source


def graph_source(*, usage=False, transform=False, ap214=True):
    data = (CORPUS / "S203-P04/input.step").read_bytes()
    if ap214:
        data = ap214_source(data)
    axis = re.search(rb"#([0-9]+)=AXIS2_PLACEMENT_3D", data)[1].decode("ascii")
    records = [
        "#900=PRODUCT('P0-PART','independent source definition',$,(#3));",
        "#901=PRODUCT_DEFINITION_FORMATION('B',$,#900);",
        "#902=PRODUCT_DEFINITION('D2',$,#901,#6);",
        "#903=PRODUCT_DEFINITION_SHAPE('',$,#902);",
        "#904=SHAPE_DEFINITION_REPRESENTATION(#903,#20);",
    ]
    if usage or transform:
        records.append("#910=NEXT_ASSEMBLY_USAGE_OCCURRENCE('usage-1','',$,#7,#902,$);")
    if transform:
        records += [
            "#911=PRODUCT_DEFINITION_SHAPE('',$,#910);",
            "#912=CONTEXT_DEPENDENT_SHAPE_REPRESENTATION(#914,#911);",
            "#914=(REPRESENTATION_RELATIONSHIP('',$,#20,#916) "
            "REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION(#915) SHAPE_REPRESENTATION_RELATIONSHIP());",
            f"#915=ITEM_DEFINED_TRANSFORMATION('',$,#{axis},#{axis});",
            f"#916=SHAPE_REPRESENTATION('',(#{axis}),#917);",
            "#917=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11)) "
            "REPRESENTATION_CONTEXT('other','3D'));",
        ]
    return data.replace(b"ENDSEC;\nEND-ISO", ("\n".join(records) + "\n").encode("ascii") + b"ENDSEC;\nEND-ISO")


def append_record(data, text):
    return data.replace(b"ENDSEC;\nEND-ISO", text + b"\nENDSEC;\nEND-ISO")


def legacy_graph_source():
    data = legacy_source(graph_source(transform=True, ap214=False))
    for old, new in (
        (b"'independent source definition',$,", b"'independent source definition','',"),
        (b"PRODUCT_DEFINITION_FORMATION('B',$,", b"PRODUCT_DEFINITION_FORMATION('B','',"),
        (b"PRODUCT_DEFINITION('D2',$,", b"PRODUCT_DEFINITION('D2','',"),
        (b"NEXT_ASSEMBLY_USAGE_OCCURRENCE('usage-1','',$,", b"NEXT_ASSEMBLY_USAGE_OCCURRENCE('usage-1','','',"),
        (b"REPRESENTATION_RELATIONSHIP('',$,", b"REPRESENTATION_RELATIONSHIP('','',"),
        (b"ITEM_DEFINED_TRANSFORMATION('',$,", b"ITEM_DEFINED_TRANSFORMATION('','',"),
    ):
        data = data.replace(old, new)
    return data


class ProductStructureTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix="step-product-graph-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path),
                                  "--general-brep-source", *flags], capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def inspect_structure(self, data):
        run, result = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertEqual("Unsupported", result["status"])
        self.assertIsNone(result["snapshot"])
        self.assertIn("UnsupportedSinglePartGraph", [x["key"] for x in result["diagnostics"]])
        graph = result["product_structure"]
        self.assertEqual("DeclaredProductGraphOnly", graph["qualification"])
        self.assertEqual("NotQualified", graph["part_ownership"])
        self.assertEqual("NotRun", graph["unit_context_validation"])
        self.assertEqual("NotRun", graph["transform_evaluation"])
        self.assertEqual("NotRun", graph["native_build"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        return graph

    def test_independent_definitions_keep_source_ids_and_shared_representation_links(self):
        graph = self.inspect_structure(graph_source())
        self.assertEqual(["7", "902"], [x["definition"] for x in graph["definitions"]])
        self.assertEqual(["4", "900"], [x["product"] for x in graph["definitions"]])
        self.assertEqual([["9"], ["904"]], [[r["link"] for r in x["shape_definition_representations"]]
                                               for x in graph["definitions"]])
        self.assertTrue(all(r["representation"] == "20" and r["context"] == "12"
                            for x in graph["definitions"] for r in x["shape_definition_representations"]))
        self.assertEqual(["7", "902"], graph["usage_roots"])
        self.assertEqual([], graph["usages"])
        # Both PRODUCT.id values are deliberately equal. They are labels,
        # not the source identity of a reusable product definition.

    def test_usage_edges_and_repeated_occurrences_are_not_merged(self):
        data = append_record(graph_source(usage=True),
                             b"#913=NEXT_ASSEMBLY_USAGE_OCCURRENCE('usage-2','',$,#7,#902,$);")
        graph = self.inspect_structure(data)
        self.assertEqual(["7"], graph["usage_roots"])
        self.assertEqual(["910", "913"], [x["usage"] for x in graph["usages"]])
        self.assertTrue(all(x["next_assembly_occurrence"] for x in graph["usages"]))

    def test_context_dependent_links_and_transform_items_remain_uninterpreted(self):
        graph = self.inspect_structure(graph_source(transform=True))
        self.assertEqual(["912"], [x["link"] for x in graph["context_dependent_shapes"]])
        self.assertEqual("910", graph["context_dependent_shapes"][0]["product_relationship"])
        transform, = graph["transforms"]
        self.assertEqual(("914", "20", "916", "12", "917", "915"),
                         tuple(transform[x] for x in ("relationship", "first_representation", "second_representation",
                                                       "first_context", "second_context", "transformation")))
        self.assertEqual(transform["first_item"], transform["second_item"])
        relation, = graph["representation_relationships"]
        self.assertEqual("915", relation["transformation"])
        self.assertTrue(relation["shape_relationship"])

    def test_required_graph_rules_have_source_locations_and_withhold_structure(self):
        controls = [
            (append_record(graph_source(usage=True),
                           b"#913=NEXT_ASSEMBLY_USAGE_OCCURRENCE('usage-1','',$,#7,#902,$);"),
             "SourceRuleViolation:PRODUCT_DEFINITION_USAGE.UR1"),
            (append_record(graph_source(usage=True),
                           b"#913=NEXT_ASSEMBLY_USAGE_OCCURRENCE('usage-2','',$,#902,#7,$);"),
             "SourceRuleViolation:PRODUCT_DEFINITION_USAGE.WR1"),
            (graph_source(usage=True).replace(b"#7,#902,$", b"#7,#7,$"),
             "SourceRuleViolation:PRODUCT_DEFINITION_USAGE.WR1"),
            (graph_source(transform=True).replace(b"#911=PRODUCT_DEFINITION_SHAPE('',$,#910)",
                                                  b"#911=PRODUCT_DEFINITION_SHAPE('',$,#7)"),
             "SourceRuleViolation:CONTEXT_DEPENDENT_SHAPE_REPRESENTATION.WR1"),
            (graph_source(transform=True).replace(b"),#917);", b"),#12);"),
             "SourceRuleViolation:REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION.WR1"),
        ]
        for data, key in controls:
            with self.subTest(key=key):
                run, result = self.invoke(data)
                self.assertNotEqual(0, run.returncode)
                self.assertEqual("Rejected", result["status"])
                self.assertIsNone(result["snapshot"])
                self.assertNotIn("product_structure", result)
                diagnostic = next(x for x in result["diagnostics"] if x["key"] == key)
                self.assertTrue(diagnostic["record_id"])
                self.assertTrue(diagnostic["parameter_path"])
                lo, hi = diagnostic["span"]
                self.assertGreater(hi, lo)

    def test_ap203_does_not_acquire_unregistered_assembly_declarations(self):
        run, result = self.invoke(graph_source(usage=True, ap214=False))
        self.assertNotEqual(0, run.returncode)
        self.assertIn("UnsupportedEntityType", [x["key"] for x in result["diagnostics"]])
        self.assertNotIn("product_structure", result)

    def test_modified_n916_keeps_its_own_transport_and_source_authority(self):
        graph = self.inspect_structure(legacy_graph_source())
        self.assertEqual(2, len(graph["definitions"]))
        self.assertEqual(1, len(graph["context_dependent_shapes"]))
        for field in (b"NEXT_ASSEMBLY_USAGE_OCCURRENCE('usage-1','','',", b"ITEM_DEFINED_TRANSFORMATION('','',"):
            with self.subTest(field=field):
                data = legacy_graph_source().replace(field, field[:-3] + b"$,", 1)
                run, result = self.invoke(data)
                self.assertNotEqual(0, run.returncode)
                self.assertEqual("Rejected", result["status"])
                self.assertIn("RequiredAttributeMissing", [x["key"] for x in result["diagnostics"]])
                self.assertNotIn("product_structure", result)

    def test_legacy_security_classification_can_reference_assembly_usage(self):
        data = append_record(legacy_graph_source(),
                             b"#950=SECURITY_CLASSIFICATION_LEVEL('unclassified');\n"
                             b"#951=SECURITY_CLASSIFICATION('synthetic','',#950);\n"
                             b"#952=CC_DESIGN_SECURITY_CLASSIFICATION(#951,(#910));")
        graph = self.inspect_structure(data)
        self.assertEqual("203-N916-Source-0.1", graph["basis"]["profile"])
        self.assertFalse(graph["basis"]["header_edition_verified"])
        self.assertEqual("910", graph["usages"][0]["usage"])

    def test_single_part_keeps_existing_semantic_admission(self):
        data = ap214_source((CORPUS / "S203-P04/input.step").read_bytes())
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual("Checked", result["status"])
        self.assertNotIn("product_structure", result)
        self.assertIsNotNone(result["snapshot"])

    def test_large_source_labels_and_record_order_do_not_change_product_identity(self):
        data = graph_source(usage=True)
        offset = 10**30
        data = re.sub(rb"#([0-9]+)", lambda m: b"#" + str(offset + int(m[1])).encode("ascii"), data)
        head, body = data.split(b"DATA;\n")
        records, tail = body.split(b"ENDSEC;")
        data = head + b"DATA;\n" + b"\n".join(reversed(records.splitlines())) + b"\nENDSEC;" + tail
        graph = self.inspect_structure(data)
        self.assertEqual({str(offset + 7), str(offset + 902)}, {x["definition"] for x in graph["definitions"]})
        self.assertEqual([str(offset + 7)], graph["usage_roots"])
        self.assertEqual(str(offset + 910), graph["usages"][0]["usage"])

    def test_structure_budget_is_not_an_excuse_to_publish_partial_graphs(self):
        data = graph_source(usage=True)
        run, result = self.invoke(data)
        self.assertIn("product_structure", result)
        visits = result["statistics"]["dependency_visits"]
        run, stopped = self.invoke(data, "--max-semantic-visits", str(visits - 1))
        self.assertNotEqual(0, run.returncode)
        self.assertEqual("ResourceLimit", stopped["status"])
        self.assertNotIn("product_structure", stopped)
        self.assertIsNone(stopped["snapshot"])


if __name__ == "__main__":
    unittest.main()
