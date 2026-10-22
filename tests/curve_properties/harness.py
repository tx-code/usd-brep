#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent curve properties; the native probe only returns observations."""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys

if __name__ == "__main__":
    sys.modules["harness"] = sys.modules[__name__]
import spline_oracle as spline


class PropertyFailure(AssertionError):
    def __init__(self, name, detail):
        self.name = name
        self.detail = detail
        super().__init__(f"{name}: {detail}")


def near(actual, expected, tolerance, name):
    if not math.isfinite(actual) or not math.isfinite(expected) or abs(actual - expected) > tolerance:
        raise PropertyFailure(name, {"actual": actual, "expected": expected, "tolerance": tolerance})


def vector_near(actual, expected, tolerance, name):
    if len(actual) != len(expected):
        raise PropertyFailure(name, "vector size")
    for a, b in zip(actual, expected):
        near(a, b, tolerance, name)


def bernstein(values, t):
    """Explicit Bernstein basis, independent of the kernel's NURBS evaluator."""
    n = len(values) - 1
    return math.fsum(math.comb(n, i) * t**i * (1 - t)**(n - i) * v for i, v in enumerate(values))


def derivatives(values, t):
    n = len(values) - 1
    first = [n * (b - a) for a, b in zip(values, values[1:])]
    second = [(n - 1) * (b - a) for a, b in zip(first, first[1:])]
    return bernstein(values, t), bernstein(first, t), bernstein(second, t) if second else 0.0


def bezier_value(case, q):
    weights = case["weights"]
    w, w1, w2 = derivatives(weights, q)
    result = [[], [], []]
    length = case["domain"][1] - case["domain"][0]
    for axis in range(3):
        h, h1, h2 = derivatives([p[axis] * v for p, v in zip(case["points"], weights)], q)
        position = h / w
        tangent = (h1 - position * w1) / w
        second = (h2 - 2 * tangent * w1 - position * w2) / w
        for row, value in zip(result, (position, tangent / length, second / length**2)):
            row.append(value)
    return result


def roots(a, b, c):
    """Real roots of a quadratic, including the linear and constant cases."""
    if a == 0:
        return [] if b == 0 else [-c / b]
    discriminant = b * b - 4 * a * c
    if discriminant < 0:
        return []
    q = -0.5 * (b + math.copysign(math.sqrt(discriminant), b))
    return [-b / (2 * a)] if q == 0 else [q / a, c / q]


def polynomial_bounds(case):
    lower, upper = case["clip"]
    result = [[], []]
    for axis in range(3):
        p = [point[axis] for point in case["points"]]
        if len(p) == 2:
            candidates = []
        elif len(p) == 3:
            candidates = roots(0, 2 * (p[2] - 2 * p[1] + p[0]), 2 * (p[1] - p[0]))
        else:
            candidates = roots(3 * (p[3] - 3 * p[2] + 3 * p[1] - p[0]),
                               6 * (p[2] - 2 * p[1] + p[0]), 3 * (p[1] - p[0]))
        values = [bernstein(p, q) for q in [lower, upper] + [q for q in candidates if lower <= q <= upper]]
        result[0].append(min(values))
        result[1].append(max(values))
    return result


def frame(case):
    return ((1, 0, 0), (0, 1, 0)) if case["frame"] == 0 else ((.6, .8, 0), (-.48, .36, .8))


def conic_point(case, angle):
    x, y = frame(case)
    a, b = case["radii"]
    theta = math.radians(angle)
    return [c + a * u * math.cos(theta) + b * v * math.sin(theta)
            for c, u, v in zip(case["center"], x, y)]


def conic_bounds(case):
    x, y = frame(case)
    a, b = case["radii"]
    lower, upper = [q * 2 * math.pi for q in case["clip"]]
    result = [[], []]
    for axis in range(3):
        root = math.atan2(b * y[axis], a * x[axis])
        candidates = [lower, upper] + [root + k * math.pi for k in range(-2, 5)
                                       if lower <= root + k * math.pi <= upper]
        values = [case["center"][axis] + a * x[axis] * math.cos(t) + b * y[axis] * math.sin(t)
                  for t in candidates]
        result[0].append(min(values))
        result[1].append(max(values))
    return result


