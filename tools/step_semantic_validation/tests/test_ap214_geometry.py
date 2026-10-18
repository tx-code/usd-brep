# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Source parameters, not evaluated/model-qualified geometry, through the real CLI."""
import json
from fractions import Fraction
import os
from pathlib import Path
import re
import random
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214, CORPUS, ap214_source, converted_ap214_source


def geometry_source(kind="CIRCLE", source=None):
    data = source if source is not None else ap214_source((CORPUS / "S203-P01/input.step").read_bytes())
    if kind in {"CIRCLE", "ELLIPSE", "B_SPLINE_CURVE_WITH_KNOTS"}:
        for record in (200, 201):
            data, count = re.subn(rb"(?m)^#" + str(record).encode() + rb"=[^\r\n]*\r?\n", b"", data)
            assert count == 1
        definition = {
            "CIRCLE": b"CIRCLE('source circle',#133,+2.500E0)",
            "ELLIPSE": b"ELLIPSE('source ellipse',#133,1.25,2.50)",
            "B_SPLINE_CURVE_WITH_KNOTS": (
                b"B_SPLINE_CURVE_WITH_KNOTS('source curve',2,(#100,#101,#102),"
                b".UNSPECIFIED.,.U.,.F.,(3,3),(-1.5,2.5),.UNSPECIFIED.)"),
        }[kind]
        assert data.count(b"LINE('',#100,#201)") == 1
        return data.replace(b"LINE('',#100,#201)", definition)
    definition = {
        "CYLINDRICAL_SURFACE": b"CYLINDRICAL_SURFACE('source cylinder',#133,3.25)",
        "CONICAL_SURFACE": b"CONICAL_SURFACE('source cone',#133,0.,-2.5)",
        "B_SPLINE_SURFACE_WITH_KNOTS": (
            b"B_SPLINE_SURFACE_WITH_KNOTS('source surface',2,1,((#100,#101),(#103,#102),(#100,#102)),"
            b".UNSPECIFIED.,.U.,.F.,.U.,(3,3),(2,2),(-2.,3.),(65.,66.),.UNSPECIFIED.)"),
    }[kind]
    if kind == "B_SPLINE_SURFACE_WITH_KNOTS":
        # Retain the original plane's frame as an explicitly owned auxiliary item.
        data = data.replace(b"'body',(#21),#12", b"'body',(#21,#133),#12")
    assert data.count(b"PLANE('',#133)") == 1
    return data.replace(b"PLANE('',#133)", definition)


