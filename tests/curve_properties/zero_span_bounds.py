#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Literal bounds for stationary spans and collinear curves that turn back."""
import argparse
import copy
import json
from pathlib import Path
import sys

import harness


def cases():
    fixture = Path(__file__).with_name("fixtures") / "zero-span-quadratic-bounds.json"
    original = json.loads(fixture.read_text(encoding="utf-8"))["case"]
    selected = []
    for axis in range(3):
        for origin in ([0, 0, 0], [2, -3, 5]):
            for rational in (False, True):
                case = copy.deepcopy(original)
                case["points"] = [list(origin) for _ in range(5)]
                case["points"][3][axis] += 1
                # Retain the unmodified frozen input as one control alongside
                # the shifted actual-parameter domains.
                if axis != 2 or origin != [0, 0, 0] or rational:
                    case["domain"] = [-17, 29]
                if rational:
                    case["weights"][3] = 2
                maximum = 2 / 3 if rational else .5
                bounds = [list(origin), list(origin)]
                bounds[1][axis] += maximum
                selected.append((f"axis-{axis}-origin-{origin}-rational-{rational}", case, bounds))
        for clip, maximum in (([0, .75], 0), ([.75, 1], .5)):
            case = copy.deepcopy(original)
            case["points"] = [[0, 0, 0] for _ in range(5)]
            case["points"][3][axis] = 1
            case["clip"] = clip
            bounds = [[0, 0, 0], [0, 0, 0]]
            bounds[1][axis] = maximum
            selected.append((f"axis-{axis}-clip-{clip}", case, bounds))
    for degree, points, maximum in ((2, [0, 1, 0], .5), (3, [0, 1, 1, 0], .75)):
        case = {"kind": "bezier", "points": [[0, 0, z] for z in points],
                "weights": [1] * (degree + 1), "domain": [3, 5], "clip": [0, 1]}
        selected.append((f"collinear-bezier-{degree}", case, [[0, 0, 0], [0, 0, maximum]]))
    middle = copy.deepcopy(original)
    middle["points"] = [[0, 0, z] for z in (0, 1, 0, 0, 0, -1, 0)]
    middle["weights"] = [1] * 7
    middle["knots"] = [0, 0, 0, .25, .25, .75, .75, 1, 1, 1]
    selected.append(("constant-middle-two-turns", middle, [[0, 0, -.5], [0, 0, .5]]))
    noncollinear = copy.deepcopy(original)
    noncollinear["points"][3][0] = 1
    noncollinear["points"][4][0] = 2
    selected.append(("noncollinear-control", noncollinear, [[0, 0, 0], [2, 0, .5]]))
    clipped = copy.deepcopy(original)
    clipped["clip"] = [.8125, .9375]
    selected.append(("clipped-turn", clipped, [[0, 0, .375], [0, 0, .5]]))
    line = {"kind": "bezier", "points": [[0, 0, 0], [2, 4, -1]],
            "weights": [1, 1], "domain": [-7, 13], "clip": [.25, .75]}
    selected.append(("ordinary-line-control", line, [[.5, 1, -.75], [1.5, 3, -.25]]))
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    for name, case, expected in cases():
        try:
            observed = harness.observe(args.probe.resolve(), case)
            harness.check(case, observed)
            for side in (0, 1):
                harness.vector_near(observed["bounds"][side], expected[side], 1e-9, "stationary_bounds")
        except harness.PropertyFailure as error:
            print(json.dumps({"status": "fail", "case": name, "property": error.name, "detail": error.detail}))
            return 1
    print(json.dumps({"status": "pass", "cases": len(cases())}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
