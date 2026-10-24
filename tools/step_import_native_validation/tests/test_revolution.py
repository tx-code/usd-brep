"""Exact revolution definitions retain generator parameters and independent geometry."""
import json
import math
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_import_native_validation.tests.test_general import cylinder_source
from tools.step_semantic_validation.tests.test_ap214_entities import append_records


def rational_revolution_source():
    # Same independent r=3, h=7 cylinder. A rational degree-one generator has
    # source parameter v in [-2,8], s=(v+2)/10, z=14*s/(1+s).
    # Angular zero starts at +Y, not the global +X axis. Parameters are not lengths.
    data = cylinder_source().replace(b"CYLINDRICAL_SURFACE('',#106,3.)", b"SURFACE_OF_REVOLUTION('rational generator',#942,#941)")
    return append_records(data, b"""#941=AXIS1_PLACEMENT('source z axis',#100,$);
#942=(BOUNDED_CURVE() B_SPLINE_CURVE(1,(#102,#103),.UNSPECIFIED.,.F.,.F.) B_SPLINE_CURVE_WITH_KNOTS((2,2),(-2.,8.),.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_CURVE((1.,2.)) REPRESENTATION_ITEM('original generator'));
""")


class RevolutionNativeTests(unittest.TestCase):
    def invoke(self, data):
        with tempfile.TemporaryDirectory(prefix="public-revolution-") as folder:
            path = Path(folder) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", str(path),
                                  "--quality", "Q-AP214-development-mm-01", "--integration-relative-accuracy", "1e-8"],
                                 capture_output=True, timeout=60)
            self.assertEqual(data, path.read_bytes())
        self.assertTrue(run.stdout, run.stderr)
        result = json.loads(run.stdout)
        return run, result

    def test_rational_generator_cylinder_is_not_fitted_or_retagged_as_analytic_cylinder(self):
        run, result = self.invoke(rational_revolution_source())
        self.assertEqual(0, run.returncode, run.stderr or result["diagnostics"])
        self.assertEqual("Accepted", result["status"])
        self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"], delta=1e-3)
        self.assertAlmostEqual(60 * math.pi, result["model"]["area_mm2"], delta=1e-3)
        self.assertTrue(any(m["kind"] == "RevolutionSourceParameters" for m in result["parameter_mappings"]))

    def test_frozen_cpp_fixture_is_the_same_independently_authored_source(self):
        fixture = Path(__file__).resolve().parents[3] / "source/STEP_IMPORT/tests/fixtures/rational_revolution_cylinder.step"
        self.assertEqual(rational_revolution_source().replace(b"\r\n", b"\n"), fixture.read_bytes().replace(b"\r\n", b"\n"))

    def test_plane_angle_prefix_changes_parameter_mapping_and_not_physical_geometry(self):
        data = rational_revolution_source().replace(b"SI_UNIT($,.RADIAN.)", b"SI_UNIT(.MILLI.,.RADIAN.)")
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, result["diagnostics"])
        self.assertAlmostEqual(63 * math.pi, result["model"]["material_volume"]["value_mm3"], delta=1e-3)
        mapping = next(m for m in result["parameter_mappings"] if m["kind"] == "RevolutionSourceParameters")
        self.assertAlmostEqual(.001 * 180 / math.pi, mapping["parameters"]["source_angle_to_native_degrees"][0])

    def test_unbounded_generator_is_not_arbitrarily_clipped_or_fitted(self):
        # Remove the now unused definition so this tests native domain scope
        # rather than failing the separate source ownership check first.
        data = rational_revolution_source().replace(b"#942,#941)", b"#112,#941)")
        data = re.sub(rb"#942=\([^\n]+\);\n", b"", data)
        run, result = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertEqual("Unsupported", result["status"])
        self.assertIn("GeneralRevolutionGeneratorUnsupported", {d["key"] for d in result["diagnostics"]})
