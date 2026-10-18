# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""N2635 general construction and diagnostic/physical-unit boundaries."""
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_import_native_validation.tests.test_general import cylinder_source
from tools.step_semantic_validation.tests.test_ap203_general import approval


def ap203_cylinder():
    return cylinder_source().replace(
        b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}",
        b"AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}").replace(
        b"'automotive_design',2010", b"'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011")


class Ap203GeneralNativeTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="ap203-general-native-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                "Q-AP214-development-mm-01", str(path), *map(str, options)], capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def test_n2635_circle_cylinder_build_has_independent_analytic_truth(self):
        run, result = self.invoke(ap203_cylinder())
        self.assertEqual(0, run.returncode, result["diagnostics"])
        self.assertEqual("203e2-G0-Source-0.1", result["source_inspection"]["schema_selection"]["qualified_profile"]["profile"])
        self.assertEqual((3, 3, 2), tuple(result["model"][key] for key in ("faces", "edges", "vertices")))
        self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"], delta=63 * math.pi * 2e-4)
        self.assertAlmostEqual(60 * math.pi, result["model"]["area_mm2"], delta=60 * math.pi * 2e-4)
        self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, result["validations"])

    def test_administrative_select_diagnostic_survives_native_publication(self):
        data = approval(7)
        strict_run, strict = self.invoke(data)
        self.assertEqual(1, strict_run.returncode)
        self.assertEqual(0, strict["native_calls"])
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, result["diagnostics"])
        self.assertEqual(6000, round(result["model"]["material_volume"]["value_mm3"], 5))
        self.assertEqual("ReferenceTypeMismatch", result["source_inspection"]["preserved_diagnostics"][0]["key"])
        self.assertEqual(data.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])

    def test_excess_projection_keeps_observation_and_discards_candidate(self):
        data = ap203_cylinder().replace(b"CYLINDRICAL_SURFACE('',#106,3.)", b"CYLINDRICAL_SURFACE('',#106,3.002)")
        self.assertNotEqual(data, ap203_cylinder())
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(9, run.returncode)
        self.assertIsNone(result["model"])
        self.assertTrue(result["candidate_discarded"])
        self.assertEqual("GeneralDerivedUvGap", result["diagnostics"][-1]["key"])
        gaps = [row["value_mm"] for row in result["measurements"] if row["stage"] == "UvProjection" and row["source_id"] == "182"]
        self.assertEqual(1, len(gaps))
        self.assertGreater(gaps[0], 0.001)

    def test_explicit_conversion_dimensions_and_positive_scale_remain_physical(self):
        original = ap203_cylinder()
        old = b"#11=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));"
        self.assertIn(old, original)
        definition = b"""#11=(CONVERSION_BASED_UNIT('degree',#194) NAMED_UNIT(#192) PLANE_ANGLE_UNIT());
#192=DIMENSIONAL_EXPONENTS(0.,0.,0.,0.,0.,0.,0.);
#193=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));
#194=PLANE_ANGLE_MEASURE_WITH_UNIT(PLANE_ANGLE_MEASURE(0.0174532925),#193);"""
        data = original.replace(old, definition)
        run, result = self.invoke(data, "--interop-nongeometric")
        self.assertEqual(0, run.returncode, result["diagnostics"])
        for broken, key in [
            (data.replace(b"#192=DIMENSIONAL_EXPONENTS(0.,", b"#192=DIMENSIONAL_EXPONENTS(1.,"),
             "SourceRuleViolation:CONVERSION_BASED_UNIT.WR1"),
            (data.replace(b"PLANE_ANGLE_MEASURE(0.0174532925)", b"PLANE_ANGLE_MEASURE(-0.0174532925)"),
             "SourceUnitScaleNonPositive"),
        ]:
            with self.subTest(key=key):
                run, result = self.invoke(broken, "--interop-nongeometric")
                self.assertEqual(1, run.returncode)
                self.assertEqual(0, result["native_calls"])
                self.assertIsNone(result["model"])
                self.assertEqual(key, result["source_inspection"]["diagnostics"][0]["key"])
