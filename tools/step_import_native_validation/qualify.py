# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Fresh, local first-profile run. Existing independent comparators remain authoritative.

This records observations of supplied binaries, not proof that they were built
from the observed checkout. Build/SDK provenance and owning C++ contracts are
separate checks. No historical result is read or reused here.
"""
from collections import Counter
from fractions import Fraction
from pathlib import Path
import subprocess
import sys

from tools.step_import_validation.common import digest, json_bytes, load_json, parse_json, write_json
from tools.step_import_validation.corpus import SOURCE_DOCUMENTS, quality_contract, verify
from tools.step_import_validation.oracle import decode
from tools.step_import_validation.records import by_id, inventory
from tools.step_inspect_validation.oracle import compare as compare_source
from tools.step_semantic_validation.oracle import compare as compare_semantic
from tools.step_planar_validation.oracle import compare as compare_planar, facets
from .oracle import corpus_case, require, vector

REPO = Path(__file__).resolve().parents[2]
CORPUS = REPO / "tools/step_import_validation/corpus/ap203e2-planar/v1"
STAGES = ("source", "semantic", "planar", "native")


def checkout_identity():
    paths = {REPO / name for name in SOURCE_DOCUMENTS}
    for module in ("STEP_SOURCE", "STEP_SEMANTICS", "STEP_PLANAR", "STEP_IMPORT"):
        paths.update(p for p in (REPO / "source" / module).rglob("*") if p.is_file() and
                     (p.suffix in {".cpp", ".h", ".lua"} or p.name in {"CMakeLists.txt", "README.md"}))
    for module in ("step_import_validation", "step_inspect_validation", "step_semantic_validation",
                   "step_planar_validation", "step_import_native_validation"):
        paths.update(p for p in (REPO / "tools" / module).rglob("*")
                     if p.is_file() and p.suffix in {".py", ".json", ".step"})
    return {p.relative_to(REPO).as_posix(): digest(p.read_bytes()) for p in sorted(paths)}


def check_query(observed, expected):
    """Only query capabilities explicitly represented by frozen independent truth."""
    budget = decode(quality_contract()["aux_length_error_mm"])
    vector(observed["point_mm"], list(map(decode, expected["point_mm"])), budget, "query input")
    if "closest_point_mm" in expected:
        vector(observed["closest_point_mm"], list(map(decode, expected["closest_point_mm"])),
               budget, "independent closest point")
        distance = Fraction(observed["distance_mm"])
        require(decode(expected["distance_mm"]["lower"]) - budget <= distance <=
                decode(expected["distance_mm"]["upper"]) + budget, "independent distance interval")
    if "material_location" in expected:
        location = expected["material_location"] or "NotApplicable"
        require(observed["material_location"] == location, "independent material role query")


def combination_facts(positive, negative):
    """Independent fixed-input preconditions: one mutation, and no orphan SI anchor."""
    old = b"#18=LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#10);"
    new = b"#18=LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#19);"
    require(positive.count(old) == 1 and positive.replace(old, new) == negative, "N34 single unit-cycle mutation")
    for data in (positive, negative):
        index = by_id(inventory(data))
        require(index[13].references == (10,), "own-mm uncertainty retains the SI anchor")
        require(index[19].references == (18, 17), "conversion factor and dimension identities")
        context, = [r for r in index.values() if "GLOBAL_UNIT_ASSIGNED_CONTEXT(" in r.raw]
        reachable, pending = set(), [context.entity_id]
        while pending:
            item = pending.pop()
            if item not in reachable:
                reachable.add(item)
                pending.extend(index[item].references)
        units = {r.entity_id for r in index.values() if "NAMED_UNIT" in facets(r)}
        require(units == {10, 11, 19} and units <= reachable,
                "all unit definitions belong to the context dependency closure; no additional orphan unit")
    require(by_id(inventory(negative))[18].references == (19,), "unit cycle is 19 -> 18 -> 19")
    return {"status": "Matched", "cycle": [19, 18, 19], "uncertainty_unit": 10,
            "orphan_unit": False, "mutation": "factor.unit_component: #10 -> #19"}


def qualify(binaries, output):
    require(set(binaries) == set(STAGES), "all four actual executables are required")
    binaries = {key: Path(path).resolve(strict=True) for key, path in binaries.items()}
    output = Path(output).resolve()
    # A fresh directory prevents accidental mixing with any prior run.
    output.mkdir(parents=True, exist_ok=False)
    cases = verify(CORPUS)["cases"]
    expected_ids = {f"S203-P{n:02}" for n in range(1, 14)} | {f"S203-N{n:02}" for n in range(1, 37)}
    require(len(cases) == 49 and {c["case_id"] for c in cases} == expected_ids, "complete frozen coverage")
    sources = checkout_identity()
    identities = {key: {"path": str(path), "sha256": digest(path.read_bytes())} for key, path in binaries.items()}
    runtime_files = [binaries["native"].parent / name for name in ("SMLib.dll", "SM_API.dll")]
    runtimes = {str(p): digest(p.read_bytes()) for p in runtime_files if p.is_file()}
    kernels = {}
    rows, counts = [], Counter()

    def execute(stage, path, directory, label=None, point=None):
        command = [str(binaries[stage]), "--fixture-limits"]
        if stage in {"planar", "native"}:
            command += ["--quality", "Q-P0-mm-01"]
        command.append(str(path))
        if point is not None:
            command += ["--query", *[str(float(decode(v))) for v in point]]
        actual = subprocess.run(command, capture_output=True, timeout=30)
        label = label or stage
        (directory / (label + ".stdout.json")).write_bytes(actual.stdout)
        (directory / (label + ".stderr.log")).write_bytes(actual.stderr)
        receipt = {"command": command, "exit": actual.returncode, "stdout_sha256": digest(actual.stdout),
                   "stderr_sha256": digest(actual.stderr)}
        write_json(directory / (label + ".execution.json"), receipt)
        require(not actual.stderr, "unexpected CLI stderr: " + label)
        return parse_json(actual.stdout), receipt

    for case in cases:
        key = case["case_id"]
        directory = output / key
        directory.mkdir()
        materials = case["identity"]["materials"]
        path = CORPUS / materials["input"]["path"]
        data = path.read_bytes()
        case["truth"] = load_json(CORPUS / materials["truth"]["path"])
        ledger = load_json(CORPUS / materials["inventory"]["path"])
        observed, receipts = {}, {}
        for stage in STAGES:
            observed[stage], receipt = execute(stage, path, directory)
            if stage == "native":
                corpus_case(case, observed[stage], receipt["exit"], data)
            else:
                compare = {"source": compare_source, "semantic": compare_semantic, "planar": compare_planar}[stage]
                compare(case, observed[stage], receipt["exit"], data, ledger)
            receipts[stage] = {**receipt, "comparison": "Matched", "status": observed[stage]["status"]}
        require(observed["semantic"]["source"] == observed["source"] and
                observed["planar"]["source_semantics"] == observed["semantic"], "same captured graph across stages")
        native = observed["native"]
        counts[native["status"]] += 1
        queries = []
        if native["status"] == "Accepted":
            kernel = Path(native["kernel_path"]).resolve(strict=True)
            require(not runtimes or str(kernel) in runtimes, "loaded kernel is outside the selected native runtime")
            kernels.setdefault(str(kernel), digest(kernel.read_bytes()))
            expected_queries = case["truth"]["queries"]
            query_items = [(name, value) for name, value in expected_queries.items()
                           if isinstance(value, dict) and "point_mm" in value]
            query_items += [(f"classification-{i}", value)
                            for i, value in enumerate(expected_queries.get("classifications", []))
                            if "material_location" in value]
            for label, expected in query_items:
                query, receipt = execute("native", path, directory, "query-" + label, expected["point_mm"])
                require(receipt["exit"] == 0 and query["import"] == native,
                        "query result belongs to the complete import")
                check_query(query["query"], expected)
                queries.append({"name": label, **receipt, "comparison": "Matched"})
        rows.append({"case_id": key, "input_sha256": digest(data), "materials": materials,
                     "stages": receipts, "queries": queries, "model": native["model"],
                     "face_uv_classification": "NotExposedByCLI",
                     "diagnostics": native["diagnostics"]})

    combination = combination_facts((CORPUS / "cases/S203-P13/input.step").read_bytes(),
                                    (CORPUS / "cases/S203-N34/input.step").read_bytes())
    require(counts == Counter(Accepted=13, Rejected=29, Unsupported=7), "qualified frozen outcome distribution")
    require(sources == checkout_identity(), "checkout/checker/corpus changed during qualification")
    require(all(digest(binaries[k].read_bytes()) == v["sha256"] for k, v in identities.items()),
            "binary changed during qualification")
    require(all(digest(Path(p).read_bytes()) == sha for p, sha in kernels.items()),
            "loaded kernel changed during run")
    require(all(digest(Path(p).read_bytes()) == sha for p, sha in runtimes.items()),
            "native runtime changed during run")
    report = {"format_version": "step-first-profile-qualification.v1", "status": "Matched",
              "scope": "Frozen49Contracts", "whole_stage_qualification": False,
              "quality": quality_contract(), "python": sys.version, "binaries": identities,
              "loaded_kernels": kernels, "adjacent_runtime_files": runtimes, "checkout_files": sources,
              "checkout_identity_sha256": digest(json_bytes(sources)),
              "build_provenance": "NotEstablishedByThisCommand", "native_outcomes": dict(counts),
              "capability_reference": "source/STEP_IMPORT/README.md#首profile的资格边界",
              "combination": combination, "cases": rows}
    write_json(output / "qualification.json", report)
    return report
