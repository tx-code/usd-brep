#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Bounded random curve exploration, complete-input replay, and failure reduction."""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import random
import subprocess
import sys
import time
import uuid

import harness
import spline_oracle as spline

SCHEMA = "curve-properties-replay-v2"


def validate(case):
    def finite_vector(values, size, bound=4096):
        if not isinstance(values, list) or len(values) != size:
            raise ValueError("input vector size")
        if any(type(x) not in (int, float) or not math.isfinite(x) or abs(x) > bound for x in values):
            raise ValueError("finite bounded numeric input required")

    if not isinstance(case, dict) or case.get("kind") not in ("bezier", "circle", "ellipse", *spline.KINDS):
        raise ValueError("supported curve kind required")
    finite_vector(case.get("domain"), 2, 64)
    first, last = case["domain"]
    if not .125 <= last - first <= 64:
        raise ValueError("domain length outside pilot scope")
    finite_vector(case.get("clip"), 2, 2)
    lower, upper = case["clip"]
    periodic = case["kind"] in ("circle", "ellipse", "periodic_nurbs")
    if not 0 <= lower < 1 or not lower < upper <= (lower + 1 if periodic else 1) or upper - lower < 1 / 64:
        raise ValueError("clip outside pilot scope")
    if case["kind"] == "bezier" or case["kind"] in spline.KINDS:
        points = case.get("points")
        maximum = 4 if case["kind"] == "bezier" else 8 if case["kind"] == "periodic_nurbs" else 25
        minimum = 3 if case["kind"] == "periodic_nurbs" else 2
        if not isinstance(points, list) or not minimum <= len(points) <= maximum:
            raise ValueError("control count outside pilot scope")
        for point in points:
            finite_vector(point, 3)
        finite_vector(case.get("weights"), len(points), 64)
        if any(w < 1 / 64 for w in case["weights"]):
            raise ValueError("positive bounded weights required")
        if len({tuple(p) for p in points}) < 2:
            raise ValueError("constant curves are outside this pilot")
        if case["kind"] == "nurbs":
            degree, knots = case.get("degree"), case.get("knots")
            if type(degree) is not int or not 1 <= degree <= 3:
                raise ValueError("NURBS degree must be 1..3")
            finite_vector(knots, len(points) + degree + 1, 1)
            if knots != sorted(knots) or knots[:degree + 1] != [0] * (degree + 1):
                raise ValueError("ordered, clamped normalized knot vector required")
            if knots[-degree - 1:] != [1] * (degree + 1):
                raise ValueError("clamped upper knots required")
            if knots.count(0) != degree + 1 or knots.count(1) != degree + 1:
                raise ValueError("endpoint multiplicities must equal degree+1")
            unique = sorted(set(knots))
            if not 3 <= len(unique) <= 9 or any(b - a < 1 / 32 for a, b in zip(unique, unique[1:])):
                raise ValueError("require 2..8 nonempty spans separated by at least 1/32")
            if any(knots.count(knot) > degree for knot in unique[1:-1]):
                raise ValueError("internal multiplicity must be 1..degree (C0 or better)")
    else:
        finite_vector(case.get("center"), 3)
        finite_vector(case.get("radii"), 2, 64)
        if min(case["radii"]) < .125 or case.get("frame") not in (0, 1):
            raise ValueError("conic radii/frame outside pilot scope")
        if case["kind"] == "circle" and case["radii"][0] != case["radii"][1]:
            raise ValueError("circle radii must agree")


def fixed_cases(suite="all"):
    cases = harness.fixed_cases()
    if suite == "legacy":
        return [case for case in cases if case["kind"] not in spline.KINDS and case["clip"][1] <= 1]
    if suite == "multispan":
        return [case for case in cases if case["kind"] in spline.KINDS]
    if suite == "repeated":
        return [case for case in cases if case["kind"] == "nurbs" and case["degree"] > 1
                and all(case["knots"].count(t) == case["degree"] for t in sorted(set(case["knots"]))[1:-1])]
    if suite == "seams":
        return [case for case in cases if case["clip"][1] > 1]
    return cases


