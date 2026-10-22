# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent hemisphere, small-cap and complementary-region controls."""
from fractions import Fraction as F
import unittest

from tools.step_import_native_validation.tests.spherical_cap_oracle import spherical_cap_truth


class SphericalCapOracleTests(unittest.TestCase):
    def test_hemisphere_literal_values(self):
        for positive in (False, True):
            truth = spherical_cap_truth(5, 0, positive_side=positive)
            self.assertEqual(F(250, 3), truth['volume_over_pi_mm3'])
            self.assertEqual(75, truth['area_over_pi_mm2'])
            self.assertEqual(F(15, 8)*(1 if positive else -1), truth['centroid_axis_mm'])
            self.assertEqual((0, 5) if positive else (-5, 0), truth['axis_bounds_mm'])

    def test_small_cap_and_large_complement_are_distinct(self):
        small = spherical_cap_truth(5, 3, positive_side=True)
        large = spherical_cap_truth(5, 3, positive_side=False)
        self.assertEqual((F(52, 3), 36, F(48, 13)),
                         tuple(small[k] for k in ('volume_over_pi_mm3', 'area_over_pi_mm2', 'centroid_axis_mm')))
        self.assertEqual((F(448, 3), 96, F(-3, 7)),
                         tuple(large[k] for k in ('volume_over_pi_mm3', 'area_over_pi_mm2', 'centroid_axis_mm')))
        self.assertEqual(F(500, 3), small['volume_over_pi_mm3'] + large['volume_over_pi_mm3'])
        self.assertEqual(0, small['volume_over_pi_mm3']*small['centroid_axis_mm']
                         + large['volume_over_pi_mm3']*large['centroid_axis_mm'])

    def test_tangent_and_degenerate_inputs_do_not_choose_a_region(self):
        for radius, offset in ((0, 0), (-5, 0), (5, 5), (5, -5), (5, 6)):
            with self.subTest(radius=radius, offset=offset):
                with self.assertRaises(ValueError):
                    spherical_cap_truth(radius, offset, positive_side=True)
        with self.assertRaises(TypeError):
            spherical_cap_truth(5, 0, positive_side=1)
