# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Exact local basis polynomials, independent of native floating point evaluation."""
from fractions import Fraction as F
from functools import lru_cache
import math


KINDS = ("nurbs", "periodic_nurbs")


def data(case):
    points, weights = case["points"], case["weights"]
    if case["kind"] == "periodic_nurbs":
        # Clamped encoding of a uniform periodic quadratic B-spline. Form the
        # adjacent homogeneous midpoints before projecting back to Euclidean CPs.
        # The basis has repeated joins; these control constraints give C1 closure.
        homogeneous = [[*(p * w for p in point), w] for point, w in zip(points, weights)]
        control = []
        for i, h in enumerate(homogeneous):
            previous, following = homogeneous[i - 1], homogeneous[(i + 1) % len(points)]
            if i == 0:
                control.append([(a + b) / 2 for a, b in zip(previous, h)])
            control.extend([h, [(a + b) / 2 for a, b in zip(h, following)]])
        points = [[v / h[3] for v in h[:3]] for h in control]
        weights = [h[3] for h in control]
        degree = 2
        knots = [0] * 3 + [i / len(case["points"]) for i in range(1, len(case["points"])) for _ in range(2)]
        knots += [1] * 3
    else:
        degree, knots = case["degree"], case["knots"]
    first, last = case["domain"]
    actual_knots = tuple(first + (last - first) * q for q in knots)
    return degree, tuple(map(tuple, points)), tuple(weights), actual_knots


def transformed(original, reverse=False):
    degree, points, weights, knots = original
    if reverse:
        total = knots[0] + knots[-1]
        return degree, points[::-1], weights[::-1], tuple(total - t for t in knots[::-1])
    return degree, points, weights, tuple(11 + 2 * (t - knots[0]) for t in knots)


def add_linear(target, polynomial, a, b):
    for i, coefficient in enumerate(polynomial):
        target[i] += a * coefficient
        target[i + 1] += b * coefficient


@lru_cache(maxsize=128)
def spans(original):
    degree, points, weights, knot_values = original
    knots = tuple(map(F, knot_values))
    result = []
    for first, last in zip(sorted(set(knots)), sorted(set(knots))[1:]):
        middle = (first + last) / 2
        previous = [[F(int(a <= middle < b))] for a, b in zip(knots, knots[1:])]
        # Symbolic Cox basis on local z in [0,1], using exact binary input values.
        # Native code evaluates NLib floating point bases and derivatives instead.
        for order in range(1, degree + 1):
            current = []
            for i in range(len(previous) - 1):
                coefficients = [F(0)] * (order + 1)
                width = knots[i + order] - knots[i]
                if width:
                    add_linear(coefficients, previous[i], (first - knots[i]) / width, (last - first) / width)
                width = knots[i + order + 1] - knots[i + 1]
                if width:
                    add_linear(coefficients, previous[i + 1], (knots[i + order + 1] - first) / width,
                               -(last - first) / width)
                current.append(coefficients)
            previous = current
        homogeneous = []
        for axis in range(4):
            coefficients = []
            for power in range(degree + 1):
                coefficients.append(sum((basis[power] * F(weight) * (F(point[axis]) if axis < 3 else 1)
                                         for basis, point, weight in zip(previous, points, weights)), F(0)))
            homogeneous.append(tuple(coefficients))
        result.append((first, last, tuple(homogeneous)))
    return tuple(result)


def polynomial(coefficients, z, order=0):
    values = list(coefficients)
    for _ in range(order):
        values = [i * v for i, v in enumerate(values)][1:]
    result = F(0)
    for coefficient in reversed(values):
        result = result * z + coefficient
    return result


def value(original, parameter, left=False):
    pieces = spans(original)
    t = min(max(F(parameter), pieces[0][0]), pieces[-1][1])
    selected = pieces[-1] if t == pieces[-1][1] else pieces[0]
    for piece in pieces:
        a, b, _ = piece
        if (left and a < t <= b) or (not left and a <= t < b):
            selected = piece
            break
    a, b, coordinates = selected
    z = (t - a) / (b - a)
    denominator = [polynomial(coordinates[3], z, order) / (b - a)**order for order in range(3)]
    result = [[], [], []]
    for coefficients in coordinates[:3]:
        h = [polynomial(coefficients, z, order) / (b - a)**order for order in range(3)]
        p = h[0] / denominator[0]
        d1 = (h[1] - p * denominator[1]) / denominator[0]
        d2 = (h[2] - 2 * d1 * denominator[1] - p * denominator[2]) / denominator[0]
        for row, number in zip(result, (p, d1, d2)):
            row.append(float(number))
    return result


def windows(case):
    lo, hi = case["clip"]
    return [[lo, hi]] if hi <= 1 else [[lo, 1], [0, hi - 1]]


def parameters(case):
    original = data(case)
    knots = sorted(set(original[3]))
    first, last = case["domain"]
    selected = set(knots)
    for t in knots[1:-1]:
        selected.update([math.nextafter(t, -math.inf), math.nextafter(t, math.inf)])
    selected.update((a + b) / 2 for a, b in zip(knots, knots[1:]))
    for lo, hi in windows(case):
        selected.update(first + (last - first) * q for q in (lo, hi, (lo + hi) / 2))
    return sorted(selected)


