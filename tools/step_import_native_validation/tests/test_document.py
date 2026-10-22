# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent definitions use one source/report owner and one publication decision."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
FIXTURES = ROOT / "source/STEP_IMPORT/tests/fixtures"


class DocumentTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix="step-document-") as directory:
            source = Path(directory) / "source.step"
            source.write_bytes(data)
            p = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                                "Q-AP214-development-mm-01", "--independent-products", "--compact-source-report",
                                str(source), *flags], capture_output=True, timeout=30)
        return p, json.loads(p.stdout) if p.stdout else None

    def test_different_unit_contexts_publish_all_definitions_with_one_source_report(self):
        data = (FIXTURES / "independent_two_parts.step").read_bytes()
        p, d = self.invoke(data)
        self.assertEqual(0, p.returncode, p.stderr or d.get("diagnostics"))
        self.assertEqual("IndependentProductDefinitions", d["scope"])
        self.assertTrue(d["product_document"]["complete"])
        definitions = d["product_document"]["definitions"]
        self.assertEqual(["7", "902"], [x["source_definition_id"] for x in definitions])
        self.assertTrue(all(x["checks_passed"] for x in definitions))
        checks = [x["checks"] for x in definitions]
        self.assertEqual([6000, 6000000], [round(x["model"]["material_volume"]["value_mm3"]) for x in checks])
        self.assertEqual(d["native_calls"], sum(x["native_calls"] for x in checks))
        self.assertTrue(all(x["source_inspection"] is None and x["source_evidence"] == "ProductDocumentRoot" for x in checks))
        self.assertIsNone(d["source_inspection"]["snapshot"])
        self.assertEqual(2, len(d["source_inspection"]["document"]["parts"]))
        self.assertEqual(data.decode("ascii"), d["source_inspection"]["source"]["input"]["raw"])

    def test_later_native_failure_and_call_limit_do_not_publish_partial_document(self):
        data = (FIXTURES / "independent_two_parts.step").read_bytes()
        _, success = self.invoke(data)
        first_calls = success["product_document"]["definitions"][0]["checks"]["native_calls"]
        for changed, flags in ((data, ("--max-native-calls", str(first_calls))),
                               (data.replace(b"SI_UNIT(.CENTI.,.METRE.)", b"SI_UNIT(.ATTO.,.METRE.)"), ())):
            with self.subTest(flags=flags):
                p, d = self.invoke(changed, *flags)
                self.assertNotEqual(0, p.returncode)
                self.assertFalse(d["product_document"]["complete"])
                self.assertTrue(d["candidate_discarded"])
                self.assertTrue(d["product_document"]["definitions"][0]["checks_passed"])

    def test_mixed_per_definition_collections_keep_all_source_bodies(self):
        data = (FIXTURES / "independent_distinct_parts.step").read_bytes()
        data = data.replace(b"'body',(#21)", b"'body',(#21,#10021)")
        p, d = self.invoke(data)
        self.assertEqual(0, p.returncode, p.stderr or d.get("diagnostics"))
        definitions = d["product_document"]["definitions"]
        collection = definitions[0]["checks"]["material_collection"]
        self.assertEqual(2, collection["selected_body_count"])
        self.assertEqual(["21", "10021"], [x["source_shape_id"] for x in collection["bodies"]])
        self.assertTrue(collection["complete"])


if __name__ == "__main__":
    unittest.main()