def fixed_cases():
    cases = []
    for degree in (1, 2, 3):
        points = [[0, 0, 0], [2, 4, -1], [5, -3, 2], [8, 1, 0]][:degree + 1]
        for rational in (False, True):
            cases.append({"kind": "bezier", "points": points,
                          "weights": [1, .5, 2, 1][:degree + 1] if rational else [1] * (degree + 1),
                          "domain": [-7, 13], "clip": [.125, .875]})
    # Interior extrema, a zero derivative, an asymmetric clip, and full periods.
    cases.append({"kind": "bezier", "points": [[0, 0, 0], [1, 4, 0], [2, 0, 0]],
                  "weights": [1, 1, 1], "domain": [3, 5], "clip": [0, 1]})
    cases.append({"kind": "bezier", "points": [[0, 0, 0], [1, 0, 0], [0, 0, 0], [1, 0, 0]],
                  "weights": [1, 1, 1, 1], "domain": [0, 1], "clip": [.25, .75]})
    for kind, radii in (("circle", [3, 3]), ("ellipse", [7, .25])):
        for clip in ([0, 1], [.125, .625], [.25, .5]):
            cases.append({"kind": kind, "center": [2, -3, 5], "radii": radii,
                          "frame": 1, "domain": [-17, 29], "clip": clip})
        for clip in ([.875, 1.125], [.25, 1.25]):
            cases.append({"kind": kind, "center": [2, -3, 5], "radii": radii,
                          "frame": 1, "domain": [-17, 29], "clip": clip})
    cases.extend(spline.fixed_cases())
    return cases


def samples(case):
    lower, upper = case["clip"]
    return sorted({q - 1 if q > 1 else q
                   for q in (0.0, .125, .25, .5, .75, .875, 1.0, lower, upper, (lower + upper) / 2)})


def encode(case):
    if case["kind"] in spline.KINDS:
        degree, points, weights, knots = spline.data(case)
        values = ["curve-properties-v2", case["kind"], *case["domain"], *case["clip"],
                  degree, len(points), len(knots)]
        for point, weight in zip(points, weights):
            values.extend([*point, weight])
        values.extend([*knots, len(spline.parameters(case)), *spline.parameters(case)])
        return " ".join(map(str, values)) + "\n"
    values = ["curve-properties-v1", case["kind"], *case["domain"], *case["clip"]]
    if case["kind"] == "bezier":
        values.append(len(case["points"]) - 1)
        for point, weight in zip(case["points"], case["weights"]):
            values.extend([*point, weight])
    else:
        values.extend([*case["center"], *case["radii"], case["frame"]])
    values.extend([len(samples(case)), *samples(case)])
    return " ".join(map(str, values)) + "\n"


def observe(probe, case, timeout=10):
    env = os.environ.copy()
    # Prefer the selected SDK; receipts in the exploration runner identify its DLLs.
    env["PATH"] = str(probe.parent) + os.pathsep + env.get("PATH", "")
    for key in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
        env[key] = str(probe.parent) + os.pathsep + env.get(key, "")
    try:
        result = subprocess.run([str(probe)], input=encode(case), text=True, capture_output=True,
                                timeout=timeout, env=env)
    except subprocess.TimeoutExpired as error:
        raise PropertyFailure("timeout", {"seconds": timeout,
                                          "stdout": (error.stdout or b"").decode("utf-8", errors="replace"),
                                          "stderr": (error.stderr or b"").decode("utf-8", errors="replace")}) from error
    if result.returncode:
        raise PropertyFailure("native_failure", {"returncode": result.returncode,
                                                "stdout": result.stdout, "stderr": result.stderr})
    try:
        return json.loads(result.stdout)
    except ValueError as error:
        raise PropertyFailure("native_protocol", {"stdout": result.stdout, "stderr": result.stderr}) from error


