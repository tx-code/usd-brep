# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Whole independent-definition source qualification; no surviving-part fallback."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_product_structure import graph_source, append_record


def two_contexts():
    data = graph_source().replace(b"#904=SHAPE_DEFINITION_REPRESENTATION(#903,#20)",
                                 b"#904=SHAPE_DEFINITION_REPRESENTATION(#903,#920)")
    return append_record(data,
                         b"#920=ADVANCED_BREP_SHAPE_REPRESENTATION('child',(#21),#921);\n"
                         b"#921=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#922,#11)) "
                         b"REPRESENTATION_CONTEXT('second units','3D'));\n"
                         b"#922=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.CENTI.,.METRE.));")


class IndependentProductsTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix="step-independent-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            p = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path),
                                "--independent-products", *flags], capture_output=True, timeout=30)
        return p, json.loads(p.stdout) if p.stdout else None

    def test_all_definitions_are_qualified_without_merging_shared_source_geometry(self):
        data = graph_source()
        p, d = self.invoke(data)
        self.assertEqual(0, p.returncode, p.stderr or d.get("diagnostics"))
        self.assertIsNone(d["snapshot"])
        self.assertEqual("IndependentDefinitionsQualified", d["document"]["qualification"])
        parts = d["document"]["parts"]
        self.assertEqual(["7", "902"], [x["roles"]["definition"] for x in parts])
        self.assertEqual(["21", "21"], [x["shapes"][0]["shape"] for x in parts])
        self.assertEqual(data.decode("ascii"), d["source"]["input"]["raw"])

    def test_each_representation_keeps_its_own_physical_unit_context(self):
        p, d = self.invoke(two_contexts())
        self.assertEqual(0, p.returncode, p.stderr or d.get("diagnostics"))
        parts = d["document"]["parts"]
        self.assertEqual(["12", "921"], [x["roles"]["unit_context"] for x in parts])
        self.assertEqual(["10", "922"], [x["units"]["length_unit"] for x in parts])
        self.assertNotEqual(parts[0]["units"]["length_scale_mm"], parts[1]["units"]["length_scale_mm"])

    def test_later_unit_failure_and_unowned_content_withhold_the_whole_document(self):
        for data in (two_contexts().replace(b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#922,#11))",
                                            b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#922))"),
                     append_record(graph_source(), b"#950=MANIFOLD_SOLID_BREP('unowned',#22);")):
            p, d = self.invoke(data)
            self.assertNotEqual(0, p.returncode)
            self.assertIsNone(d["snapshot"])
            self.assertNotIn("document", d)

    def test_assembly_usage_is_never_flattened_to_independent_roots(self):
        p, d = self.invoke(graph_source(usage=True))
        self.assertNotEqual(0, p.returncode)
        self.assertIn("IndependentProductsContainAssembly", [x["key"] for x in d["diagnostics"]])
        self.assertNotIn("document", d)

    def test_shared_category_membership_keeps_the_original_product_set(self):
        data = append_record(graph_source(), b"#950=PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,(#4,#900));")
        p, d = self.invoke(data)
        self.assertEqual(0, p.returncode, p.stderr or d.get("diagnostics"))
        self.assertEqual(["950", "950"], [x["roles"]["category"] for x in d["document"]["parts"]])
        self.assertEqual(data.decode("ascii"), d["source"]["input"]["raw"])


if __name__ == "__main__":
    unittest.main()
