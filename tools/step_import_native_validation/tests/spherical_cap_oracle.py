# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Exact rational coefficients for a solid sphere clipped by one plane.

This test oracle integrates circular cross sections, independently of either
CAD reader or the target pole/seam representation. Area and volume are returned
as coefficients of pi; the axial centroid is relative to the sphere center.
"""
from fractions import Fraction


def spherical_cap_truth(radius, plane_offset, *, positive_side):
    radius, offset = Fraction(radius), Fraction(plane_offset)
    if radius <= 0 or not -radius < offset < radius:
        raise ValueError('A nondegenerate circular section requires R > 0 and -R < h < R')
    if type(positive_side) is not bool:
        raise TypeError('The retained half-space must be selected explicitly')
    low, high = (offset, radius) if positive_side else (-radius, offset)

    # Cross-section area/pi is R^2-z^2. Its first axial moment/pi is
    # z*(R^2-z^2); integrate before dividing to avoid quadrature or CAD truth.
    def volume_primitive(z):
        return radius**2*z - z**3/3

    def moment_primitive(z):
        return radius**2*z**2/2 - z**4/4

    volume = volume_primitive(high) - volume_primitive(low)
    moment = moment_primitive(high) - moment_primitive(low)
    section_radius_squared = radius**2 - offset**2
    return {
        'volume_over_pi_mm3': volume,
        'area_over_pi_mm2': 2*radius*(high-low) + section_radius_squared,
        'centroid_axis_mm': moment/volume,
        'section_radius_squared_mm2': section_radius_squared,
        'axis_bounds_mm': (low, high),
    }
