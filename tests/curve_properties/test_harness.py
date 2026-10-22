# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Known answers for the independent oracle, without a native SDK dependency."""
import unittest

import harness


class OracleTests(unittest.TestCase):
    def test_quadratic_interior_extremum(self):
        case = {"points": [[0, 0, 0], [1, 4, 0], [2, 0, 0]], "clip": [0, 1]}
        self.assertEqual(harness.polynomial_bounds(case), [[0, 0, 0], [2, 2, 0]])

    def test_shifted_parameter_derivatives(self):
        case = {"points": [[0, 0, 0], [1, 4, 0], [2, 0, 0]],
                "weights": [1, 1, 1], "domain": [3, 5]}
        self.assertEqual(harness.bezier_value(case, .5), [[1, 2, 0], [1, 0, 0], [0, -4, 0]])

    def test_rational_quarter_circle_midpoint(self):
        case = {"points": [[1, 0, 0], [1, 1, 0], [0, 1, 0]],
                "weights": [1, 2**-.5, 1], "domain": [0, 1]}
        value = harness.bezier_value(case, .5)[0]
        self.assertAlmostEqual(value[0], 2**-.5, places=14)
        self.assertAlmostEqual(value[1], 2**-.5, places=14)

    def test_quarter_ellipse_bounds(self):
        case = {"frame": 0, "radii": [7, 2], "center": [0, 0, 0], "clip": [0, .25]}
        bounds = harness.conic_bounds(case)
        harness.vector_near(bounds[0], [0, 0, 0], 1e-14, "known_lower")
        harness.vector_near(bounds[1], [7, 2, 0], 1e-14, "known_upper")


if __name__ == "__main__":
    unittest.main()
