# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Run real compiler cases; rejection needs a passing control and an error marker."""

import argparse
from pathlib import Path
import re
import subprocess

from .catalog import ROOT, encoded, loads

DEFAULT_MANIFEST = Path(__file__).parent / "compile/cases.json"


def assess(case, returncode, log):
    if case["expect"] == "pass":
        return returncode == 0
    # A marker appearing only in a command line, warning or filename is not a
    # validation failure. Require it on an actual compiler error line.
    return returncode != 0 and any(
        case["marker"] in line and re.search(r"\berror(?:\s+[A-Z]+\d+)?\s*:", line, re.IGNORECASE)
        for line in log.splitlines()
    )


def validate_cases(manifest):
    if manifest.get("format") != "step-catalog-compile-cases.v1":
        raise ValueError("Unknown compile case manifest")
    cases = manifest["cases"]
    if not cases:
        raise ValueError("An empty compile suite cannot establish acceptance")
    names = [case["name"] for case in cases]
    if len(set(names)) != len(names) or any(not re.fullmatch(r"[a-z][a-z0-9_]*", name) for name in names):
        raise ValueError("Compile case names must be unique safe target names")
    by_name = {case["name"]: case for case in cases}
    for case in cases:
        if case["expect"] not in ("pass", "fail"):
            raise ValueError("Unknown compile expectation")
        if case["expect"] == "fail":
            control = by_name.get(case.get("positive_control"))
            if not case.get("marker") or not control or control["expect"] != "pass":
                raise ValueError("Every rejection requires a marker and a passing positive control")
    return cases


def run(args):
    manifest = loads(args.manifest.read_text(encoding="utf-8"))
    cases = validate_cases(manifest)
    args.build_dir.mkdir(parents=True, exist_ok=True)
    report = {"format": "step-catalog-compile-results.v1", "scope": manifest["scope"],
              "configured": False, "accepted": False, "cases": []}
    # Invalidate an old receipt before configuring, so a failed new run cannot
    # leave a previous successful report looking current.
    (args.build_dir / "results.json").write_bytes(encoded(report))
    command = ["cmake", "-S", str(DEFAULT_MANIFEST.parent), "-B", str(args.build_dir),
               f"-DCASE_MANIFEST={args.manifest.resolve()}", f"-DCATALOG_REPOSITORY={ROOT}"]
    if args.generator:
        command += ["-G", args.generator]
    if args.architecture:
        command += ["-A", args.architecture]
    configured = subprocess.run(command, capture_output=True, text=True, errors="replace")
    (args.build_dir / "configure.log").write_text(configured.stdout + configured.stderr, encoding="utf-8")
    if configured.returncode:
        raise ValueError("Compiler-case configure failed; inspect configure.log")
    results = []
    for case in cases:
        completed = subprocess.run(["cmake", "--build", str(args.build_dir), "--config", args.config,
                                    "--target", case["name"]], capture_output=True, text=True, errors="replace")
        log = completed.stdout + completed.stderr
        (args.build_dir / f"{case['name']}.log").write_text(log, encoding="utf-8")
        results.append({"name": case["name"], "expected": case["expect"], "exit_code": completed.returncode,
                        "accepted": assess(case, completed.returncode, log)})
    by_name = {result["name"]: result for result in results}
    for case, result in zip(cases, results):
        if case["expect"] == "fail" and not by_name[case["positive_control"]]["accepted"]:
            result["accepted"] = False
    report.update(configured=True, accepted=all(result["accepted"] for result in results), cases=results)
    (args.build_dir / "results.json").write_bytes(encoded(report))
    for result in results:
        print(f"{result['name']}: expected={result['expected']}, accepted={result['accepted']}")
    return 0 if report["accepted"] else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--generator")
    parser.add_argument("--architecture")
    args = parser.parse_args(argv)
    try:
        return run(args)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"compile contracts: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
