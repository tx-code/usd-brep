# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""AP214 category dictionaries and visibility retain source meaning and ownership."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214, CORPUS, ap214_source
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX
from tools.step_semantic_validation.tests.test_ap214_entities import append_records, presentation_source


def category_source(schema=AP214):
    data = ap214_source((CORPUS / "S203-P04/input.step").read_bytes()).replace(AP214, schema)
    return append_records(data, b"#900=PRODUCT_CATEGORY('parent dictionary',$);\n"
                          b"#901=PRODUCT_CATEGORY('child dictionary','source description');\n"
                          b"#902=PRODUCT_CATEGORY_RELATIONSHIP('source hierarchy',$,#900,#901);\n")


def invisible_source(schema=AP214):
    return append_records(presentation_source().replace(AP214, schema), b"#950=INVISIBILITY((#901));\n")


class Ap214SourceRoleTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix="step-ap214-source-roles-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path),
                                  "--general-brep-source", *map(str, flags)], capture_output=True, timeout=20)
        self.assertTrue(run.stdout, run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(data.decode(), report["source"]["input"]["raw"])
        return run, report

    def test_unattached_dictionary_hierarchy_requires_explicit_interoperability(self):
        for schema in (AP214, MBX):
            with self.subTest(schema=schema):
                strict, rejected = self.invoke(category_source(schema))
                self.assertEqual(2, strict.returncode)
                self.assertEqual("UnownedOrOrphanProfileContent", rejected["diagnostics"][0]["key"])
                run, report = self.invoke(category_source(schema), "--interop-nongeometric")
                self.assertEqual(0, run.returncode, report["diagnostics"])
                self.assertEqual(rejected["diagnostics"], report["preserved_diagnostics"])
                self.assertEqual(["900", "901", "902"], report["snapshot"]["unattached_non_geometric_declarations"])
                self.assertNotIn("category", report["snapshot"]["part"]["roles"])
                records = {x["id"]: x for x in report["snapshot"]["records"]}
                self.assertEqual(["name", "description"], [x["name"] for x in records["900"]["attributes"]])
                self.assertEqual(["name", "description", "category", "sub_category"],
                                 [x["name"] for x in records["902"]["attributes"]])

    def test_category_cycle_is_source_invalid_including_under_interoperability(self):
        for data in (category_source().replace(b"#900,#901", b"#900,#900"),
                     append_records(category_source(),
                                    b"#903=PRODUCT_CATEGORY_RELATIONSHIP('back',$,#901,#900);\n")):
            for flags in ((), ("--interop-nongeometric",)):
                with self.subTest(flags=flags):
                    run, report = self.invoke(data, *flags)
                    self.assertEqual(1, run.returncode)
                    self.assertEqual("SourceRuleViolation:PRODUCT_CATEGORY_RELATIONSHIP.WR1",
                                     report["diagnostics"][0]["key"])
                    self.assertEqual("sub_category", report["diagnostics"][0]["field"])
                    self.assertIsNone(report["snapshot"])

    def test_diamond_shared_descendants_are_not_cycles(self):
        data = append_records(category_source(), b"#903=PRODUCT_CATEGORY('third',$);\n"
                              b"#904=PRODUCT_CATEGORY('fourth',$);\n"
                              b"#905=PRODUCT_CATEGORY_RELATIONSHIP('a',$,#900,#903);\n"
                              b"#906=PRODUCT_CATEGORY_RELATIONSHIP('b',$,#901,#904);\n"
                              b"#907=PRODUCT_CATEGORY_RELATIONSHIP('c',$,#903,#904);\n")
        run, report = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, report["diagnostics"])
        self.assertEqual(8, len(report["snapshot"]["unattached_non_geometric_declarations"]))

    def test_category_relationship_cannot_carry_geometry_and_derived_id_is_not_a_slot(self):
        for data in (category_source().replace(b"#900,#901", b"#900,#21"),
                     category_source().replace(b"'parent dictionary',$)", b"'parent dictionary',$,*)")):
            run, report = self.invoke(data, "--interop-nongeometric")
            self.assertEqual(1, run.returncode)
            self.assertIsNone(report["snapshot"])
            self.assertEqual([], report["preserved_diagnostics"])

    def test_invisibility_attaches_only_to_previously_owned_style_or_representation(self):
        for schema in (AP214, MBX):
            for target in (b"#901", b"#20", b"#900"):
                with self.subTest(schema=schema, target=target):
                    data = invisible_source(schema).replace(b"INVISIBILITY((#901))", b"INVISIBILITY((" + target + b"))")
                    run, report = self.invoke(data)
                    self.assertEqual(0, run.returncode, report["diagnostics"])
                    self.assertIn("950", [value for key, value in report["snapshot"]["part"]["roles"].items()
                                         if key.startswith("invisibility_")])
                    self.assertEqual("NotRun", report["checks"]["native_build"])

    def test_invisible_layer_uses_existing_complete_attachment_and_does_not_own_geometry(self):
        data = append_records(invisible_source(MBX).replace(b"INVISIBILITY((#901))", b"INVISIBILITY((#951))"),
                              b"#951=PRESENTATION_LAYER_ASSIGNMENT('source layer','source description',(#21));\n")
        run, report = self.invoke(data)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        bad = append_records(data.replace(b"'source description',(#21)", b"'source description',(#952)"),
                             b"#952=CARTESIAN_POINT('outside selection',(1.,2.,3.));\n")
        run, report = self.invoke(bad, "--interop-nongeometric")
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedAdministrativeAttachment", report["diagnostics"][0]["key"])
        self.assertIsNone(report["snapshot"])

    def test_invisibility_does_not_grant_an_orphan_style_ownership(self):
        data = invisible_source().replace(
            b"#900=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('display',(#901),#12);\n", b"")
        run, report = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedInvisibilityAttachment", report["diagnostics"][0]["key"])

    def test_invisibility_select_set_and_contextual_subtype_stay_closed(self):
        for replacement in (b"INVISIBILITY(())", b"INVISIBILITY((#901,#901))", b"INVISIBILITY((#21))"):
            run, report = self.invoke(invisible_source().replace(b"INVISIBILITY((#901))", replacement),
                                      "--interop-nongeometric")
            self.assertEqual(1, run.returncode)
            self.assertIsNone(report["snapshot"])
        data = invisible_source().replace(b"#950=INVISIBILITY", b"#950=CONTEXT_DEPENDENT_INVISIBILITY")
        run, report = self.invoke(data)
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedEntityType", report["diagnostics"][0]["key"])

    def test_exact_visit_budget_still_bounds_new_roles(self):
        for data in (category_source(), invisible_source()):
            run, report = self.invoke(data, "--interop-nongeometric")
            self.assertEqual(0, run.returncode)
            visits = report["statistics"]["dependency_visits"]
            run, report = self.invoke(data, "--interop-nongeometric", "--max-semantic-visits", visits)
            self.assertEqual(0, run.returncode, report["diagnostics"])
            run, report = self.invoke(data, "--interop-nongeometric", "--max-semantic-visits", visits - 1)
            self.assertEqual(3, run.returncode)
            self.assertIsNone(report["snapshot"])

    def test_other_source_protocols_do_not_inherit_new_declarations(self):
        for data in (category_source().replace(AP214, AP203), invisible_source().replace(AP214, AP203)):
            run, report = self.invoke(data, "--interop-nongeometric")
            self.assertEqual(2, run.returncode)
            self.assertEqual("UnsupportedEntityType", report["diagnostics"][0]["key"])

    def test_deep_hierarchy_is_bounded_without_recursive_stack_growth(self):
        data = category_source()
        records = []
        for index in range(1000):
            parent = 901 if index == 0 else 1000 + index - 1
            records.append(f"#{1000 + index}=PRODUCT_CATEGORY('node',$);\n"
                           f"#{3000 + index}=PRODUCT_CATEGORY_RELATIONSHIP('link',$,#{parent},#{1000 + index});\n")
        data = append_records(data, ''.join(records).encode())
        run, report = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, report["diagnostics"])
        cycle = append_records(data, b"#5000=PRODUCT_CATEGORY_RELATIONSHIP('back',$,#1999,#900);\n")
        run, report = self.invoke(cycle, "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertEqual("SourceRuleViolation:PRODUCT_CATEGORY_RELATIONSHIP.WR1", report["diagnostics"][0]["key"])
