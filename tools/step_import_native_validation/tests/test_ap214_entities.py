# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Presentation/source-link support does not qualify AP214 rules or native models."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214_entities import linked_source, presentation_source
from tools.step_semantic_validation.tests.test_ap214_geometry import geometry_source
from tools.step_semantic_validation.tests.test_ap214_units import unit_source
from tools.step_semantic_validation.tests.test_ap214_measure import measure_source


class Ap214EntityBoundaryTests(unittest.TestCase):
    def test_measure_item_source_check_does_not_publish_a_native_model(self):
        with tempfile.TemporaryDirectory(prefix="step-ap214-measure-native-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(measure_source())
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality", "Q-P0-mm-01",
                                  str(path)], capture_output=True, timeout=15)
            result = json.loads(run.stdout)
            self.assertEqual(2, run.returncode)
            self.assertEqual("Checked", result["source"]["semantics"])
            self.assertEqual("UnsupportedRuleProfile", result["diagnostics"][0]["key"])
            self.assertEqual(0, result["native_calls"])
            self.assertIsNone(result["model"])

    def test_derived_dimensions_do_not_grant_native_model_qualification(self):
        with tempfile.TemporaryDirectory(prefix="step-ap214-units-native-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(unit_source())
            command = [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality", "Q-P0-mm-01", str(path)]
            run = subprocess.run(command, capture_output=True, timeout=15)
            result = json.loads(run.stdout)
            self.assertEqual(2, run.returncode)
            self.assertEqual("Checked", result["source"]["semantics"])
            self.assertEqual("UnsupportedRuleProfile", result["diagnostics"][0]["key"])
            self.assertEqual(0, result["native_calls"])
            self.assertIsNone(result["model"])

    def test_source_geometry_parameters_do_not_qualify_native_geometry(self):
        for kind in ("CIRCLE", "ELLIPSE", "CYLINDRICAL_SURFACE", "CONICAL_SURFACE",
                     "B_SPLINE_CURVE_WITH_KNOTS", "B_SPLINE_SURFACE_WITH_KNOTS"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory(
                    prefix="step-ap214-geometry-native-") as directory:
                data = geometry_source(kind)
                path = Path(directory) / "source.step"
                path.write_bytes(data)
                run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality", "Q-P0-mm-01",
                                      str(path)], capture_output=True, timeout=15)
                result = json.loads(run.stdout)
                self.assertEqual(2, run.returncode, result.get("diagnostics"))
                self.assertEqual("Checked", result["source"]["semantics"])
                self.assertEqual("UnsupportedRuleProfile", result["diagnostics"][0]["key"])
                self.assertEqual(0, result["native_calls"])
                self.assertIsNone(result["model"])

    def test_presentation_and_linked_source_stop_before_protocol_rules_and_native_build(self):
        for data in (presentation_source(), linked_source()):
            with tempfile.TemporaryDirectory(prefix="step-ap214-native-") as directory:
                path = Path(directory) / "source.step"
                path.write_bytes(data)
                results = {}
                for stage, variable in (("planar", "STEP_PLANAR_BINARY"), ("native", "STEP_NATIVE_BINARY")):
                    command = [os.environ[variable], "--fixture-limits", "--quality", "Q-P0-mm-01", str(path)]
                    run = subprocess.run(command, capture_output=True, timeout=15)
                    result = json.loads(run.stdout)
                    self.assertEqual(2, run.returncode, run.stderr or result.get("diagnostics"))
                    self.assertEqual("UnsupportedRuleProfile", result["diagnostics"][0]["key"])
                    self.assertIsNone(result["model"])
                    results[stage] = result
                self.assertEqual("Checked", results["planar"]["source_semantics"]["status"])
                self.assertEqual([], results["planar"]["rules"])
                self.assertEqual(0, results["planar"]["work"])
                for name in ("local_rules", "global_rules_47", "planar_regions", "conversion_units", "native_build"):
                    self.assertEqual("NotRun", results["planar"]["checks"][name])
                native = results["native"]
                self.assertEqual("Checked", native["source"]["semantics"])
                self.assertEqual(hashlib.sha256(data).hexdigest(), native["source"]["sha256"])
                self.assertEqual(0, native["native_calls"])
                self.assertEqual("SourceDeclarationsPartUnitsOnly",
                                 native["source_schema"]["qualified_profile"]["scope"])
