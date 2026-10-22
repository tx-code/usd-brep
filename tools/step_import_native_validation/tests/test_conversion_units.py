# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""General length normalization has exact source-ratio and analytic shape truth."""
from fractions import Fraction
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.step_import_native_validation.tests.test_general import CORPUS, bulged_box_source


def conversion_source(*, ap214=False, nested=False, factor="0.0254", si_prefix="$", data=None):
    data = data or (CORPUS / "S203-P04/input.step").read_bytes()
    if ap214:
        data = data.replace(
            b"AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}",
            b"AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}")
        data = data.replace(
            b"'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011",
            b"'automotive_design',2010")
    highest = max(int(x) for x in re.findall(rb"#([0-9]+)\s*=", data))
    measure, dimensions, base, middle, inner = range(highest + 1, highest + 6)
    dim = "*" if ap214 else f"#{dimensions}"
    root = f"#10=(CONVERSION_BASED_UNIT('arbitrary label',#{measure}) LENGTH_UNIT() NAMED_UNIT({dim}));"
    original = b"#10=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.));"
    assert data.count(original) == 1
    data = data.replace(original, root.encode("ascii"))
    records = [f"#{base}=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT({si_prefix},.METRE.));\n"]
    if not ap214:
        records.append(f"#{dimensions}=DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.);\n")
    if nested:
        records += [f"#{middle}=(CONVERSION_BASED_UNIT('also not a unit name',#{inner}) "
                    f"LENGTH_UNIT() NAMED_UNIT({dim}));\n",
                    f"#{inner}=MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0127),#{base});\n",
                    f"#{measure}=MEASURE_WITH_UNIT(LENGTH_MEASURE(2.),#{middle});\n"]
    else:
        records.append(f"#{measure}=MEASURE_WITH_UNIT(LENGTH_MEASURE({factor}),#{base});\n")
    return data.replace(b"ENDSEC;\nEND-ISO", "".join(records).encode("ascii") + b"ENDSEC;\nEND-ISO")


def rational(value):
    return Fraction(int(value["num"]), int(value["den"]))


