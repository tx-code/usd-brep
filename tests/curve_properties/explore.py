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

SCHEMA = "curve-properties-replay-v1"


def validate(case):
    def finite_vector(values, size, bound=4096):
        if not isinstance(values, list) or len(values) != size:
            raise ValueError("input vector size")
        if any(type(x) not in (int, float) or not math.isfinite(x) or abs(x) > bound for x in values):
            raise ValueError("finite bounded numeric input required")

    if not isinstance(case, dict) or case.get("kind") not in ("bezier", "circle", "ellipse"):
        raise ValueError("supported curve kind required")
    finite_vector(case.get("domain"), 2, 64)
    first, last = case["domain"]
    if not .125 <= last - first <= 64:
        raise ValueError("domain length outside pilot scope")
    finite_vector(case.get("clip"), 2, 1)
    lower, upper = case["clip"]
    if not 0 <= lower < upper <= 1 or upper - lower < 1 / 64:
        raise ValueError("clip outside pilot scope")
    if case["kind"] == "bezier":
        points = case.get("points")
        if not isinstance(points, list) or not 2 <= len(points) <= 4:
            raise ValueError("Bezier degree must be 1..3")
        for point in points:
            finite_vector(point, 3)
        finite_vector(case.get("weights"), len(points), 64)
        if any(w < 1 / 64 for w in case["weights"]):
            raise ValueError("positive bounded weights required")
        if len({tuple(p) for p in points}) < 2:
            raise ValueError("constant curves are outside this pilot")
    else:
        finite_vector(case.get("center"), 3)
        finite_vector(case.get("radii"), 2, 64)
        if min(case["radii"]) < .125 or case.get("frame") not in (0, 1):
            raise ValueError("conic radii/frame outside pilot scope")
        if case["kind"] == "circle" and case["radii"][0] != case["radii"][1]:
            raise ValueError("circle radii must agree")


def generate(seed, count):
    rng = random.Random(seed)
    for index in range(count):
        first = rng.randrange(-64, 65) / 4
        length = rng.choice([.25, .5, 1, 2, 8, 32])
        lo, hi = sorted(rng.sample(range(33), 2))
        clip = rng.choice([[0, 1], [.25, .5], [lo / 32, hi / 32]])
        domain = [first, first + length]
        family = index % 8
        if family < 6:
            degree = family // 2 + 1
            scale = rng.choice([.25, 1, 4, 16])
            offset = [rng.choice([0, -256, 32, 256]) for _ in range(3)]
            points = [[offset[0] + scale * i,
                       offset[1] + scale * rng.randrange(-8, 9) / 4,
                       offset[2] + scale * rng.randrange(-8, 9) / 4] for i in range(degree + 1)]
            weights = [rng.choice([.25, .5, 1, 2, 4]) if family % 2 else 1 for _ in points]
            case = {"kind": "bezier", "points": points, "weights": weights, "domain": domain, "clip": clip}
        else:
            a = rng.choice([.25, 1, 3, 7, 16])
            b = a if family == 6 else rng.choice([.125, .25, 1, 3])
            case = {"kind": "circle" if family == 6 else "ellipse", "center":
                    [rng.randrange(-128, 129) / 4 for _ in range(3)], "radii": [a, b],
                    "frame": rng.randrange(2), "domain": domain, "clip": clip}
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


def reductions(case):
    """Domain-aware reductions, always preserving schema and a strict complexity decrease."""
    proposals = []
    if case["kind"] == "bezier":
        if len(case["points"]) > 2:
            for index in range(1, len(case["points"]) - 1):
                candidate = copy.deepcopy(case)
                candidate["points"].pop(index)
                candidate["weights"].pop(index)
                proposals.append(candidate)
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
        if key not in seen and complexity(candidate) < complexity(case):
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
               if path.name.lower() == "smlib.dll" or path.name.startswith("libSMLib.so") or path.name == "libSMLib.dylib"]
    if not kernels:
        raise ValueError("exploration requires a co-located SMLib runtime; use the isolated kernel build")
    return {str(path.resolve()): digest(path) for path in [probe, *kernels]}


def write(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")


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
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, capture_output=True, text=True, check=True).stdout.strip()
    status = subprocess.run(["git", "status", "--short"], cwd=root, capture_output=True, text=True, check=True).stdout
    source_identity = {str(path.relative_to(root)): digest(path) for path in Path(__file__).parent.glob("*.py")}
    provenance = {"revision": revision, "working_changes": status, "binaries": native_identity,
                  "harness": source_identity, "python": sys.version, "platform": sys.platform,
                  "timeout_seconds": args.timeout}
    if args.command == "run":
        cases = harness.fixed_cases() + list(generate(args.seed, args.cases))
    else:
        record = json.loads(args.input.read_text(encoding="utf-8"))
        if record.get("schema") != SCHEMA:
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
    summary = {"schema": SCHEMA, "status": "running", "seed": args.seed, "selected": len(cases),
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
                           "observation": getattr(error, "observation", None)}
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
