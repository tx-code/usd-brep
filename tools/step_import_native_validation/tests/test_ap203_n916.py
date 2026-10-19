# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Complete native checks retain the legacy implementation/header distinction."""
import math
import unittest

from tools.step_import_native_validation.tests import test_general as general
from tools.step_semantic_validation.tests.test_ap203_n916 import layer_source, legacy_source
from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214


class Ap203N916NativeTests(unittest.TestCase):
    invoke = general.GeneralTests.invoke_bytes

    def test_legacy_box_has_independent_geometry_and_unchanged_source(self):
        data = legacy_source()
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, result["diagnostics"])
        self.assertEqual((6, 12, 8), tuple(result["model"][k] for k in ("faces", "edges", "vertices")))
        self.assertAlmostEqual(2200., result["model"]["area_mm2"], delta=2200 * 2e-4)
        self.assertAlmostEqual(6000., result["model"]["material_volume"]["value_mm3"], delta=6000 * 2e-4)
        self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, result["validations"])
        self.assertEqual(data.decode(), result["source_inspection"]["source"]["input"]["raw"])
        profile = result["source_inspection"]["schema_selection"]["qualified_profile"]
        self.assertEqual("203-N916-Source-0.1", profile["profile"])
        self.assertIs(False, profile["header_edition_verified"])

    def test_legacy_cylinder_preserves_shared_curves_and_analytic_truth(self):
        data = legacy_source(general.cylinder_source().replace(AP214, AP203).replace(
            b"'automotive_design',2010",
            b"'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011"))
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, result["diagnostics"])
        self.assertEqual((3, 3, 2), tuple(result["model"][k] for k in ("faces", "edges", "vertices")))
        self.assertAlmostEqual(60 * math.pi, result["model"]["area_mm2"], delta=60 * math.pi * 2e-4)
        self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"], delta=63 * math.pi * 2e-4)
        self.assertEqual(data.decode(), result["source_inspection"]["source"]["input"]["raw"])

    def test_foreign_layer_requires_policy_and_survives_complete_publication(self):
        data = layer_source()
        strict_run, strict = self.invoke(data)
        self.assertEqual(2, strict_run.returncode)
        self.assertEqual(0, strict["native_calls"])
        self.assertIsNone(strict["model"])
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, result["diagnostics"])
        self.assertEqual("UnsupportedEntityType", result["source_inspection"]["preserved_diagnostics"][0]["key"])
        self.assertAlmostEqual(6000., result["model"]["material_volume"]["value_mm3"], delta=6000 * 2e-4)

    def test_source_failure_cannot_publish_a_native_candidate(self):
        data = legacy_source().replace(b"PRODUCT_DEFINITION('D1','',", b"PRODUCT_DEFINITION('D1',$,")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertEqual(0, result["native_calls"])
        self.assertIsNone(result["model"])

    def test_missing_apd_diagnostic_survives_native_publication(self):
        data = b"\n".join(line for line in layer_source().split(b"\n") if not line.startswith(b"#2="))
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, result["diagnostics"])
        issues = result["source_inspection"]["preserved_diagnostics"]
        self.assertEqual({"UnsupportedSinglePartGraph", "UnsupportedEntityType"}, {d["key"] for d in issues})
        self.assertEqual(data.decode(), result["source_inspection"]["source"]["input"]["raw"])
        self.assertAlmostEqual(6000., result["model"]["material_volume"]["value_mm3"], delta=6000 * 2e-4)
