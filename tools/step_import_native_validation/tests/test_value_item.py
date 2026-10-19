# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Property value checks cannot change shape, units, ownership or publication."""
import unittest

from tools.step_import_native_validation.tests import test_wireframe as wireframe_tests
from tools.step_semantic_validation.tests.test_ap214 import AP214
from tools.step_semantic_validation.tests.test_ap214_curve_style import MBX
from tools.step_semantic_validation.tests.test_ap214_entities import append_records
from tools.step_semantic_validation.tests.test_value_item import value_source


class ValueItemNativeTests(unittest.TestCase):
    invoke = wireframe_tests.LinkedWireframeNativeTests.invoke

    def test_typed_values_and_context_survive_without_affecting_material_geometry(self):
        for schema in (AP214, MBX):
            for value in (b"COUNT_MEASURE(-7)", b"NUMERIC_MEASURE(123456789012345678901234567890)",
                          b"DESCRIPTIVE_MEASURE('unaltered source metadata')"):
                run, report = self.invoke(value_source(value, schema), write=True)
                self.assertEqual(0, run.returncode, run.stderr or report["diagnostics"])
                self.assertEqual("Accepted", report["status"])
                self.assertEqual((6, 12, 8), tuple(report["model"][k] for k in ("faces", "edges", "vertices")))
                self.assertAlmostEqual(6000., report["model"]["material_volume"]["value_mm3"], places=5)
                self.assertEqual("12", report["source_inspection"]["snapshot"]["part"]["property_attachments"][0]["context"])

    def test_invalid_context_and_numeric_type_reject_before_native_allocation(self):
        context = append_records(value_source().replace(b"'source values',(#901),#12",
                                                        b"'source values',(#901),#904"),
                                 b"#904=REPRESENTATION_CONTEXT('metadata','no units');\n")
        for data in (context, value_source(b"POSITIVE_LENGTH_MEASURE(0.)")):
            run, report = self.invoke(data, "--interop-nongeometric")
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(report["model"])
            self.assertEqual(0, report["native_calls"])

    def test_existing_explicit_non_geometric_ratio_exception_retains_diagnostic(self):
        data = value_source(b"POSITIVE_RATIO_MEASURE(0.)")
        run, strict = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(strict["model"])
        run, report = self.invoke(data, "--interop-nongeometric", write=True)
        self.assertEqual(0, run.returncode, report["diagnostics"])
        self.assertEqual("CheckedWithDiagnostics", report["source_inspection"]["status"])
        self.assertIn("SourceRuleViolation:POSITIVE_RATIO_MEASURE.WR1",
                      [x["key"] for x in report["source_inspection"]["preserved_diagnostics"]])
        self.assertEqual(data.decode(), report["source_inspection"]["source"]["input"]["raw"])

    def test_interpreted_binding_is_preserved(self):
        data = value_source(schema=MBX).replace(MBX, b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1 1}")
        run, report = self.invoke(data, "--interop-nongeometric", "--interpret-unregistered-ap214-2010", write=True)
        self.assertEqual(11, run.returncode, run.stderr or report["diagnostics"])
        self.assertEqual("AcceptedUnderInterpretation", report["status"])
        self.assertFalse(report["source_binding_verified"])
