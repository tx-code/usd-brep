# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Capture immutable refactoring baselines and compare every ordered catalog fact."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PROFILES = ("Ap203e2Planar", "Ap203e2GeneralSource", "Ap214e3Source", "Ap214MbxSource", "Ap203N916Source")
PROFILE_KEYS = {
    "kind", "profile", "schema_sha256", "membership_capacity", "registered_types", "name_index", "declarations",
    "reference_selects", "enumerations", "complex_signatures", "measure_branches", "derived_slots",
    "exclusive_groups", "si_prefixes",
}


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def _constant(value):
    raise ValueError(f"Non-finite JSON value: {value}")


def loads(text):
    return json.loads(text, object_pairs_hook=_object, parse_constant=_constant)


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def differences(expected, actual, path="$", limit=12):
    """Object key order is irrelevant; array order and JSON value types are not."""
    found = []

    def visit(left, right, location):
        if len(found) >= limit:
            return
        if type(left) is not type(right):
            found.append(f"{location}: type {type(left).__name__} -> {type(right).__name__}")
        elif isinstance(left, dict):
            for key in sorted(left.keys() | right.keys()):
                if key not in right:
                    found.append(f"{location}.{key}: missing")
                elif key not in left:
                    found.append(f"{location}.{key}: unexpected")
                else:
                    visit(left[key], right[key], f"{location}.{key}")
                if len(found) >= limit:
                    break
        elif isinstance(left, list):
            if len(left) != len(right):
                found.append(f"{location}: length {len(left)} -> {len(right)}")
            for index, (a, b) in enumerate(zip(left, right)):
                visit(a, b, f"{location}[{index}]")
        elif left != right:
            found.append(f"{location}: {left!r} -> {right!r}")

    visit(expected, actual, path)
    return found[:limit]


def validate_catalog(value):
    if not isinstance(value, dict) or set(value) != {"format", "profiles"}:
        raise ValueError("Snapshot envelope is incomplete")
    profiles = {"step-catalog-snapshot.v1": PROFILES[:4], "step-catalog-snapshot.v2": PROFILES}.get(value["format"])
    if profiles is None:
        raise ValueError("Unknown snapshot format")
    if tuple(p.get("kind") for p in value["profiles"]) != profiles:
        raise ValueError("Expected every profile for the declared snapshot format, in its fixed order")
    for profile in value["profiles"]:
        if set(profile) != PROFILE_KEYS:
            raise ValueError(f"Incomplete profile sections: {profile['kind']}")
        declarations = profile["declarations"]
        if profile["registered_types"] != [d["name"] for d in declarations]:
            raise ValueError("Registered type order differs from declarations")
        if [d["id"] for d in declarations] != list(range(len(declarations))):
            raise ValueError("Declaration IDs are not contiguous")
        for declaration in declarations:
            if set(declaration) != {"id", "name", "parents", "simple", "fields", "ancestors", "flattened"}:
                raise ValueError("Incomplete declaration sections")
            for field in declaration["fields"]:
                if set(field) != {"name", "type", "kind", "optional", "element_kind", "minimum", "maximum",
                                  "unique", "nested_aggregates"}:
                    raise ValueError("Incomplete local field attributes")


def execute(binary, *args):
    result = subprocess.run([str(binary), *args], capture_output=True, check=False)
    if result.returncode or result.stderr:
        raise ValueError(f"{binary.name} failed ({result.returncode}): {result.stderr.decode('utf-8', 'replace')}")
    return loads(result.stdout.decode("utf-8"))


