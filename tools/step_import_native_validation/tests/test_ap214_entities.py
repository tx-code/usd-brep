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


class Ap214EntityBoundaryTests(unittest.TestCase):
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
