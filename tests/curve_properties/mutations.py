#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Compile three real kernel mutations in an isolated committed-source snapshot."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

import explore

MUTATIONS = (
    {"name": "outside_knots", "path": "source/SMLib/src/SmCurve.cpp", "expected": "analytic_bounds",
     "old": "if (crInterval.ContainsValue(sKnots[i]) &&\n"
            "              (i==sKnots.GetSize()-1 || sContinuities[i] <= SM_CT_C1))",
     "new": "if (i==sKnots.GetSize()-1 || sContinuities[i] <= SM_CT_C1)"},
    {"name": "reverse_interval", "path": "source/SMLib/src/SmBSplineCurve.cpp", "expected": "reverse_interval",
     "old": "double dTNewMin = 1.0 - (crOldInterval.GetMax()",
     "new": "double dTNewMin = 1.0 - (crOldInterval.GetMin()"},
    {"name": "omit_second_derivative", "path": "source/SMLib/src/SmBSplineCurve.cpp", "expected": "bernstein_d2",
     "old": "  // all done\n  return SM_SUCCESS;\n\n} // end SmBSplineCurve::Evaluate\n",
     "new": "  // Deliberate test mutation: discard the requested second derivative.\n"
            "  if (lNumDerivatives >= 2) aPointAndDerivatives[2].Set(0, 0, 0);\n"
            "  return SM_SUCCESS;\n\n} // end SmBSplineCurve::Evaluate\n"},
)


def replace_once(data, old, new):
    if data.count(old) == 0 and b"\n" in old and b"\r\n" not in old:
        old = old.replace(b"\n", b"\r\n")
        new = new.replace(b"\n", b"\r\n")
    if data.count(old) != 1:
        raise ValueError("mutation anchor must match exactly once; review the changed production source")
    return data.replace(old, new, 1)


def is_detection(expected, returncode, failure):
    return returncode == 1 and failure.get("property") == expected


def execute(command, log, expected=0):
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT, timeout=900)
    if expected is not None and result.returncode != expected:
        raise ValueError(f"command exited {result.returncode}; expected {expected}; see {log}")
    return result.returncode


