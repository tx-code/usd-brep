# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Source angle admission must not invent an executable native angle scale."""
import math
import unittest

from tools.step_import_native_validation.tests import test_general as general
from tools.step_semantic_validation.tests.test_ap214 import AP214
from tools.step_semantic_validation.tests.test_ap214_angles import MBX, angle_context


class Ap214AngleNativeTests(unittest.TestCase):
    invoke = general.GeneralTests.invoke_bytes

    def test_cylinder_keeps_analytic_geometry_and_original_angle_context(self):
        # Full circles and a cylinder built from points/radius/axis do not use
        # a source angular parameter. Its unused source angle scale stays raw.
        for identifier in (AP214, MBX):
            with self.subTest(identifier=identifier):
                data = angle_context(general.cylinder_source()).replace(AP214, identifier)
                run, result = self.invoke(data)
                self.assertEqual(0, run.returncode, run.stderr or result["diagnostics"])
                self.assertEqual((3, 3, 2), tuple(result["model"][k] for k in ("faces", "edges", "vertices")))
                self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"],
                                       delta=63 * math.pi * 2e-4)
                self.assertAlmostEqual(60 * math.pi, result["model"]["area_mm2"], delta=60 * math.pi * 2e-4)
                self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, result["validations"])
                self.assertEqual(data.decode(), result["source_inspection"]["source"]["input"]["raw"])

    def test_cone_cannot_use_an_unestablished_conversion_angle_as_radians(self):
        data = angle_context(general.conical_frustum_source())
        run, result = self.invoke(data)
        self.assertEqual(9, run.returncode)
        self.assertIsNone(result["model"])
        self.assertTrue(result["candidate_discarded"])
        self.assertEqual("GeneralConeAngleUnitUnestablished", result["diagnostics"][-1]["key"])
        self.assertEqual(data.decode(), result["source_inspection"]["source"]["input"]["raw"])

    def test_bad_physical_factor_never_reaches_native_construction(self):
        data = angle_context(general.cylinder_source()).replace(b"E+0),#901", b"E+0),#10")
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(1, run.returncode)
        self.assertEqual(0, result["native_calls"])
        self.assertIsNone(result["model"])
        self.assertEqual("SourceRuleViolation:PLANE_ANGLE_UNIT.WR1",
                         result["source_inspection"]["diagnostics"][0]["key"])