def generate(seed, count, suite="all"):
    rng = random.Random(seed)
    families = {"all": list(range(18)), "legacy": list(range(8)),
                "multispan": list(range(8, 16)), "repeated": list(range(10, 14)),
                "seams": list(range(14, 18))}[suite]
    for index in range(count):
        family = families[index % len(families)]
        first = rng.randrange(-64, 65) / 4
        length = rng.choice([.25, .5, 1, 2, 8, 32] if family < 8 else [2, 8, 16, 32])
        lo, hi = sorted(rng.sample(range(33), 2))
        clip = rng.choice([[0, 1], [.25, .5], [lo / 32, hi / 32]])
        domain = [first, first + length]
        if family < 6:
            degree = family // 2 + 1
            scale = rng.choice([.25, 1, 4, 16])
            offset = [rng.choice([0, -256, 32, 256]) for _ in range(3)]
            points = [[offset[0] + scale * i,
                       offset[1] + scale * rng.randrange(-8, 9) / 4,
                       offset[2] + scale * rng.randrange(-8, 9) / 4] for i in range(degree + 1)]
            weights = [rng.choice([.25, .5, 1, 2, 4]) if family % 2 else 1 for _ in points]
            case = {"kind": "bezier", "points": points, "weights": weights, "domain": domain, "clip": clip}
        elif 8 <= family < 14:
            degree = (family - 8) // 2 + 1
            joins = sorted(rng.sample(range(1, 16), rng.randrange(1, 8)))
            knots = [0] * (degree + 1)
            for knot in joins:
                multiplicity = degree if suite == "repeated" else rng.choice([1, degree])
                knots.extend([knot / 16] * multiplicity)
            knots.extend([1] * (degree + 1))
            size = len(knots) - degree - 1
            scale = rng.choice([.5, 1, 4])
            offset = [rng.choice([0, -32, 32]) for _ in range(3)]
            points = [[offset[0] + i * scale, offset[1] + scale * rng.randrange(-4, 5),
                       offset[2] + scale * rng.randrange(-4, 5)] for i in range(size)]
            weights = [rng.choice([.5, 1, 2]) if family % 2 else 1 for _ in points]
            clip = rng.choice([clip, [joins[0] / 16, 1], [0, joins[-1] / 16]])
            case = {"kind": "nurbs", "degree": degree, "points": points, "weights": weights,
                    "knots": knots, "domain": domain, "clip": clip}
        elif family in (14, 15):
            size = rng.choice([4, 8])
            offset = [rng.choice([0, -8, 16]) for _ in range(3)]
            a, b, height = rng.choice([.5, 1, 4]), rng.choice([.5, 1, 4]), rng.choice([0, .125, .5])
            points = [[offset[0] + a * math.cos(2 * math.pi * i / size),
                       offset[1] + b * math.sin(2 * math.pi * i / size),
                       offset[2] + height * math.sin(4 * math.pi * i / size)] for i in range(size)]
            case = {"kind": "periodic_nurbs", "points": points,
                    "weights": [rng.choice([.5, 1, 2]) if family % 2 else 1 for _ in points],
                    "domain": domain, "clip": rng.choice([[.875, 1.125], [.75, 1.25], [13 / 16, 19 / 16]])}
        else:
            a = rng.choice([.25, 1, 3, 7, 16])
            circle = family in (6, 16)
            b = a if circle else rng.choice([.125, .25, 1, 3])
            case = {"kind": "circle" if circle else "ellipse", "center":
                    [rng.randrange(-128, 129) / 4 for _ in range(3)], "radii": [a, b],
                    "frame": rng.randrange(2), "domain": domain, "clip": clip}
            if family >= 16:
                case["clip"] = rng.choice([[.875, 1.125], [.75, 1.25], [13 / 16, 19 / 16]])
        validate(case)
        yield case


def complexity(case):
    def cost(value):
        if isinstance(value, dict):
            return sum(cost(v) for v in value.values())
        if isinstance(value, list):
            return sum(cost(v) for v in value)
        if type(value) in (int, float):
            return 0 if value == 0 else 1 if abs(value) == 1 else 2 + abs(value)
        return 0
    return len(case.get("points", [])), cost(case)


def features(case):
    if case["kind"] == "nurbs":
        degree = case["degree"]
        multiplicities = [case["knots"].count(knot) for knot in sorted(set(case["knots"]))[1:-1]]
        return case["kind"], degree, max(multiplicities) > 1, degree in multiplicities, False
    return case["kind"], case["clip"][1] > 1