def read_baseline(directory):
    manifest = loads((directory / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("format") != "step-catalog-baseline.v1":
        raise ValueError("Unknown baseline manifest")
    expected_names = {"catalog.json"}
    if set(manifest["payload_sha256"]) != expected_names:
        raise ValueError("Baseline payload roles are incomplete")
    for name, digest in manifest["payload_sha256"].items():
        if sha(directory / name) != digest:
            raise ValueError(f"Baseline hash mismatch: {name}")
    catalog = loads((directory / "catalog.json").read_text(encoding="utf-8"))
    validate_catalog(catalog)
    return catalog, manifest


def source_fingerprints():
    source = ROOT / "source/STEP_SEMANTICS/src"
    files = sorted([*source.glob("*Catalog*.h"), *source.glob("*Catalog*.cpp")])
    files += [ROOT / p for p in ("source/STEP_SEMANTICS/inc/StepSemantics.h", "source/STEP_SOURCE/inc/StepSource.h",
                                "source/STEP_SOURCE/inc/StepSourceReport.h", "source/STEP_SOURCE/src/StepSource.cpp",
                                "source/STEP_SOURCE/src/StepSourceReport.cpp")]
    files += sorted((ROOT / "source/STEP_SEMANTICS/tests/catalog").glob("*.cpp"))
    files += [ROOT / "source/STEP_SEMANTICS/CMakeLists.txt"]
    files += sorted(Path(__file__).parent.glob("*.py"))
    return {p.relative_to(ROOT).as_posix(): sha(p) for p in files}


def capture(args):
    output = args.output.resolve()
    if output.exists():
        raise ValueError(f"Refusing to overwrite baseline: {output}")
    before = source_fingerprints()
    command = ["cmake", "--build", str(args.build_dir.resolve()), "--config", args.config,
               "--target", "step_catalog_snapshot", "step_catalog_allocations", "--parallel", "4"]
    subprocess.run(command, check=True)
    build_info = args.build_info.read_text(encoding="utf-8")
    info = dict(line.split("=", 1) for line in build_info.splitlines() if "=" in line)
    if not args.build_info.resolve().is_relative_to(args.build_dir.resolve()):
        raise ValueError("Build identity must come from the selected build directory")
    for role, supplied in (("snapshot", args.snapshot), ("allocations", args.allocations)):
        if Path(info.get(role, "")).resolve() != supplied.resolve():
            raise ValueError(f"{role} binary does not match the configured target")
    if info.get("configuration") != args.config:
        raise ValueError("Build identity configuration mismatch")
    catalog = execute(args.snapshot)
    validate_catalog(catalog)
    if differences(catalog, execute(args.snapshot)):
        raise ValueError("Snapshot is not deterministic")
    rounds = [[execute(args.allocations, str(i)) for i in range(len(PROFILES))] for _ in range(2)]
    if rounds[0] != rounds[1] or tuple(row["profile"] for row in rounds[0]) != PROFILES:
        raise ValueError("Allocation rounds/profile coverage differ")
    for row in rounds[0]:
        if row["allocation_controls"] != {"paths": 8, "allocations": 8, "requested_bytes": 136}:
            raise ValueError("Allocation counter positive controls failed")
    if source_fingerprints() != before:
        raise ValueError("Direct product sources changed during build/capture")
    payloads = {"catalog.json": encoded(catalog)}
    manifest = {
        "format": "step-catalog-baseline.v1",
        "scope": "Refactoring equivalence only; not an independent schema witness or import qualification.",
        "payload_sha256": {name: hashlib.sha256(data).hexdigest() for name, data in payloads.items()},
    }
    # Commit only deterministic comparison inputs. Machine/build identities,
    # source inventories and per-run allocation observations stay in the build
    # directory, as required by the repository's published design convention.
    observations = {
        "format": "step-catalog-capture-observations.v1",
        "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "platform": platform.platform(), "configuration": args.config, "build_command": command,
        "build_info": build_info,
        "binary_sha256": {"snapshot": sha(args.snapshot), "allocations": sha(args.allocations)},
        "source_sha256": before,
        "payload_sha256": manifest["payload_sha256"],
        "allocation_rounds": rounds,
    }
    descriptor, receipt_name = tempfile.mkstemp(prefix="catalog-capture-", suffix=".json", dir=args.build_dir)
    with os.fdopen(descriptor, "wb") as receipt:
        receipt.write(encoded(observations))
    output.parent.mkdir(parents=True, exist_ok=True)
    # Publish only a complete new directory. Never update expected data during a
    # check; replacing a sealed baseline requires an explicitly new capture path.
    staging = Path(tempfile.mkdtemp(prefix=".catalog-capture-", dir=output.parent)).resolve()
    try:
        for name, data in payloads.items():
            (staging / name).write_bytes(data)
        (staging / "manifest.json").write_bytes(encoded(manifest))
        read_baseline(staging)
        if output.exists():
            raise ValueError(f"Baseline appeared during capture: {output}")
        staging.rename(output)
    finally:
        if staging.exists():
            # Only remove the fresh staging directory created above, never output.
            assert staging.parent == output.parent and staging.name.startswith(".catalog-capture-")
            shutil.rmtree(staging)
    print(f"Captured all {len(PROFILES)} full catalogs: {output}")
    print(f"Local build/allocation observations: {receipt_name}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("check")
    check.add_argument("--snapshot", type=Path, required=True)
    check.add_argument("--baseline", type=Path, required=True)
    save = commands.add_parser("capture")
    save.add_argument("--snapshot", type=Path, required=True)
    save.add_argument("--allocations", type=Path, required=True)
    save.add_argument("--build-dir", type=Path, required=True)
    save.add_argument("--build-info", type=Path, required=True)
    save.add_argument("--config", default="Release")
    save.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "capture":
            capture(args)
        else:
            expected, _ = read_baseline(args.baseline)
            actual = execute(args.snapshot)
            validate_catalog(actual)
            changes = differences(expected, actual)
            if changes:
                raise ValueError("Catalog differs:\n" + "\n".join(changes))
            print(f"All {len(actual['profiles'])} full catalogs match the sealed baseline")
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(f"catalog validation: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