def source_manifest(source):
    return {str(path.relative_to(source)).replace("\\", "/"): explore.digest(path)
            for path in sorted(source.rglob("*")) if path.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="new isolated experiment directory")
    parser.add_argument("--revision", default="HEAD", help="committed source, never working changes")
    parser.add_argument("--config", choices=["Release", "Debug"], default="Release")
    parser.add_argument("--parallel", type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.parallel <= 16:
        parser.error("--parallel must be 1..16")
    root = Path(__file__).resolve().parents[2]
    revision = subprocess.run(["git", "rev-parse", f"{args.revision}^{{commit}}"], cwd=root,
                              capture_output=True, text=True, check=True).stdout.strip()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source, build = output / "source", output / "build"
    source.mkdir()
    archive = output / "committed-source.tar"
    with archive.open("wb") as stream:
        subprocess.run(["git", "archive", revision, "source/SMLib", "tests/curve_properties"],
                       cwd=root, stdout=stream, check=True)
    # Extract only ordinary files/directories under these two tracked paths.
    # Never move/delete the caller's checkout, reuse objects, or restore mtimes.
    with tarfile.open(archive) as bundle:
        for member in bundle:
            relative = Path(member.name)
            if relative.is_absolute() or ".." in relative.parts or not (member.isdir() or member.isfile()):
                raise ValueError("unsafe source archive member")
            target = source / relative
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(bundle.extractfile(member).read())
    manifest = source_manifest(source)
    explore.write(output / "source-manifest.json", manifest)
    originals = {item["path"]: (source / item["path"]).read_bytes() for item in MUTATIONS}
    # Fail before compiling if a source update invalidated any of the three anchors.
    for item in MUTATIONS:
        replace_once(originals[item["path"]], item["old"].encode(), item["new"].encode())
    configure = ["cmake", "-S", source / "tests/curve_properties", "-B", build,
                 "-DCURVE_HARNESS_BUILD_KERNEL=ON", f"-DPython3_EXECUTABLE={sys.executable}"]
    compile_command = ["cmake", "--build", build, "--config", args.config,
                       "--target", "curve_property_probe", "--parallel", args.parallel]
    summary = {"status": "running", "revision": revision, "config": args.config,
               "configure": list(map(str, configure)), "build": list(map(str, compile_command)), "mutants": []}
    explore.write(output / "summary.json", summary)
    print("Building isolated unmodified kernel", flush=True)
    execute(configure, output / "configure.log")
    execute(compile_command, output / "baseline-build.log")
    candidates = list(build.rglob("curve_property_probe.exe" if sys.platform == "win32" else "curve_property_probe"))
    candidates = [p for p in candidates if p.is_file() and (sys.platform != "win32" or p.parent.name == args.config)]
    if len(candidates) != 1:
        raise ValueError("ambiguous/missing built probe")
    probe = candidates[0]
    harness = source / "tests/curve_properties/harness.py"
    explorer = source / "tests/curve_properties/explore.py"
    summary["driver_sha256"] = explore.digest(Path(__file__))
    execute([sys.executable, "-B", harness, "check", "--probe", probe], output / "baseline-check.log")
    if source_manifest(source) != manifest:
        raise ValueError("source changed during baseline compilation")
    baseline_runtime = output / "baseline-runtime"
    baseline_runtime.mkdir()
    baseline_identity = explore.identity(probe)
    for name in baseline_identity:
        shutil.copyfile(name, baseline_runtime / Path(name).name)
    baseline_probe = baseline_runtime / probe.name
    summary["baseline_binaries"] = explore.identity(baseline_probe)
    explore.write(output / "summary.json", summary)
    try:
        for item in MUTATIONS:
            experiment = output / item["name"]
            experiment.mkdir()
            print(f"Compiling and checking mutation: {item['name']}", flush=True)
            path = source / item["path"]
            mutated = replace_once(originals[item["path"]], item["old"].encode(), item["new"].encode())
            path.write_bytes(mutated)
            path.touch()
            try:
                execute(compile_command, experiment / "build.log")
                mutated_identity = explore.identity(probe)
                if mutated_identity == baseline_identity:
                    raise ValueError("mutation did not change the native binary")
                # Attest exactly one changed production file; no test/oracle changes.
                modified_manifest = source_manifest(source)
                changed = [name for name in manifest if manifest[name] != modified_manifest.get(name)]
                if changed != [item["path"]] or set(manifest) != set(modified_manifest):
                    raise ValueError("mutation scope escaped its single production file")
                explore.write(experiment / "mutation.json", {**item, "sha256": hashlib.sha256(mutated).hexdigest(),
                                                            "binaries": mutated_identity})
                result = execute([sys.executable, "-B", explorer, "run", "--probe", probe, "--cases", 8,
                                  "--output", experiment / "discovery"], experiment / "discovery.log", expected=None)
                discovery = json.loads((experiment / "discovery/summary.json").read_text(encoding="utf-8"))
                if result == 0:
                    raise ValueError(f"mutation {item['name']} survived the property tests")
                if discovery["status"] != "fail":
                    raise ValueError(f"mutation {item['name']} discovery ended as {discovery['status']}")
                failure = json.loads((experiment / "discovery/failure.json").read_text(encoding="utf-8"))
                if not is_detection(item["expected"], result, failure):
                    raise ValueError(f"mutation {item['name']} was not caught by its intended geometry property")
                reduced_path = experiment / "discovery/minimized.json"
                reduced = json.loads(reduced_path.read_text(encoding="utf-8"))
                if (not reduced["reproduced"]
                        or explore.complexity(reduced["case"]) >= explore.complexity(failure["case"])):
                    raise ValueError("reduction did not preserve the failure while reducing its input")
                execute([sys.executable, "-B", explorer, "replay", "--probe", probe, "--input", reduced_path,
                         "--output", experiment / "mutant-replay"], experiment / "mutant-replay.log", expected=1)
                replay = json.loads((experiment / "mutant-replay/failure.json").read_text(encoding="utf-8"))
                if replay["property"] != failure["property"]:
                    raise ValueError("replay changed failure property")
                execute([sys.executable, "-B", explorer, "replay", "--probe", baseline_probe, "--input", reduced_path,
                         "--output", experiment / "baseline-replay"], experiment / "baseline-replay.log")
                summary["mutants"].append({"name": item["name"], "outcome": "killed", "property": failure["property"],
                                           "original_complexity": explore.complexity(failure["case"]),
                                           "reduced_complexity": explore.complexity(reduced["case"]),
                                           "reduction_attempts": reduced["attempts"]})
                explore.write(output / "summary.json", summary)
            finally:
                # Restore only this disposable snapshot's exact original bytes.
                path.write_bytes(originals[item["path"]])
                path.touch()
    finally:
        for name, data in originals.items():
            (source / name).write_bytes(data)
        if source_manifest(source) != manifest:
            raise ValueError("source restoration failed")
    print("Rebuilding restored kernel and replaying fixed properties", flush=True)
    execute(compile_command, output / "restored-build.log")
    execute([sys.executable, "-B", harness, "check", "--probe", probe], output / "restored-check.log")
    summary["status"] = "pass"
    summary["restored_binaries"] = explore.identity(probe)
    explore.write(output / "summary.json", summary)
    print(json.dumps({"status": summary["status"], "killed": len(summary["mutants"]), "selected": len(MUTATIONS)}))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(json.dumps({"status": "experiment_incomplete", "detail": str(error)}), file=sys.stderr)
        sys.exit(2)