def reductions(case):
    """Domain-aware reductions, always preserving schema and a strict complexity decrease."""
    proposals = []
    if case["kind"] in ("bezier", "periodic_nurbs"):
        if len(case["points"]) > 2:
            for index in range(1, len(case["points"]) - 1):
                candidate = copy.deepcopy(case)
                candidate["points"].pop(index)
                candidate["weights"].pop(index)
                proposals.append(candidate)
    if case["kind"] == "nurbs":
        for knot in sorted(set(case["knots"]))[1:-1]:
            # Removing a knot group and the same number of CPs creates a smaller
            # valid input, not a shape-preserving kernel knot-removal operation.
            for count in sorted({1, case["knots"].count(knot)}):
                candidate = copy.deepcopy(case)
                for _ in range(count):
                    candidate["knots"].remove(knot)
                    index = min(case["degree"], len(candidate["points"]) - 2)
                    candidate["points"].pop(index)
                    candidate["weights"].pop(index)
                proposals.append(candidate)
    if case["kind"] == "bezier" or case["kind"] in spline.KINDS:
        candidate = copy.deepcopy(case)
        candidate["weights"] = [1] * len(candidate["weights"])
        proposals.append(candidate)
    for key, value in (("domain", [0, 1]), ("clip", [0, 1]), ("clip", [0, .5]), ("clip", [.5, 1])):
        candidate = copy.deepcopy(case)
        candidate[key] = value
        proposals.append(candidate)

    def paths(value, path=()):
        if isinstance(value, dict):
            for key, item in value.items():
                yield from paths(item, (*path, key))
        elif isinstance(value, list):
            for index, item in enumerate(value):
                yield from paths(item, (*path, index))
        elif type(value) in (int, float):
            yield path, value

    for path, value in paths(case):
        for simpler in (0, math.copysign(1, value), value / 2):
            candidate = copy.deepcopy(case)
            slot = candidate
            for key in path[:-1]:
                slot = slot[key]
            slot[path[-1]] = simpler
            if case["kind"] == "circle" and path[0] == "radii":
                candidate["radii"] = [simpler, simpler]
            proposals.append(candidate)
    seen = set()
    for candidate in proposals:
        try:
            validate(candidate)
        except ValueError:
            continue
        key = json.dumps(candidate, sort_keys=True)
        if key not in seen and complexity(candidate) < complexity(case) and features(candidate) == features(case):
            seen.add(key)
            yield candidate


def minimize(case, still_fails, budget=100, seconds=30):
    reduced = copy.deepcopy(case)
    attempts = 0
    deadline = time.monotonic() + seconds
    while attempts < budget and time.monotonic() < deadline:
        for candidate in reductions(reduced):
            if attempts >= budget or time.monotonic() >= deadline:
                return reduced, attempts
            attempts += 1
            if still_fails(candidate):
                reduced = candidate
                break
        else:
            break
    return reduced, attempts


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def identity(probe):
    # Require a co-located kernel instead of guessing which DLL a system PATH loads.
    kernels = [path for path in probe.parent.iterdir()
               if path.name.lower() == "smlib.dll" or path.name.startswith("libSMLib.so")
               or path.name == "libSMLib.dylib"]
    if not kernels:
        raise ValueError("exploration requires a co-located SMLib runtime; use the isolated kernel build")
    return {str(path.resolve()): digest(path) for path in [probe, *kernels]}


def write(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")


def repository_context(root):
    try:
        result = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=root, capture_output=True, text=True)
    except FileNotFoundError:
        return {"revision": None, "working_changes": None}
    if result.returncode or Path(result.stdout.strip()).resolve() != root.resolve():
        # An exported snapshot can live inside another checkout or outside Git.
        # Its manifest owns source identity; a parent checkout's HEAD does not.
        return {"revision": None, "working_changes": None}
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root,
                              capture_output=True, text=True, check=True).stdout.strip()
    status = subprocess.run(["git", "status", "--short"], cwd=root,
                            capture_output=True, text=True, check=True).stdout
    return {"revision": revision, "working_changes": status}


def evaluate(probe, case, timeout):
    validate(case)
    observation = harness.observe(probe, case, timeout)
    try:
        harness.check(case, observation)
    except harness.PropertyFailure as error:
        error.observation = observation
        raise
    return observation


