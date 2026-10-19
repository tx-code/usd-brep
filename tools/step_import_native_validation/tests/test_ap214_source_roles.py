# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Presentation visibility and unattached dictionaries never remove native geometry."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP214, CORPUS, ap214_source
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX
from tools.step_semantic_validation.tests.test_ap214_entities import append_records
from tools.step_semantic_validation.tests.test_ap214_source_roles import category_source


class NativeAp214SourceRoleTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix="native-step-source-roles-") as folder:
            path = Path(folder) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                                  "Q-AP214-development-mm-01", str(path), *flags], capture_output=True, timeout=30)
        self.assertTrue(run.stdout, run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])
        return run, report

    def test_hidden_body_or_layer_keeps_complete_material_geometry(self):
        for schema in (AP214, MBX):
            base = ap214_source((CORPUS / "S203-P04/input.step").read_bytes()).replace(AP214, schema)
            run, original = self.invoke(base)
            self.assertEqual(0, run.returncode)
            variants = [append_records(base, b"#950=INVISIBILITY((#20));\n")]
            if schema == MBX:
                variants.append(append_records(base,
                    b"#951=PRESENTATION_LAYER_ASSIGNMENT('layer','retained',(#21));\n#950=INVISIBILITY((#951));\n"))
            for data in variants:
                with self.subTest(schema=schema):
                    run, report = self.invoke(data)
                    self.assertEqual(0, run.returncode, report["diagnostics"])
                    self.assertEqual(original["model"], report["model"])
                    self.assertEqual(original["native_calls"], report["native_calls"])
                    self.assertIn("950", report["source_inspection"]["snapshot"]["part"]["roles"].values())

    def test_category_dictionary_needs_interoperability_but_never_gains_product_membership(self):
        for schema in (AP214, MBX):
            strict, rejected = self.invoke(category_source(schema))
            self.assertEqual(2, strict.returncode)
            self.assertIsNone(rejected["model"])
            run, report = self.invoke(category_source(schema), "--interop-nongeometric")
            self.assertEqual(0, run.returncode, report["diagnostics"])
            self.assertAlmostEqual(6000., report["model"]["material_volume"]["value_mm3"], places=5)
            self.assertEqual(["900", "901", "902"],
                             report["source_inspection"]["snapshot"]["unattached_non_geometric_declarations"])
            self.assertNotIn("category", report["source_inspection"]["snapshot"]["part"]["roles"])

    def test_cycle_or_invalid_visibility_prevents_any_native_call(self):
        base = ap214_source((CORPUS / "S203-P04/input.step").read_bytes())
        data = append_records(base, b"#950=INVISIBILITY((#21));\n")
        cycle = category_source().replace(b"#900,#901", b"#900,#900")
        for invalid in (data, cycle):
            run, report = self.invoke(invalid, "--interop-nongeometric")
            self.assertEqual(1, run.returncode)
            self.assertIsNone(report["model"])
            self.assertEqual(0, report["native_calls"])
