# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Hand-derived answers for multispan and seam reference properties."""
import unittest
import json
from pathlib import Path

import explore
import spline_oracle as spline
import zero_span_bounds


class SplineOracleTests(unittest.TestCase):
    def test_stationary_suite_keeps_the_unmodified_frozen_input(self):
        path = Path(__file__).with_name("fixtures") / "zero-span-quadratic-bounds.json"
        original = json.loads(path.read_text(encoding="utf-8"))["case"]
        self.assertTrue(any(case == original for _, case, _ in zero_span_bounds.cases()))

    def test_zero_span_witness_has_an_independent_half_unit_arch(self):
        path = Path(__file__).with_name("fixtures") / "zero-span-quadratic-bounds.json"
        case = json.loads(path.read_text(encoding="utf-8"))["case"]
        explore.validate(case)
        self.assertEqual(spline.bounds(case), [[0, 0, 0], [0, 0, .5]])
        self.assertEqual(spline.value(spline.data(case), .875)[0], [0, 0, .5])
    def test_all_fixed_inputs_have_valid_knot_and_control_cardinality(self):
        for case in spline.fixed_cases():
            explore.validate(case)
            degree, points, _, knots = spline.data(case)
            self.assertEqual(len(knots), len(points) + degree + 1)

    def test_c0_knot_has_distinct_one_sided_tangents(self):
        case = {"kind": "nurbs", "degree": 1, "points": [[0, 0, 0], [1, 2, 0], [3, 1, 0]],
                "weights": [1, 1, 1], "knots": [0, 0, .5, 1, 1], "domain": [3, 5], "clip": [0, 1]}
        data = spline.data(case)
        self.assertEqual(spline.value(data, 4, left=True), [[1, 2, 0], [1, 2, 0], [0, 0, 0]])
        self.assertEqual(spline.value(data, 4, left=False), [[1, 2, 0], [2, -1, 0], [0, 0, 0]])
        self.assertEqual(spline.bounds(case), [[0, 0, 0], [3, 2, 0]])

    def test_simple_quadratic_knot_keeps_tangent_and_changes_second_derivative(self):
        case = {"kind": "nurbs", "degree": 2, "points": [[0, 0, 0], [1, 2, 0], [2, 0, 0], [4, 0, 0]],
                "weights": [1] * 4, "knots": [0, 0, 0, .5, 1, 1, 1], "domain": [0, 1], "clip": [0, 1]}
        data = spline.data(case)
        self.assertEqual(spline.value(data, .5, True), [[1.5, 1, 0], [2, -4, 0], [-4, -24, 0]])
        self.assertEqual(spline.value(data, .5, False), [[1.5, 1, 0], [2, -4, 0], [12, 8, 0]])

    def test_cubic_double_knot_has_hand_derived_c1_limits(self):
        # One insertion at .5 gives two cubic Bezier spans sharing (P2+P3)/2.
        # Endpoint Bernstein derivatives on spans of length .5 give these literals.
        case = {"kind": "nurbs", "degree": 3,
                "points": [[0, 0, 0], [1, 1, 0], [2, 2, 0], [3, 0, 0], [4, 1, 0], [5, 0, 0]],
                "weights": [1] * 6, "knots": [0, 0, 0, 0, .5, .5, 1, 1, 1, 1],
                "domain": [0, 1], "clip": [0, 1]}
        data = spline.data(case)
        self.assertEqual(spline.value(data, .5, True), [[2.5, 1, 0], [3, -6, 0], [-12, -48, 0]])
        self.assertEqual(spline.value(data, .5, False), [[2.5, 1, 0], [3, -6, 0], [12, 48, 0]])

    def test_periodic_quadratic_has_known_seam_limits(self):
        case = {"kind": "periodic_nurbs", "points": [[1, 0, 0], [0, 1, 0], [-1, 0, 0], [0, -1, 0]],
                "weights": [1] * 4, "domain": [-7, 1], "clip": [.875, 1.125]}
        data = spline.data(case)
        start, end = spline.value(data, -7, False), spline.value(data, 1, True)
        self.assertEqual(start[:2], end[:2])
        self.assertEqual(start[0], [.5, -.5, 0])
        self.assertEqual(start[1], [.5, .5, 0])
        self.assertNotEqual(start[2], end[2])
        self.assertEqual(spline.windows(case), [[.875, 1], [0, .125]])

    def test_rational_c0_knot_has_known_quotient_derivatives(self):
        case = {"kind": "nurbs", "degree": 1, "points": [[0, 0, 0], [1, 2, 0], [3, 1, 0]],
                "weights": [1, .5, 1], "knots": [0, 0, .5, 1, 1], "domain": [0, 2], "clip": [0, 1]}
        data = spline.data(case)
        self.assertEqual(spline.value(data, 1, True), [[1, 2, 0], [2, 4, 0], [4, 8, 0]])
        self.assertEqual(spline.value(data, 1, False), [[1, 2, 0], [4, -2, 0], [-8, 4, 0]])


if __name__ == "__main__":
    unittest.main()