def signature(error):
    # A different crash code is not evidence of reproducing the original crash.
    return error.name, error.detail.get("returncode") if isinstance(error.detail, dict) else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["run", "replay", "shrink"])
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new receipt directory")
    parser.add_argument("--unique-output", action="store_true", help="create a fresh child below --output (CTest)")
    parser.add_argument("--input", type=Path, help="complete replay/failure JSON, independent of seed")
    parser.add_argument("--seed", type=int, default=431)
    parser.add_argument("--suite", choices=["all", "legacy", "multispan", "repeated", "seams"], default="all")
    parser.add_argument("--cases", type=int, default=128)
    parser.add_argument("--timeout", type=float, default=10)
    parser.add_argument("--shrink-budget", type=int, default=100)
    parser.add_argument("--shrink-seconds", type=float, default=30)
    args = parser.parse_args()
    if not 1 <= args.cases <= 100000 or not 0 < args.timeout <= 120:
        parser.error("require 1..100000 cases and timeout in (0,120]")
    if not 0 <= args.shrink_budget <= 10000 or not 0 < args.shrink_seconds <= 120:
        parser.error("require bounded reduction budget/time")
    if args.command != "run" and not args.input:
        parser.error("replay/shrink requires --input")
    probe = args.probe.resolve()
    native_identity = identity(probe)
    root = Path(__file__).resolve().parents[2]
    source_identity = {str(path.relative_to(root)): digest(path) for path in Path(__file__).parent.glob("*.py")}
    provenance = {**repository_context(root), "binaries": native_identity,
                  "harness": source_identity, "python": sys.version, "platform": sys.platform,
                  "timeout_seconds": args.timeout}
    if args.command == "run":
        cases = fixed_cases(args.suite) + list(generate(args.seed, args.cases, args.suite))
    else:
        record = json.loads(args.input.read_text(encoding="utf-8"))
        if record.get("schema") not in (SCHEMA, "curve-properties-replay-v1"):
            raise ValueError("unsupported replay schema")
        cases = [record["case"]]
    for case in cases:
        validate(case)
    if args.unique_output:
        args.output = args.output / uuid.uuid4().hex
    args.output.mkdir(parents=True, exist_ok=False)
    # Save all materialized inputs before execution; RNG/version changes cannot lose a failure.
    with (args.output / "inputs.jsonl").open("w", encoding="utf-8") as stream:
        for index, case in enumerate(cases):
            stream.write(json.dumps({"schema": SCHEMA, "index": index, "case": case}, allow_nan=False) + "\n")
    summary = {"schema": SCHEMA, "status": "running", "seed": args.seed, "suite": args.suite, "selected": len(cases),
               "completed": 0, "failures": 0, "provenance": provenance}
    write(args.output / "summary.json", summary)
    started = time.monotonic()
    with (args.output / "observations.jsonl").open("w", encoding="utf-8") as observations:
        for index, case in enumerate(cases):
            try:
                observation = evaluate(probe, case, args.timeout)
                observations.write(json.dumps({"index": index, "status": "pass", "observation": observation},
                                              allow_nan=False) + "\n")
            except harness.PropertyFailure as error:
                summary["failures"] += 1
                failure = {"schema": SCHEMA, "case": case, "index": index, "seed": args.seed,
                           "property": error.name, "detail": error.detail, "provenance": provenance,
                           "observation": getattr(error, "observation", None), "features": features(case)}
                write(args.output / "failure.json", failure)
                if args.command in ("run", "shrink"):
                    wanted = signature(error)

                    def still_fails(candidate):
                        try:
                            evaluate(probe, candidate, args.timeout)
                        except harness.PropertyFailure as other:
                            return signature(other) == wanted
                        return False

                    reduced, attempts = minimize(case, still_fails, args.shrink_budget, args.shrink_seconds)
                    minimized = {**failure, "case": reduced, "attempts": attempts,
                                 "original_case": case, "original_detail": error.detail,
                                 "observation": None, "reproduced": False}
                    try:
                        evaluate(probe, reduced, args.timeout)
                    except harness.PropertyFailure as confirmed:
                        minimized.update(detail=confirmed.detail, observation=getattr(confirmed, "observation", None),
                                         reproduced=signature(confirmed) == wanted)
                    write(args.output / "minimized.json", minimized)
                summary["completed"] = index + 1
                break
            summary["completed"] = index + 1
    summary["status"] = "fail" if summary["failures"] else "pass"
    summary["elapsed_seconds"] = time.monotonic() - started
    if identity(probe) != native_identity:
        summary["status"] = "infrastructure_error"
        summary["detail"] = "native binaries changed while testing"
    write(args.output / "summary.json", summary)
    print(json.dumps({"output": str(args.output.resolve()), **{key: summary[key] for key in
                     ("status", "selected", "completed", "failures", "elapsed_seconds")}}))
    return 0 if summary["status"] == "pass" else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(json.dumps({"status": "infrastructure_error", "detail": str(error)}), file=sys.stderr)
        sys.exit(2)
