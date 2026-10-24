# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent spherical-patch truth for native boundary-pole qualification."""
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


FIXTURES = Path(__file__).resolve().parents[3] / "source/STEP_IMPORT/tests/fixtures"


class RevolutionPoleTests(unittest.TestCase):
    def invoke(self, name, *extra, source=None):
        source = (FIXTURES / name).read_bytes() if source is None else source
        with tempfile.TemporaryDirectory(prefix="public-revolution-poles-") as folder:
            path = Path(folder) / "input.step"
            path.write_bytes(source)
            result = subprocess.run(
                [os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", str(path),
                 "--quality", "Q-AP214-development-mm-01", "--integration-relative-accuracy", "1e-8", *extra],
                capture_output=True, timeout=60,
            )
            self.assertEqual(source, path.read_bytes())
        self.assertTrue(result.stdout, result.stderr)
        return result, json.loads(result.stdout)

    def test_both_generator_ends_and_two_poles_preserve_the_open_role(self):
        for suffix, area, pairs in (("patch", 8.1, 3), ("min_patch", 8.1, 3), ("lune", 16.2, 4)):
            with self.subTest(suffix=suffix):
                result, report = self.invoke(f"source_pcurve_revolution_pole_{suffix}.step")
                self.assertEqual(0, result.returncode, report["diagnostics"])
                self.assertEqual("Accepted", report["status"])
                self.assertEqual("OpenSurfaceModel", report["model"]["shape_role"])
                self.assertEqual("NotApplicable", report["model"]["material_volume"])
                self.assertAlmostEqual(area, report["model"]["area_mm2"], delta=1e-5)
                expected_length = 6 * math.pi if suffix == "lune" else 3 * math.pi + 2.7
                self.assertAlmostEqual(expected_length, report["model"]["boundary_mm"], delta=1e-6)
                self.assertEqual({"pointer_reports": 0, "level2_reports": 0}, report["validations"])
                self.assertTrue(report["revolution_pole_visits"])
                for proof in report["revolution_pole_visits"]:
                    self.assertEqual(pairs, proof["checked_edge_pole_pairs"])
                    self.assertGreater(proof["bezier_patches"], 0)
                    self.assertFalse(proof["source_exact_poles_qualified"])
                self.assertGreater(report["exact_geometry_work"]["used_units"], 0)
                self.assertEqual("None", report["repair"])

    def test_zero_exact_work_withholds_the_whole_model(self):
        result, report = self.invoke("source_pcurve_revolution_pole_patch.step", "--max-exact-work-units", "0")
        self.assertEqual(3, result.returncode)
        self.assertEqual("ResourceLimit", report["status"])
        self.assertIsNone(report["model"])
        self.assertTrue(any(item["key"] == "ExactGeometryWorkLimit" for item in report["diagnostics"]))
        self.assertEqual({"maximum_units": 0, "used_units": 0}, report["exact_geometry_work"])

    def test_declared_pcurve_retains_priority_and_continuous_composed_receipt(self):
        name = 'source_pcurve_revolution_pole_driven_patch.step'
        result, report = self.invoke(name)
        self.assertEqual(0, result.returncode, report['diagnostics'])
        self.assertEqual('Accepted', report['status'])
        self.assertEqual('None', report['repair'])
        self.assertNotIn('surface_curve_3d_interpretation', report)
        self.assertEqual('OpenSurfaceModel', report['model']['shape_role'])
        self.assertAlmostEqual(8.1, report['model']['area_mm2'], delta=1e-5)
        self.assertAlmostEqual(3 * math.pi + 2.7, report['model']['boundary_mm'], delta=1e-6)
        self.assertEqual({'pointer_reports': 0, 'level2_reports': 0}, report['validations'])
        proof = report['revolution_pole_visits'][0]
        self.assertEqual(3, proof['checked_edge_pole_pairs'])
        self.assertEqual(1, proof['composed_edge_pole_pairs'])
        self.assertGreater(proof['composed_interval_regions'], 0)
        self.assertGreater(proof['bezier_patches'], 0)
        self.assertEqual('OutwardIntervalDefiningUvRevolution', proof['composed_evidence'])
        self.assertFalse(proof['source_exact_poles_qualified'])
        self.assertEqual((FIXTURES / name).read_bytes().decode('utf-8'),
                         report['source_inspection']['source']['input']['raw'])
        units = report['exact_geometry_work']['used_units']
        for budget, expected in ((units, 'Accepted'), (units - 1, 'ResourceLimit')):
            with self.subTest(budget=budget):
                run, bounded = self.invoke(name, '--max-exact-work-units', str(budget))
                self.assertEqual(expected, bounded['status'])
                self.assertEqual(0 if expected == 'Accepted' else 3, run.returncode)
                self.assertEqual(expected == 'Accepted', bounded['model'] is not None)

    def test_zero_exact_work_does_not_change_the_existing_pole_free_path(self):
        result, report = self.invoke("source_pcurve_revolution_patch.step", "--max-exact-work-units", "0")
        self.assertEqual(0, result.returncode, report["diagnostics"])
        self.assertEqual("Accepted", report["status"])
        self.assertNotIn("exact_geometry_work", report)
        self.assertNotIn("revolution_pole_visits", report)

    def test_declared_outer_marker_cannot_bypass_the_pole_check(self):
        name = "source_pcurve_revolution_pole_patch.step"
        source = (FIXTURES / name).read_bytes().replace(b"#170=FACE_BOUND", b"#170=FACE_OUTER_BOUND")
        result, report = self.invoke(name, source=source)
        self.assertEqual(0, result.returncode, report["diagnostics"])
        self.assertTrue(report["revolution_pole_visits"])
        result, report = self.invoke(name, "--max-exact-work-units", "0", source=source)
        self.assertEqual(3, result.returncode)
        self.assertIsNone(report["model"])
        self.assertTrue(any(item["key"] == "ExactGeometryWorkLimit" for item in report["diagnostics"]))

    def test_unqualified_normalization_compositions_refuse_without_publication(self):
        for option in ("--normalize-periodic-faces", "--restrict-unused-support-domains"):
            with self.subTest(option=option):
                result, report = self.invoke("source_pcurve_revolution_pole_patch.step", option)
                self.assertEqual(2, result.returncode)
                self.assertIsNone(report["model"])
                # The existing periodic-surface scope guard can refuse this
                # family before any native pole candidate is constructed.
                expected = ("GeneralSurfaceConfigurationUnestablished" if option == "--normalize-periodic-faces"
                            else "GeneralRevolutionPoleNormalizationCompositionUnestablished")
                self.assertTrue(any(item["key"] == expected for item in report["diagnostics"]))


if __name__ == "__main__":
    unittest.main()
