# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Derived unit source semantics through the existing public CLI."""
import json
from fractions import Fraction
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214, CORPUS, ap214_source
from tools.step_semantic_validation.tests.test_ap214_entities import append_records


def unit_source():
    data = ap214_source((CORPUS / "S203-P01/input.step").read_bytes())
    data = data.replace(b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11))", b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11,#903))")
    return append_records(data, b"""#900=(MASS_UNIT() NAMED_UNIT(*) SI_UNIT(.KILO.,.GRAM.));
#901=DERIVED_UNIT_ELEMENT(#900,+1.0);
#902=DERIVED_UNIT_ELEMENT(#10,-3.);
#903=DERIVED_UNIT((#901,#902));
""")


def conversion_mass_source():
    data = unit_source().replace(b"DERIVED_UNIT_ELEMENT(#900,+1.0)", b"DERIVED_UNIT_ELEMENT(#905,+1.0)")
    return append_records(data, b"""#904=MASS_MEASURE_WITH_UNIT(MASS_MEASURE(2.2046),#900);
#905=(CONVERSION_BASED_UNIT('source mass',#904) MASS_UNIT() NAMED_UNIT(*));
""")


class Ap214UnitTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-ap214-unit-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            command = [os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), *map(str, options)]
            run = subprocess.run(command, capture_output=True, timeout=20)
        return run, json.loads(run.stdout) if run.stdout else None

    def checked(self, data):
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        self.assertEqual("Passed", result["checks"]["unit_dependencies"])
        return result

    def failed(self, data, key, code=1):
        run, result = self.invoke(data)
        self.assertEqual(code, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual(key, result["diagnostics"][0]["key"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        self.assertIsNone(result["snapshot"])
        return result

    def dimensions(self, result, record="903"):
        item = next(u for u in result["snapshot"]["units"]["dimension_definitions"] if u["source_id"] == record)
        return [Fraction(v["num"]) / Fraction(v["den"]) for v in item["dimensions"]]

    def test_derived_density_preserves_elements_parameters_and_source_units(self):
        data = unit_source()
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual("Checked", result["status"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        self.assertEqual("Passed", result["checks"]["unit_dependencies"])
        dimensions = {u["source_id"]: u for u in result["snapshot"]["units"]["dimension_definitions"]}
        self.assertEqual("Derived", dimensions["903"]["nominal"])
        self.assertEqual([("-3", "1"), ("1", "1")] + [("0", "1")] * 5,
                         [(v["num"], v["den"]) for v in dimensions["903"]["dimensions"]])
        mass = next(u for u in result["snapshot"]["units"]["si_definitions"] if u["source_id"] == "900")
        self.assertEqual(("Mass", "kg", {"kind": "rational", "num": "1", "den": "1"}),
                         (mass["quantity"], mass["canonical_unit"], mass["scale"]))
        self.assertEqual({"kind": "rational", "num": "1", "den": "1"},
                         result["snapshot"]["units"]["length_scale_mm"])

    def test_fractional_powers_cancellation_and_set_order_remain_exact(self):
        data = unit_source().replace(b"#900,+1.0", b"#900,0.5").replace(b"#10,-3.", b"#10,-1.25")
        self.assertEqual([Fraction(-5, 4), Fraction(1, 2)] + [Fraction(0)] * 5,
                         self.dimensions(self.checked(data)))
        for a, b in (("0.1", "0.2"), ("1.25", "-1.25"), ("0.333333333", "-0.333333332")):
            data = unit_source().replace(b"#900,+1.0", b"#10," + a.encode()).replace(b"#10,-3.", b"#10," + b.encode())
            data = data.replace(b"(#10,#11,#903)", b"(#10,#11,#900,#903)")
            expected = [Fraction(a) + Fraction(b)] + [Fraction(0)] * 6
            self.assertEqual(expected, self.dimensions(self.checked(data)))
            permuted = data.replace(b"((#901,#902))", b"((#902,#901))")
            self.assertEqual(expected, self.dimensions(self.checked(permuted)))
        definitions = self.checked(data)["snapshot"]["units"]["dimension_definitions"]
        item = next(u for u in definitions if u["source_id"] == "903")
        self.assertEqual("Derived", item["nominal"])

    def test_plain_named_dimensions_and_fractional_multiplication_are_preserved(self):
        data = unit_source().replace(b"#10,-3.", b"#905,-1.2")
        data = append_records(data, b"#904=DIMENSIONAL_EXPONENTS(0.25,0.,0.,0.,0.,0.,0.);\n#905=NAMED_UNIT(#904);\n")
        result = self.checked(data)
        self.assertEqual([Fraction(-3, 10), Fraction(1)] + [Fraction(0)] * 5, self.dimensions(result))
        item = next(u for u in result["snapshot"]["units"]["dimension_definitions"] if u["source_id"] == "905")
        self.assertEqual("Named", item["nominal"])
        self.assertEqual(Fraction(1, 4), self.dimensions(result, "905")[0])

    def test_mass_kilo_rule_follows_actual_element_references_including_zero(self):
        for prefix in (b"$", b".MILLI."):
            for exponent in (b"+1.0", b"0."):
                with self.subTest(prefix=prefix, exponent=exponent):
                    data = unit_source().replace(b".KILO.,.GRAM.", prefix + b",.GRAM.")
                    data = data.replace(b"#900,+1.0", b"#900," + exponent)
                    result = self.failed(data, "SourceRuleViolation:SI_UNIT.WR1")
                    self.assertEqual(("900", "prefix"),
                                     (result["diagnostics"][0]["record_id"], result["diagnostics"][0]["field"]))
        # The source rule is direct USEDIN, not a transitive mass-usage rule.
        data = conversion_mass_source().replace(b".KILO.,.GRAM.", b"$,.GRAM.")
        self.assertEqual([Fraction(-3), Fraction(1)] + [Fraction(0)] * 5, self.dimensions(self.checked(data)))

    def test_single_element_one_is_not_a_derived_unit_alias(self):
        data = unit_source().replace(b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11,#903))",
                                     b"GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11,#900,#903))")
        data = data.replace(b"DERIVED_UNIT((#901,#902))", b"DERIVED_UNIT((#901))")
        data = data.replace(b"#902=DERIVED_UNIT_ELEMENT(#10,-3.);\n", b"")
        self.failed(data, "SourceRuleViolation:DERIVED_UNIT.WR1")
        result = self.checked(data.replace(b"#900,+1.0", b"#900,0."))
        self.assertEqual([Fraction(0)] * 7, self.dimensions(result))

    def test_conversion_dimensions_do_not_rewrite_factors_or_invent_scales(self):
        data = conversion_mass_source()
        result = self.checked(data)
        self.assertEqual([Fraction(0), Fraction(1)] + [Fraction(0)] * 5, self.dimensions(result, "905"))
        self.assertIn("MASS_MEASURE(2.2046)", result["source"]["input"]["raw"])
        record = next(r for r in result["snapshot"]["records"] if r["id"] == "905")
        dims = next(a for a in record["attributes"] if a["name"] == "dimensions")
        self.assertTrue(dims["derived_slot"])
        self.assertEqual("CONVERSION_BASED_UNIT", dims["redeclared_by"])
        self.assertEqual("NotRun", result["snapshot"]["units"]["conversion_status"])
        self.assertEqual("NotRun", result["checks"]["native_build"])

    def test_nominal_and_measure_dimension_mismatches_are_distinguished(self):
        data = unit_source().replace(b".KILO.,.GRAM.", b".KILO.,.METRE.")
        self.failed(data, "SourceRuleViolation:MASS_UNIT.WR1")
        data = conversion_mass_source().replace(b"MASS_MEASURE(2.2046)", b"LENGTH_MEASURE(2.2046)")
        self.failed(data, "SourceRuleViolation:MEASURE_WITH_UNIT.WR1")
        data = conversion_mass_source().replace(b"#904) MASS_UNIT() NAMED_UNIT", b"#904) LENGTH_UNIT() NAMED_UNIT")
        self.failed(data, "SourceRuleViolation:LENGTH_UNIT.WR1")

    def test_unit_cycles_are_rejected_before_recursive_dimension_evaluation(self):
        data = conversion_mass_source().replace(b"MASS_MEASURE(2.2046),#900", b"MASS_MEASURE(2.2046),#903")
        result = self.failed(data, "SourceUnitDependencyCycle")
        self.assertEqual("Rejected", result["checks"]["unit_dependencies"])
        self.assertEqual("NotRun", result["checks"]["geometry_dependencies"])
        self.assertEqual("904", result["diagnostics"][0]["record_id"])
        self.assertEqual("unit_component", result["diagnostics"][0]["field"])

    def test_transport_rules_and_units_outside_the_subset_stay_explicit(self):
        original = unit_source()
        for old, new, key in (
            (b"DERIVED_UNIT_ELEMENT(#900,+1.0)", b"DERIVED_UNIT_ELEMENT(#900,1)", "AttributeTypeMismatch"),
            (b"DERIVED_UNIT_ELEMENT(#900,+1.0)", b"DERIVED_UNIT_ELEMENT(#903,+1.0)", "ReferenceTypeMismatch"),
            (b"DERIVED_UNIT((#901,#902))", b"DERIVED_UNIT((#901,#901))", "SetDuplicateMember"),
            (b"DERIVED_UNIT((#901,#902))", b"DERIVED_UNIT(())", "AggregateCardinalityViolation"),
            (b"DERIVED_UNIT((#901,#902))", b"DERIVED_UNIT('invented name',(#901,#902))", "AttributeCountMismatch")):
            with self.subTest(key=key):
                self.failed(original.replace(old, new), key)
        self.failed(original.replace(AP214, AP203), "UnsupportedComplexSignature", 2)

    def test_capacity_is_a_resource_limit_and_zero_never_expands_a_power(self):
        data = unit_source().replace(b"#10,-3.", b"#10,1.E999999999999999999999999")
        result = self.failed(data, "UnitDimensionCapacityExceeded", 3)
        self.assertEqual("ResourceLimit", result["checks"]["unit_dependencies"])
        self.assertEqual("ResourceLimit", result["diagnostics"][0]["classification"])
        result = self.checked(unit_source().replace(b"#10,-3.", b"#10,-0.E999999999999999999999999"))
        self.assertEqual([Fraction(0), Fraction(1)] + [Fraction(0)] * 5, self.dimensions(result))
        # Dimensionless basis multiplication is exactly zero, not an approximation.
        data = unit_source().replace(b"#10,-3.", b"#11,1.E999999999999999999999999")
        self.assertEqual([Fraction(0), Fraction(1)] + [Fraction(0)] * 5, self.dimensions(self.checked(data)))

    def test_dependency_budget_and_combined_report_remain_atomic(self):
        data = unit_source()
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode)
        visits = result["statistics"]["dependency_visits"]
        self.assertEqual(0, self.invoke(data, "--max-semantic-visits", visits)[0].returncode)
        stopped, limited = self.invoke(data, "--max-semantic-visits", visits - 1)
        self.assertEqual(3, stopped.returncode)
        self.assertIsNone(limited["snapshot"])
        _, failed = self.invoke(data.replace(b".KILO.,.GRAM.", b".MILLI.,.GRAM."))
        threshold = failed["statistics"]["dependency_visits"] - 1
        _, limited = self.invoke(data, "--max-semantic-visits", threshold)
        self.assertEqual("Passed", limited["checks"]["type_fields"])
        self.assertEqual("ResourceLimit", limited["checks"]["unit_dependencies"])
        self.assertEqual("NotRun", limited["checks"]["geometry_dependencies"])
        size = len(run.stdout)
        for _ in range(4):
            run, _ = self.invoke(data, "--max-report-bytes", size)
            self.assertEqual(0, run.returncode)
            size = len(run.stdout)
        stopped, result = self.invoke(data, "--max-report-bytes", size - 1)
        self.assertEqual(5, stopped.returncode)
        self.assertEqual(b"", stopped.stdout)
        self.assertIsNone(result)

    def test_deep_conversion_dependencies_are_memoized_without_source_recursion(self):
        data = unit_source()
        records = []
        previous = 10
        for i in range(128):
            factor, unit = 1000 + 2 * i, 1001 + 2 * i
            records.append(f"#{factor}=MEASURE_WITH_UNIT(LENGTH_MEASURE(1.),#{previous});\n".encode())
            records.append((f"#{unit}=(CONVERSION_BASED_UNIT('source',#{factor}) "
                            "LENGTH_UNIT() NAMED_UNIT(*));\n").encode())
            previous = unit
        data = append_records(data.replace(b"#10,-3.", f"#{previous},-3.".encode()), b"".join(records))
        result = self.checked(data)
        self.assertEqual([Fraction(-3), Fraction(1)] + [Fraction(0)] * 5, self.dimensions(result))

    def test_declarations_keep_derived_names_out_of_the_serialized_slots(self):
        result = self.checked(unit_source())
        records = {r["id"]: r for r in result["snapshot"]["records"]}
        self.assertEqual([("unit", "NAMED_UNIT"), ("exponent", "REAL")],
                         [(a["name"], a["declared_type"]) for a in records["901"]["attributes"]])
        self.assertEqual(["elements"], [a["name"] for a in records["903"]["attributes"]])
        field = records["903"]["attributes"][0]
        self.assertEqual(("SET", 1, True), (field["aggregate"], field["minimum"], field["unique_elements"]))
        self.failed(unit_source().replace(b".KILO.,.GRAM.", b".KILO.,.NEWTON."),
                    "UnsupportedUnitDimensionKind", 2)