class ConversionUnitTests(unittest.TestCase):
    def invoke(self, data, *flags):
        with tempfile.TemporaryDirectory(prefix="step-conversion-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_NATIVE_BINARY"], "--fixture-limits", "--quality",
                                  "Q-AP214-development-mm-01", str(path), *map(str, flags)],
                                 capture_output=True, timeout=30)
        return run, json.loads(run.stdout) if run.stdout else None

    def assert_inch_box(self, result):
        scale = Fraction(127, 5)
        model = result["model"]
        self.assertEqual((6, 12, 8), (model["faces"], model["edges"], model["vertices"]))
        for actual, expected in ((model["material_volume"]["value_mm3"], 6000 * scale**3),
                                 (model["area_mm2"], 2200 * scale**2), (model["boundary_mm"], 240 * scale)):
            self.assertAlmostEqual(float(expected), actual, delta=max(1e-7, float(expected) * 1e-10))
        for actuals, source in zip(model["bounds_mm"], ((2, -3, 5), (12, 17, 35))):
            for actual, coordinate in zip(actuals, source):
                self.assertAlmostEqual(float(coordinate * scale), actual, delta=1e-6)
        self.assertEqual(scale, rational(result["unit_normalization"]["length_to_mm"]))
        self.assertEqual("DimensionlessUnchanged", result["unit_normalization"]["line_parameter"])
        self.assertEqual("Dimensionless", result["unit_normalization"]["direction_ratios"])

    def test_exact_conversion_in_ap203_and_derived_slot_ap214(self):
        for ap214 in (False, True):
            with self.subTest(ap214=ap214):
                data = conversion_source(ap214=ap214)
                run, result = self.invoke(data)
                self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                self.assert_inch_box(result)
                definitions = result["unit_normalization"]["definitions"]
                self.assertEqual(1, len(definitions))
                self.assertEqual(Fraction(127, 5000), rational(definitions[0]["factor_value"]))
                self.assertNotIn("source_dimensions", definitions[0])
                self.assertEqual(data.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])

    def test_nested_factors_are_applied_once_in_dependency_order(self):
        data = conversion_source(ap214=True, nested=True)
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assert_inch_box(result)
        definitions = result["unit_normalization"]["definitions"]
        self.assertEqual([Fraction(127, 10000), Fraction(2)],
                         [rational(x["factor_value"]) for x in definitions])
        self.assertEqual([Fraction(127, 10), Fraction(127, 5)],
                         [rational(x["scale_mm"]) for x in definitions])
        self.assertEqual(definitions[0]["source_unit"], definitions[1]["source_base_unit"])

    def test_si_prefix_and_unit_labels_do_not_change_the_physical_model(self):
        first, a = self.invoke(conversion_source(ap214=True))
        second, b = self.invoke(conversion_source(ap214=True, factor="25.4", si_prefix=".MILLI.")
                                .replace(b"arbitrary label", b"METRE"))
        self.assertEqual((0, 0), (first.returncode, second.returncode))
        self.assertEqual(a["model"], b["model"])
        self.assert_inch_box(a)

    def test_nonpositive_physical_scales_are_not_used(self):
        for value in ("0.", "-0.0254"):
            with self.subTest(value=value):
                run, result = self.invoke(conversion_source(ap214=True, factor=value))
                self.assertNotEqual(0, run.returncode)
                self.assertIsNone(result["model"])
                self.assertEqual(0, result["native_calls"])
                self.assertIn("GeneralUnitScaleNotPositive", [x["key"] for x in result["diagnostics"]])

    def test_exact_capacity_exhaustion_is_indeterminate(self):
        run, result = self.invoke(conversion_source(ap214=True, factor="1.E100"))
        self.assertNotEqual(0, run.returncode)
        self.assertEqual("Indeterminate", result["status"])
        self.assertIsNone(result["model"])
        self.assertEqual(0, result["native_calls"])
        self.assertIn("GeneralUnitScaleRange", [x["key"] for x in result["diagnostics"]])

    def test_wide_source_factor_retains_exact_representable_millimetre_scale(self):
        # Constructed decimal: its denominator exceeds int64, but multiplication
        # by the SI metre's 1000 mm cancels three powers of ten exactly.
        literal = "0.001234567890123456789"
        factor = Fraction(literal)
        scale = factor * 1000
        self.assertGreater(factor.denominator, 2**63 - 1)
        for ap214 in (False, True):
            with self.subTest(ap214=ap214):
                data = conversion_source(ap214=ap214, factor=literal)
                run, result = self.invoke(data)
                self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                units = result["unit_normalization"]
                self.assertEqual(scale, rational(units["length_to_mm"]))
                self.assertEqual(factor, rational(units["definitions"][0]["factor_value"]))
                self.assertEqual(scale, rational(units["definitions"][0]["scale_mm"]))
                self.assertEqual(data.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])
                model = result["model"]
                self.assertEqual((6, 12, 8), (model["faces"], model["edges"], model["vertices"]))
                for actual, expected in ((model["material_volume"]["value_mm3"], 6000 * scale**3),
                                         (model["area_mm2"], 2200 * scale**2)):
                    self.assertAlmostEqual(float(expected), actual, delta=float(expected) * 1e-10)

    def test_wide_intermediate_scale_cancels_before_native_narrowing(self):
        for power in (30, 100, 1233):
            with self.subTest(power=power):
                data = conversion_source(ap214=True, nested=True, si_prefix=".MILLI.")
                data = data.replace(b"LENGTH_MEASURE(0.0127)", f"LENGTH_MEASURE(1.E{power})".encode("ascii"))
                data = data.replace(b"LENGTH_MEASURE(2.)", f"LENGTH_MEASURE(1.E-{power})".encode("ascii"))
                run, result = self.invoke(data)
                self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                units = result["unit_normalization"]
                self.assertEqual(Fraction(1), rational(units["length_to_mm"]))
                self.assertEqual([Fraction(10**power), Fraction(1)],
                                 [rational(x["scale_mm"]) for x in units["definitions"]])
                self.assertEqual([Fraction(10**power), Fraction(1, 10**power)],
                                 [rational(x["factor_value"]) for x in units["definitions"]])
                self.assertAlmostEqual(6000, result["model"]["material_volume"]["value_mm3"], delta=1e-7)
                self.assertEqual(data.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])

    def test_wide_arithmetic_still_refuses_capacity_and_nonpositive_factors(self):
        for literal, key, status in (("1.E1234", "GeneralUnitScaleRange", "Indeterminate"),
                                     ("1.E-1234", "GeneralUnitScaleRange", "Indeterminate"),
                                     ("1.E999999999999999999999999999999", "GeneralUnitScaleRange", "Indeterminate"),
                                     ("-0.001234567890123456789", "GeneralUnitScaleNotPositive", "Unsupported")):
            with self.subTest(literal=literal):
                data = conversion_source(ap214=True, factor=literal)
                run, result = self.invoke(data)
                self.assertNotEqual(0, run.returncode)
                self.assertEqual(status, result["status"])
                self.assertIsNone(result["model"])
                self.assertEqual(0, result["native_calls"])
                self.assertIn(key, [x["key"] for x in result["diagnostics"]])
                self.assertEqual(data.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])

    def test_cycle_is_still_a_source_failure_before_native_allocation(self):
        data = conversion_source(ap214=True)
        data = re.sub(rb"(MEASURE_WITH_UNIT\(LENGTH_MEASURE\(0.0254\),)#\d+", rb"\g<1>#10", data)
        run, result = self.invoke(data)
        self.assertNotEqual(0, run.returncode)
        self.assertIsNone(result["model"])
        self.assertEqual(0, result["native_calls"])
        self.assertIn("SourceUnitDependencyCycle", [x["key"] for x in result["diagnostics"]])

    def test_source_order_and_large_labels_do_not_select_a_different_scale(self):
        data = conversion_source(ap214=True, nested=True)
        data = re.sub(rb"#([0-9]+)", lambda m: b"#" + str(10**30 + int(m[1])).encode("ascii"), data)
        head, tail = data.split(b"DATA;\n")
        body, end = tail.split(b"ENDSEC;")
        data = head + b"DATA;\n" + b"\n".join(reversed(body.splitlines())) + b"\nENDSEC;" + end
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assert_inch_box(result)
        self.assertEqual(str(10**30 + 10), result["unit_normalization"]["source_length_unit"])

    def test_spline_geometry_scales_coordinates_not_parameter_knots(self):
        data = conversion_source(data=bulged_box_source())
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        # The independent Bernstein roof volume is (6000+4000/9)*scale^3.
        truth = (Fraction(6000) + Fraction(4000, 9)) * Fraction(127, 5)**3
        self.assertAlmostEqual(float(truth), result["model"]["material_volume"]["value_mm3"],
                               delta=float(truth) * 2e-4)
        self.assertEqual(data.decode("ascii"), result["source_inspection"]["source"]["input"]["raw"])

    def test_linked_line_parameters_and_angles_stay_dimensionless(self):
        fixture = Path(__file__).resolve().parents[3] / "source/STEP_IMPORT/tests/fixtures/linked_wireframe.step"
        data = conversion_source(ap214=True, data=fixture.read_bytes())
        data = data.replace(b"(#914),(#915),.T.,.CARTESIAN.",
                            b"(PARAMETER_VALUE(0.),#914),(PARAMETER_VALUE(5.),#915),.T.,.PARAMETER.")
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assert_inch_box(result)
        point = result["linked_wireframes"][0]["points"][0]["point_mm"]
        for actual, coordinate in zip(point, (7, 8, 9)):
            self.assertAlmostEqual(coordinate * 25.4, actual, delta=1e-9)
        line, = [x for x in result["parameter_mappings"] if x["kind"] == "WireframeTrimmedLine"]
        self.assertEqual([0., 5.], line["parameters"]["basis_range"])
        self.assertEqual([25.4], line["parameters"]["vector_magnitude_mm"])
        circle, = [x for x in result["parameter_mappings"] if x["kind"] == "WireframeConicRationalParameter"]
        self.assertEqual([1.], circle["parameters"]["angle_parameter_to_rad"])


if __name__ == "__main__":
    unittest.main()
