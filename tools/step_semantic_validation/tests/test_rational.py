# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Literal rational mappings and one-defect source contracts, not a schema generator."""

import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214_geometry import geometry_source
from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214, CORPUS, ap214_source


def rational_curve(data=None, weights="(1.,+0.600E0,1.)"):
    data = geometry_source("B_SPLINE_CURVE_WITH_KNOTS") if data is None else data
    old = b"B_SPLINE_CURVE_WITH_KNOTS('source curve',2,(#100,#101,#102),.UNSPECIFIED.,.U.,.F.,(3,3),(-1.5,2.5),.UNSPECIFIED.)"
    new = (
        "(BOUNDED_CURVE() B_SPLINE_CURVE(2,(#100,#101,#102),.UNSPECIFIED.,.U.,.F.) "
        "B_SPLINE_CURVE_WITH_KNOTS((3,3),(-1.5,2.5),.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() "
        f"RATIONAL_B_SPLINE_CURVE({weights}) REPRESENTATION_ITEM('source curve'))"
    ).encode()
    assert old in data
    return data.replace(old, new)


def rational_surface(weights="((1.,2.),(3.,4.),(5.,6.))"):
    data = geometry_source("B_SPLINE_SURFACE_WITH_KNOTS")
    old = b"B_SPLINE_SURFACE_WITH_KNOTS('source surface',2,1,((#100,#101),(#103,#102),(#100,#102)),.UNSPECIFIED.,.U.,.F.,.U.,(3,3),(2,2),(-2.,3.),(65.,66.),.UNSPECIFIED.)"
    new = (
        "(BOUNDED_SURFACE() B_SPLINE_SURFACE(2,1,((#100,#101),(#103,#102),(#100,#102)),.UNSPECIFIED.,.U.,.F.,.U.) "
        "B_SPLINE_SURFACE_WITH_KNOTS((3,3),(2,2),(-2.,3.),(65.,66.),.UNSPECIFIED.) GEOMETRIC_REPRESENTATION_ITEM() "
        f"RATIONAL_B_SPLINE_SURFACE({weights}) REPRESENTATION_ITEM('source surface') SURFACE())"
    ).encode()
    assert old in data
    return data.replace(old, new)


def rational_box(curve=True, surface=True, surface_weights="((1.25,1.25),(1.25,1.25))"):
    data = ap214_source((CORPUS / "S203-P04/input.step").read_bytes())
    if curve:
        for record in (200, 201):
            data, n = re.subn(rb"(?m)^#" + str(record).encode() + rb"=[^\r\n]*\r?\n", b"", data)
            assert n == 1
        definition = b"(BOUNDED_CURVE() B_SPLINE_CURVE(1,(#100,#101),.POLYLINE_FORM.,.F.,.F.) B_SPLINE_CURVE_WITH_KNOTS((2,2),(0.,1.),.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_CURVE((2.,5.)) REPRESENTATION_ITEM('weighted line'))"
        assert b"LINE('',#100,#201)" in data
        data = data.replace(b"LINE('',#100,#201)", definition)
    if surface:
        definition = (
            "(BOUNDED_SURFACE() B_SPLINE_SURFACE(1,1,((#104,#107),(#105,#106)),.PLANE_SURF.,.F.,.F.,.F.) "
            "B_SPLINE_SURFACE_WITH_KNOTS((2,2),(2,2),(0.,1.),(0.,1.),.UNSPECIFIED.) GEOMETRIC_REPRESENTATION_ITEM() "
            f"RATIONAL_B_SPLINE_SURFACE({surface_weights}) REPRESENTATION_ITEM('weighted rectangle') SURFACE())"
        ).encode()
        assert b"PLANE('',#513)" in data
        data = data.replace(b"PLANE('',#513)", definition).replace(b"'body',(#21),#12", b"'body',(#21,#513),#12")
    return data


class RationalSourceTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-rational-") as directory:
            path = Path(directory) / "input.step"
            path.write_bytes(data)
            result = subprocess.run(
                [os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path), *map(str, options)],
                capture_output=True,
                timeout=30,
            )
        return result, json.loads(result.stdout) if result.stdout else None

    def test_curve_and_surface_keep_original_weights_and_facets(self):
        for data, record, kind in [
            (rational_curve(), "202", "RATIONAL_B_SPLINE_CURVE"),
            (rational_surface(), "134", "RATIONAL_B_SPLINE_SURFACE"),
        ]:
            with self.subTest(kind=kind):
                run, r = self.invoke(data)
                self.assertEqual(0, run.returncode, r["diagnostics"])
                self.assertEqual(data.decode("ascii"), r["source"]["input"]["raw"])
                item = next(x for x in r["snapshot"]["records"] if x["id"] == record)
                field = next(x for x in item["attributes"] if x["name"] == "weights_data")
                self.assertEqual(kind, field["declared_in"])
                self.assertEqual("Passed", r["checks"]["geometry_dependencies"])

    def test_curve_length_mismatch_and_nonpositive_weights_fail_even_in_interop(self):
        for weights, key in [
            ("(1.,2.)", "RATIONAL_B_SPLINE_CURVE.WR1"),
            ("(1.,-0.0,1.)", "RATIONAL_B_SPLINE_CURVE.WR2"),
            ("(1.,-1.0E-9999,1.)", "RATIONAL_B_SPLINE_CURVE.WR2"),
        ]:
            with self.subTest(weights=weights):
                run, r = self.invoke(rational_curve(weights=weights), "--interop-nongeometric")
                self.assertEqual(1, run.returncode)
                self.assertEqual("SourceRuleViolation:" + key, r["diagnostics"][0]["key"])
                self.assertIsNone(r["snapshot"])

    def test_surface_weight_shape_and_positivity_are_checked_without_transpose(self):
        for weights, key in [
            ("((1.,2.),(3.,4.))", "SourceRuleViolation:RATIONAL_B_SPLINE_SURFACE.WR1"),
            ("((1.,2.,3.),(3.,4.),(5.,6.))", "SourceRuleViolation:RATIONAL_B_SPLINE_SURFACE.WR1"),
            ("((1.,2.),(3.,4.,5.),(5.,6.))", "SourceGeometryDependencyViolation"),
            ("((1.,2.),(0.,4.),(5.,6.))", "SourceRuleViolation:RATIONAL_B_SPLINE_SURFACE.WR2"),
        ]:
            with self.subTest(weights=weights):
                run, r = self.invoke(rational_surface(weights), "--interop-nongeometric")
                self.assertEqual(1, run.returncode)
                self.assertEqual(key, r["diagnostics"][0]["key"])
                self.assertIsNone(r["snapshot"])

    def test_n2635_general_admits_weights_without_leaking_into_default_p0(self):
        data = rational_curve().replace(AP214, AP203)
        run, r = self.invoke(data, "--general-brep-source")
        self.assertEqual(0, run.returncode, r["diagnostics"])
        default, rejected = self.invoke(data)
        self.assertEqual(2, default.returncode)
        self.assertIsNone(rejected["snapshot"])

    def test_positive_tiny_weight_is_not_rounded_to_zero_by_source_predicate(self):
        run, r = self.invoke(rational_curve(weights="(1.,1.0E-9999,1.)"))
        self.assertEqual(0, run.returncode, r["diagnostics"])
        self.assertIn("1.0E-9999", r["source"]["input"]["raw"])

    def test_weight_descriptor_has_no_derived_transport_slot(self):
        data = rational_curve().replace(
            b"RATIONAL_B_SPLINE_CURVE((1.,+0.600E0,1.))", b"RATIONAL_B_SPLINE_CURVE((1.,0.6,1.),(1.,0.6,1.))"
        )
        run, r = self.invoke(data)
        self.assertEqual(1, run.returncode)
        self.assertEqual("AttributeCountMismatch", r["diagnostics"][0]["key"])

    def test_visit_budget_stops_atomically_on_weight_walk(self):
        data = rational_surface()
        run, r = self.invoke(data)
        visits = r["statistics"]["dependency_visits"]
        self.assertEqual(0, run.returncode)
        exact, _ = self.invoke(data, "--max-semantic-visits", visits)
        short, failed = self.invoke(data, "--max-semantic-visits", visits - 1)
        self.assertEqual(0, exact.returncode)
        self.assertEqual(3, short.returncode)
        self.assertIsNone(failed["snapshot"])