def bounds(case):
    # Only constant homogeneous weight permits polynomial extrema here.
    # General positive rational inputs use hull and sample containment contracts.
    from harness import roots
    original = data(case)
    assert len(set(original[2])) == 1
    first, last = case["domain"]
    result = [[math.inf] * 3, [-math.inf] * 3]
    for low, high in windows(case):
        low, high = first + (last - first) * low, first + (last - first) * high
        for a, b, coordinates in spans(original):
            lo, hi = max(F(low), a), min(F(high), b)
            if lo > hi:
                continue
            for axis in range(3):
                coefficients = [v / coordinates[3][0] for v in coordinates[axis]]
                c = list(map(float, coefficients)) + [0] * (4 - len(coefficients))
                candidates = [lo, hi]
                for z in roots(3 * c[3], 2 * c[2], c[1]):
                    t = a + F(z) * (b - a)
                    if lo <= t <= hi:
                        candidates.append(t)
                values = [float(polynomial(coefficients, (t - a) / (b - a))) for t in candidates]
                result[0][axis] = min(result[0][axis], *values)
                result[1][axis] = max(result[1][axis], *values)
    return result


def fixed_cases():
    result = []
    for degree in (1, 2, 3):
        for multiplicity in sorted({1, degree}):
            knots = [0] * (degree + 1) + [.25] * multiplicity + [.75] * degree + [1] * (degree + 1)
            count = len(knots) - degree - 1
            for rational in (False, True):
                result.append({"kind": "nurbs", "degree": degree,
                               "points": [[i, (i % 3) - 1, (i % 2) / 2] for i in range(count)],
                               "weights": [(.5 if i % 2 else 1) if rational else 1 for i in range(count)],
                               "knots": knots, "domain": [-7, 9], "clip": [.125, .875]})
    for rational in (False, True):
        result.append({"kind": "periodic_nurbs", "points": [[1, 0, 0], [0, 1, 1], [-1, 0, 0], [0, -1, -1]],
                       "weights": [1, .5, 2, 1] if rational else [1] * 4,
                       "domain": [-7, 1], "clip": [.875, 1.125]})
    return result


def check(case, observed):
    from harness import PropertyFailure, near, vector_near
    original = data(case)
    reverse, edited = transformed(original, True), transformed(original)
    scale = max(1, *(abs(v) for point in original[1] for v in point))
    tolerance = 1e-9 + 64 * math.ulp(1.0) * scale
    first, last = case["domain"]
    vector_near(observed["domain"], [first, last], 1e-12, "natural_domain")
    vector_near(observed["edited_domain"], [11, 11 + 2 * (last - first)], 1e-12, "edited_domain")
    intervals = [[first + (last - first) * q for q in window] for window in windows(case)]
    if len(observed["native_intervals"]) != len(intervals):
        raise PropertyFailure("spline_intervals", "seam split count")
    for actual, expected in zip(observed["native_intervals"], intervals):
        vector_near(actual, expected, 1e-12, "spline_intervals")
    vector_near(observed["reversed_interval"], [first + last - intervals[0][1], first + last - intervals[0][0]],
                1e-11, "reverse_interval")
    selected = parameters(case)
    if len(observed["samples"]) != len(selected):
        raise PropertyFailure("sample_count", "spline observation count")

    def check_evaluation(sample, field, model, parameter, left):
        expected = value(model, parameter, left)
        for order in range(3):
            budget = tolerance + 1e-9 * max(1, *(abs(v) for v in expected[order]))
            vector_near(sample[field][order], expected[order], budget, f"spline_{field}_d{order}")
        return expected

    # Establish both limits on the original curve before diagnosing transforms.
    # A reflected ULP neighbor can round onto a knot and expose an evaluator
    # side-selection fault before the original knot is reached in sample order.
    original_values = []
    for t, sample in zip(selected, observed["samples"]):
        near(sample["parameter"], t, 0, "spline_parameter")
        left_value = check_evaluation(sample, "value", original, t, True)
        check_evaluation(sample, "right_value", original, t, False)
        vector_near(sample["point"], left_value[0], tolerance, "spline_point")
        original_values.append(left_value)
    for t, sample, left_value in zip(selected, observed["samples"], original_values):
        expectations = (("reverse", reverse, sample["reverse_parameter"], False),
                        ("reverse_left", reverse, sample["reverse_parameter"], True),
                        ("edited", edited, sample["edited_parameter"], True),
                        ("edited_right", edited, sample["edited_parameter"], False))
        for field, model, parameter, left in expectations:
            check_evaluation(sample, field, model, parameter, left)
        if any(lo <= t <= hi for lo, hi in intervals):
            for axis, coordinate in enumerate(left_value[0]):
                lo, hi = observed["bounds"][0][axis], observed["bounds"][1][axis]
                if not lo - tolerance <= coordinate <= hi + tolerance:
                    raise PropertyFailure("spline_containment", {"parameter": t, "axis": axis})
    if case["kind"] == "periodic_nurbs":
        if not observed["periodic"]:
            raise PropertyFailure("spline_periodic", "native periodic predicate")
        for order in (0, 1):
            start = observed["samples"][0]["right_value"][order]
            end = observed["samples"][-1]["value"][order]
            vector_near(start, end, tolerance + 1e-9 * max(1, *(abs(v) for v in start)), f"spline_seam_d{order}")
    expected_bounds = bounds(case) if len(set(original[2])) == 1 else None
    for side in range(2):
        if expected_bounds is not None:
            vector_near(observed["bounds"][side], expected_bounds[side], tolerance, "spline_analytic_bounds")
        else:
            for axis, actual in enumerate(observed["bounds"][side]):
                if not math.isfinite(actual):
                    raise PropertyFailure("spline_hull", "nonfinite bound")
                extreme = (min if side == 0 else max)(p[axis] for p in original[1])
                if (side == 0 and actual < extreme - tolerance) or (side == 1 and actual > extreme + tolerance):
                    raise PropertyFailure("spline_hull", {"axis": axis})
        for name in ("reverse_bounds", "edited_bounds"):
            vector_near(observed[name][side], observed["bounds"][side], tolerance, f"spline_{name}")