def check(case, observed):
    if case["kind"] in spline.KINDS:
        return spline.check(case, observed)
    scale = max(1, *(abs(x) for p in case.get("points", [case.get("center", [0, 0, 0])]) for x in p),
                *case.get("radii", [1]))
    # Fixed test budgets on a bounded input domain; never change product tolerances.
    position_tolerance = 1e-9 + 64 * sys.float_info.epsilon * scale
    first, last = case["domain"]
    if case["kind"] in ("circle", "ellipse") and case["clip"][1] > 1:
        if len(observed["native_intervals"]) != 2:
            raise PropertyFailure("seam_interval_count", "cross-seam source window requires both native intervals")
    vector_near(observed["domain"], [first, last], 1e-12, "natural_domain")
    vector_near(observed["edited_domain"], [11, 11 + 2 * (last - first)], 1e-12, "edited_domain")
    native_low, native_high = observed["native_interval"]
    vector_near(observed["reversed_interval"], [first + last - native_high, first + last - native_low],
                1e-11, "reverse_interval")
    if len(observed["samples"]) != len(samples(case)):
        raise PropertyFailure("sample_count", "native observation count")
    for q, sample in zip(samples(case), observed["samples"]):
        value = sample["value"]
        vector_near(sample["point"], value[0], position_tolerance, "point_evaluator")
        if case["kind"] == "bezier":
            expected = bezier_value(case, q)
            for order in range(3):
                tolerance = position_tolerance + 1e-9 * max(1, *(abs(v) for v in expected[order]))
                vector_near(value[order], expected[order], tolerance, f"bernstein_d{order}")
        else:
            # Same clipped-conic converter budget as tests/conic_bounds/bounds.cpp.
            tolerance = 2e-6 * max(case["radii"]) + position_tolerance
            vector_near(value[0], conic_point(case, sample["angle"]), tolerance, "angular_correspondence")
            if 0 < q < 1:  # Periodic endpoints may normalize to the other end.
                near(sample["roundtrip"], first + q * (last - first), 2e-6 * (last - first), "angular_roundtrip")
        for order in range(3):
            tolerance = position_tolerance + 1e-9 * max(1, *(abs(v) for v in value[order]))
            vector_near(sample["reverse"][order], [(-1)**order * v for v in value[order]],
                        tolerance, f"reverse_d{order}")
            vector_near(sample["edited"][order], [v / 2**order for v in value[order]],
                        tolerance, f"reparameterize_d{order}")
        # Sampling checks containment at these parameters only, not a continuous bound.
        if case["kind"] == "bezier" and case["clip"][0] <= q <= case["clip"][1]:
            for axis, coordinate in enumerate(value[0]):
                lo, hi = observed["bounds"][0][axis], observed["bounds"][1][axis]
                if not lo - position_tolerance <= coordinate <= hi + position_tolerance:
                    raise PropertyFailure("sample_containment", {"q": q, "axis": axis})
    bounds_tolerance = position_tolerance
    if case["kind"] != "bezier":
        expected_bounds = conic_bounds(case)
        bounds_tolerance += 1e-10 if case["clip"] == [0, 1] else 2e-6 * max(case["radii"])
    elif len(set(case["weights"])) == 1:
        expected_bounds = polynomial_bounds(case)
    else:
        expected_bounds = None
        for axis in range(3):
            lo = min(p[axis] for p in case["points"])
            hi = max(p[axis] for p in case["points"])
            if (observed["bounds"][0][axis] < lo - bounds_tolerance
                    or observed["bounds"][1][axis] > hi + bounds_tolerance):
                raise PropertyFailure("positive_weight_hull", {"axis": axis})
    for side in range(2):
        if expected_bounds is not None:
            vector_near(observed["bounds"][side], expected_bounds[side], bounds_tolerance, "analytic_bounds")
        for name in ("reverse_bounds", "edited_bounds"):
            vector_near(observed[name][side], observed["bounds"][side], bounds_tolerance, name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["check"])
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    passed = 0
    for index, case in enumerate(fixed_cases()):
        try:
            check(case, observe(args.probe.resolve(), case))
            passed += 1
        except PropertyFailure as error:
            print(json.dumps({"status": "fail", "index": index, "property": error.name,
                              "detail": str(error), "case": case}, allow_nan=False))
            return 1
    print(json.dumps({"status": "pass", "cases": passed, "scope": "curve-properties-v2"}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