class Ap214GeometryTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-ap214-geometry-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            command = [os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), *map(str, options)]
            run = subprocess.run(command, capture_output=True, timeout=20)
        return run, json.loads(run.stdout) if run.stdout else None

    def failed(self, data, key, code=1):
        run, result = self.invoke(data)
        self.assertEqual(code, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual(key, result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        return result

    def test_analytic_source_parameters_keep_axes_angles_and_units(self):
        for kind, record, fields in (
                ("CIRCLE", "202", ["name", "position", "radius"]),
                ("ELLIPSE", "202", ["name", "position", "semi_axis_1", "semi_axis_2"]),
                ("CYLINDRICAL_SURFACE", "134", ["name", "position", "radius"]),
                ("CONICAL_SURFACE", "134", ["name", "position", "radius", "semi_angle"])):
            with self.subTest(kind=kind):
                data = geometry_source(kind)
                run, result = self.invoke(data)
                self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                self.assertEqual("Checked", result["status"])
                self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
                entity = next(r for r in result["snapshot"]["records"] if r["id"] == record)
                self.assertEqual(fields, [a["name"] for a in entity["attributes"]])
                self.assertEqual("Passed", result["checks"]["geometry_dependencies"])
                self.assertEqual({"kind": "rational", "num": "1", "den": "1"},
                                 result["snapshot"]["units"]["length_scale_mm"])
                self.assertEqual("NotRun", result["checks"]["global_rules_47"])

    def test_spline_rows_unknown_logicals_and_noncanonical_knots_are_retained(self):
        for kind, record in (("B_SPLINE_CURVE_WITH_KNOTS", "202"), ("B_SPLINE_SURFACE_WITH_KNOTS", "134")):
            with self.subTest(kind=kind):
                data = geometry_source(kind)
                run, result = self.invoke(data)
                self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
                self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
                self.assertEqual("Passed", result["checks"]["geometry_dependencies"])
                self.assertGreater(result["statistics"]["geometry_dependency_checks"], 0)
                raw = next(r for r in result["source"]["records"] if r["id"] == record)
                self.assertIn(".U.", raw["raw"])
                if record == "134":
                    grid = raw["components"][0]["parameters"][3]
                    self.assertEqual([["100", "101"], ["103", "102"], ["100", "102"]],
                                     [[v["text"] for v in row["children"]] for row in grid["children"]])
                    entity = next(r for r in result["snapshot"]["records"] if r["id"] == record)
                    field = next(a for a in entity["attributes"] if a["name"] == "control_points_list")
                    self.assertEqual([{"aggregate": "LIST", "minimum": 2, "maximum": None, "unique_elements": False}],
                                     field["nested_aggregates"])

    def test_positive_lengths_and_cone_zero_have_distinct_source_predicates(self):
        cases = (
            ("CIRCLE", b"+2.500E0", b"-0.E99999999999999999999", "POSITIVE_LENGTH_MEASURE.WR1", "radius"),
            ("ELLIPSE", b"1.25,2.50", b"1.25,0.", "POSITIVE_LENGTH_MEASURE.WR1", "semi_axis_2"),
            ("CYLINDRICAL_SURFACE", b"3.25", b"-1.E-99999999999999999", "POSITIVE_LENGTH_MEASURE.WR1", "radius"),
            ("CONICAL_SURFACE", b",0.,-2.5", b",-1.,-2.5", "CONICAL_SURFACE.WR1", "radius"),
        )
        for kind, old, new, clause, field in cases:
            with self.subTest(kind=kind):
                data = geometry_source(kind).replace(old, new)
                result = self.failed(data, "SourceRuleViolation:" + clause)
                self.assertEqual("Passed", result["checks"]["type_fields"])
                self.assertEqual("Rejected", result["checks"]["geometry_dependencies"])
                self.assertEqual("NotRun", result["checks"]["single_part_profile"])
                error = result["diagnostics"][0]
                self.assertEqual(field, error["field"])
                self.assertEqual("AP214E3_2010.exp " + clause, error["responsibility"])
        for radius in (b"-0.E999999999999999999999", b"+0."):
            data = geometry_source("CONICAL_SURFACE").replace(b",0.,-2.5", b"," + radius + b",-2.5")
            self.assertEqual(0, self.invoke(data)[0].returncode)

    def test_spline_degree_counts_multiplicities_and_knot_order(self):
        original = geometry_source("B_SPLINE_SURFACE_WITH_KNOTS")
        cases = (
            (b"'source surface',2,1", b"'source surface',0,1", "WR1", "u_degree"),
            (b"'source surface',2,1", b"'source surface',2,2", "WR2", "v_degree"),
            (b"'source surface',2,1", b"'source surface'," + b"9" * 100 + b",1", "WR1", "u_degree"),
            (b"(3,3),(2,2)", b"(3,3,1),(2,2)", "WR3", "u_multiplicities"),
            (b"(3,3),(2,2)", b"(3,3),(2,1,1)", "WR4", "v_multiplicities"),
            (b"(3,3),(2,2)", b"(2,3),(2,2)", "WR1", "u_multiplicities"),
            (b"(3,3),(2,2)", b"(4,2),(2,2)", "WR1", "u_multiplicities"),
            (b"(3,3),(2,2)", b"(0,6),(2,2)", "WR1", "u_multiplicities"),
            (b"(-2.,3.)", b"(3.,-2.)", "WR1", "u_knots"),
            (b"(-2.,3.)", b"(-2.,-2.E0)", "WR1", "u_knots"),
        )
        for old, new, suffix, field in cases:
            with self.subTest(field=field, mutation=new[:32]):
                data = original.replace(old, new)
                result = self.failed(data, "SourceRuleViolation:B_SPLINE_SURFACE_WITH_KNOTS." + suffix)
                error = result["diagnostics"][0]
                self.assertEqual(field, error["field"])
                self.assertTrue(error["parameter_path"])
        curve = geometry_source("B_SPLINE_CURVE_WITH_KNOTS")
        interior = curve.replace(b"(3,3),(-1.5,2.5)", b"(2,3,1),(-1.5,0.,2.5)")
        result = self.failed(interior, "SourceRuleViolation:B_SPLINE_CURVE_WITH_KNOTS.WR1")
        self.assertEqual([7, 2], result["diagnostics"][0]["parameter_path"])

    def test_nested_control_grid_keeps_rectangularity_types_and_source_paths(self):
        original = geometry_source("B_SPLINE_SURFACE_WITH_KNOTS")
        ragged = original.replace(b"(#100,#102))", b"(#100,#102,#101))")
        result = self.failed(ragged, "SourceGeometryDependencyViolation")
        error = result["diagnostics"][0]
        self.assertEqual(("134", "control_points_list", [4, 3]),
                         (error["record_id"], error["field"], error["parameter_path"]))
        begin, end = error["span"]
        self.assertEqual(b"(#100,#102,#101)", ragged[begin:end])
        for new, key, path in ((b"(#100,#110)", "ReferenceTypeMismatch", [4, 1, 2]),
                               (b"(#100)", "AggregateCardinalityViolation", [4, 1])):
            data = original.replace(b"(#100,#101)", new)
            result = self.failed(data, key)
            self.assertEqual(path, result["diagnostics"][0]["parameter_path"])
            self.assertEqual("NotRun", result["checks"]["geometry_dependencies"])

    def test_logical_unknown_is_not_boolean_or_an_invented_default(self):
        data = geometry_source("B_SPLINE_CURVE_WITH_KNOTS")
        result = self.failed(data.replace(b".U.,.F.", b".UNKNOWN.,.F."), "LogicalValueInvalid")
        self.assertEqual("closed_curve", result["diagnostics"][0]["field"])
        self.failed(data.replace(b".U.,.F.", b"$,.F."), "RequiredAttributeMissing")
        self.failed(data.replace(b"#202,.T.", b"#202,.U."), "BooleanValueInvalid")
        self.failed(data.replace(b".UNSPECIFIED.,.U.", b".UNKNOWN.,.U."), "EnumerationValueInvalid")

    def test_original_parameters_do_not_follow_canonical_length_scaling(self):
        data = geometry_source().replace(b".MILLI.,.METRE.", b"$,.METRE.")
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode)
        self.assertEqual({"kind": "rational", "num": "1000", "den": "1"},
                         result["snapshot"]["units"]["length_scale_mm"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        data = geometry_source("CIRCLE", converted_ap214_source()).replace(
            b"CIRCLE('source circle',#133", b"CIRCLE('source circle',#503")
        run, result = self.invoke(data)
        self.assertEqual(7, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual("Deferred", result["status"])
        self.assertEqual("Passed", result["checks"]["geometry_dependencies"])
        self.assertIsNone(result["snapshot"]["units"]["length_scale_mm"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])

    def test_exact_knot_order_handles_precision_huge_exponents_and_signed_zero(self):
        pairs = [
            ("1." + "0" * 90 + "1", "1." + "0" * 90 + "2", True),
            ("1000.", "1.E3", False),
            ("-0.E999999999999999999999", "0.", False),
            ("0.9E10000000000000000000", "1.E9999999999999999999", False),
            ("1.E9999999999999999999", "0.9E10000000000000000000", True),
            ("0.1E10000000000000000000", "1.E9999999999999999999", False),
            ("1.E-10000000000000000000", "1.E-9999999999999999999", True),
            ("-1.E10000000000000000000", "-0.99E10000000000000000000", True),
            ("-0.001", "-0.0009", True),
            ("1.E10000000000000000000", "10000000000.E9999999999999999999", True),
            ("1.E5", "0.000001E11", False),
            ("1000000000.E-10000000000000000000", "1.E-9999999999999999990", True),
            ("1.E+000000000099", "1.E100", True),
            ("1.0", "1.E0", False),
        ]
        rng = random.Random(214)
        for _ in range(32):
            a = f"{rng.randrange(-200, 200)}.{rng.randrange(10000):04d}E{rng.randrange(-25, 25):+d}"
            b = f"{rng.randrange(-200, 200)}.{rng.randrange(10000):04d}E{rng.randrange(-25, 25):+d}"
            pairs.append((a, b, Fraction(a) < Fraction(b)))
        for a, b, increasing in pairs:
            with self.subTest(a=a[:30], b=b[:30]):
                data = geometry_source("B_SPLINE_SURFACE_WITH_KNOTS").replace(b"(-2.,3.)", f"({a},{b})".encode())
                run, result = self.invoke(data)
                self.assertEqual(0 if increasing else 1, run.returncode, result.get("diagnostics"))
                if increasing:
                    self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
                else:
                    self.assertEqual("SourceRuleViolation:B_SPLINE_SURFACE_WITH_KNOTS.WR1",
                                     result["diagnostics"][0]["key"])

    def test_phase_budget_and_report_publication_remain_atomic(self):
        data = geometry_source("B_SPLINE_SURFACE_WITH_KNOTS")
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode)
        maximum = result["statistics"]["dependency_visits"]
        self.assertEqual(0, self.invoke(data, "--max-semantic-visits", maximum)[0].returncode)
        stopped, limited = self.invoke(data, "--max-semantic-visits", maximum - 1)
        self.assertEqual(3, stopped.returncode)
        self.assertIsNone(limited["snapshot"])
        bad = geometry_source().replace(b"+2.500E0", b"0.")
        _, failed = self.invoke(bad)
        stop_before_predicate = failed["statistics"]["dependency_visits"] - 1
        _, limited = self.invoke(geometry_source(), "--max-semantic-visits", stop_before_predicate)
        self.assertEqual("Passed", limited["checks"]["type_fields"])
        self.assertEqual("ResourceLimit", limited["checks"]["geometry_dependencies"])
        self.assertIsNone(limited["snapshot"])
        size = len(run.stdout)
        for _ in range(4):
            run, _ = self.invoke(data, "--max-report-bytes", size)
            self.assertEqual(0, run.returncode)
            size = len(run.stdout)
        stopped, result = self.invoke(data, "--max-report-bytes", size - 1)
        self.assertEqual(5, stopped.returncode)
        self.assertEqual(b"", stopped.stdout)
        self.assertIsNone(result)

    def test_other_profiles_and_rational_forms_do_not_gain_implicit_support(self):
        self.failed(geometry_source().replace(AP214, AP203), "UnsupportedEntityType", 2)
        data = geometry_source("B_SPLINE_SURFACE_WITH_KNOTS")
        # The rational declaration is known, but only its explicit-knot complex
        # mapping is admitted. A renamed simple record cannot inherit support.
        self.failed(data.replace(b"B_SPLINE_SURFACE_WITH_KNOTS(", b"RATIONAL_B_SPLINE_SURFACE("),
                    "UnsupportedEntityMapping", 2)

    def test_new_geometry_field_declarations_match_literal_source_contract(self):
        run, result = self.invoke(geometry_source("B_SPLINE_SURFACE_WITH_KNOTS"))
        self.assertEqual(0, run.returncode)
        record = next(r for r in result["snapshot"]["records"] if r["id"] == "134")
        expected = [
            ("name", "REPRESENTATION_ITEM", "LABEL"),
            ("u_degree", "B_SPLINE_SURFACE", "INTEGER"), ("v_degree", "B_SPLINE_SURFACE", "INTEGER"),
            ("control_points_list", "B_SPLINE_SURFACE", "CARTESIAN_POINT"),
            ("surface_form", "B_SPLINE_SURFACE", "B_SPLINE_SURFACE_FORM"),
            ("u_closed", "B_SPLINE_SURFACE", "LOGICAL"), ("v_closed", "B_SPLINE_SURFACE", "LOGICAL"),
            ("self_intersect", "B_SPLINE_SURFACE", "LOGICAL"),
            ("u_multiplicities", "B_SPLINE_SURFACE_WITH_KNOTS", "INTEGER"),
            ("v_multiplicities", "B_SPLINE_SURFACE_WITH_KNOTS", "INTEGER"),
            ("u_knots", "B_SPLINE_SURFACE_WITH_KNOTS", "PARAMETER_VALUE"),
            ("v_knots", "B_SPLINE_SURFACE_WITH_KNOTS", "PARAMETER_VALUE"),
            ("knot_spec", "B_SPLINE_SURFACE_WITH_KNOTS", "KNOT_TYPE"),
        ]
        self.assertEqual(expected, [(a["name"], a["declared_in"], a["declared_type"]) for a in record["attributes"]])
        self.assertEqual("Partial", result["checks"]["other_entity_where"])
        self.assertEqual("NotRun", result["checks"]["named_measure_predicates"])
        circle = self.invoke(geometry_source())[1]
        self.assertEqual("Partial", circle["checks"]["other_entity_where"])
        self.assertEqual(0, circle["statistics"]["geometry_entity_predicate_checks"])
        self.assertGreater(circle["statistics"]["unit_dependency_checks"], 0)
        self.assertEqual("Partial", circle["checks"]["named_measure_predicates"])
