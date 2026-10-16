# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Deterministic corpus materialization and fail-closed material verification."""

from __future__ import annotations

import base64
from fractions import Fraction
from pathlib import Path

from . import FORMAT
from .common import (
    InvalidEvidence, checked_bytes, digest, file_ref, inside, load_json, parse_json, tool_identity, write_json,
)
from .fixtures import PROFILE, Q_ID, all_cases
from .oracle import decode, exact, reference_truth, verify_input_definition
from .records import inventory, ledger, nesting_depth
from .reference import anchors

SOURCE_DOCUMENTS = [
    "docs/step-import-repair-spec.md", "docs/step-import/ap203e2-planar-profile.md",
    "docs/step-import/ap203e2-planar-global-rules.md", "docs/step-import/ap203e2-planar-context-closure.md",
    "docs/step-import/ap203e2-planar-geometry-closure.md", "docs/step-import/ap203e2-sample-design.md",
    "docs/step-import/ap203e2-sample-plane.md", "docs/step-import/ap203e2-sample-box.md",
    "docs/step-import/ap203e2-sample-cases.md", "docs/step-import/p0-planar-cases.md",
]
CASE_MATERIALS = {"input", "definition", "inventory", "truth", "assertions"}


def quality_contract() -> dict:
    return {"format_version": FORMAT, "id": Q_ID, "production_default": False, "canonical_length": "mm",
            "definition_error_mm": exact(Fraction(1, 100_000_000)),
            "consistency_error_mm": exact(Fraction(1, 10_000_000)),
            "derived_uv_error_mm": exact(Fraction(1, 10_000_000)),
            "initial_working_tolerance_mm": exact(Fraction(1, 100_000)),
            "max_working_tolerance_mm": exact(Fraction(1, 10_000)),
            "aux_length_error_mm": exact(Fraction(1, 10_000_000)),
            "aux_area_error_mm2": exact(Fraction(1, 1_000_000)),
            "aux_volume_error_mm3": exact(Fraction(1, 100_000)),
            "clean_growth_mm": exact(0), "repair": "None"}


def _json_material(root: Path, relative: str, role: str, value: object) -> dict:
    write_json(inside(root, relative), value)
    return file_ref(root, relative, role)


def build(repo: Path, output: Path) -> dict:
    output = output.resolve()
    if output.exists():
        raise InvalidEvidence("Build.FrozenOutputAlreadyExists", str(output))
    cases, recipes = all_cases(repo)
    reference = anchors(repo)
    output.mkdir(parents=True)
    materials, entries = [], []
    ref_ref = _json_material(output, "reference/anchors.json", "reference", {
        "format_version": FORMAT, "source": "independently transcribed design anchors; no CAD measurements",
        "cases": reference,
    })
    materials.append(ref_ref)
    sources = {"format_version": FORMAT, "documents": [file_ref(repo, path, "design") for path in SOURCE_DOCUMENTS],
               "n2635": {"sha256": "c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088",
                           "uri": "https://www.mbx-if.org/home/wp-content/uploads/2024/07/part403ts_wg3n2635mim_lf.zip",
                           "classification": "publisher implementation release; ISO byte identity unverified"},
               "generator": tool_identity(), "reference_sha256": ref_ref["sha256"]}
    materials.append(_json_material(output, "sources.lock.json", "sources", sources))
    q = quality_contract()
    materials.append(_json_material(output, f"quality/{Q_ID}.json", "quality", q))
    limits = {"format_version": FORMAT, "max_case_bytes": 2 * max(map(len, cases.values())),
              "max_total_input_bytes": 2 * sum(map(len, cases.values())), "max_records": 1000,
              "max_report_bytes": 10_000_000, "max_case_depth": 16, "production_default": False}
    limits["measured"] = {
        "max_input_bytes": max(map(len, cases.values())), "total_input_bytes": sum(map(len, cases.values())),
        "max_record_occurrences": max(len(inventory(data)) for data in cases.values()),
        "max_record_nesting": max(nesting_depth(r.raw) for data in cases.values() for r in inventory(data)),
        "nesting_definition": "parenthesis nesting within DATA record value, excluding quoted text",
    }
    materials.append(_json_material(output, "resources/T01-corpus-limits.json", "resources", limits))
    for case_id, data in cases.items():
        directory = f"cases/{case_id}"
        inside(output, f"{directory}/input.step").parent.mkdir(parents=True)
        inside(output, f"{directory}/input.step").write_bytes(data)
        input_ref = file_ref(output, f"{directory}/input.step", "input")
        refs = {"input": input_ref}
        recipe = recipes[case_id]
        definition = {"format_version": FORMAT, "case_id": case_id, **recipe, "input_sha256": digest(data)}
        if recipe["parent"]:
            definition["parent_input_sha256"] = digest(cases[recipe["parent"]])
        if reference[case_id]["kind"] == "Negative":
            for edit in definition["edits"]:
                edit["old_bytes_b64"] = base64.b64encode(edit["old_text"].encode("ascii")).decode("ascii")
                edit["new_bytes_b64"] = base64.b64encode(edit["new_text"].encode("ascii")).decode("ascii")
        refs["definition"] = _json_material(
            output, f"{directory}/{'mutation' if case_id[5] == 'N' else 'recipe'}.json", "definition", definition)
        records = inventory(data)
        refs["inventory"] = _json_material(output, f"{directory}/inventory.json", "inventory", {
            "format_version": FORMAT, "data_record_occurrences": len(records),
            "unique_entity_ids": len({r.entity_id for r in records}), "records": ledger(records)})
        refs["truth"] = _json_material(output, f"{directory}/truth.json", "truth",
                                      reference_truth(case_id, reference[case_id]))
        assertion = {"format_version": FORMAT, "case_id": case_id,
                     "expected_result": "Rejected" if case_id[5] == "N" else "Accepted",
                     "model_present": case_id[5] == "P", "repair": "None",
                     "expected_stage": recipe.get("stage"), "diagnostic_key": recipe.get("diagnostic"),
                     "qualification": "future product execution, not material preparation",
                     "mutation_source_span": ([recipe["edits"][0]["start"],
                                                recipe["edits"][0]["start"] +
                                                len(recipe["edits"][0]["new_text"].encode("ascii"))]
                                              if case_id[5] == "N" else None)}
        refs["assertions"] = _json_material(output, f"{directory}/assertions.json", "assertions", assertion)
        entry = {"format_version": FORMAT, "case_id": case_id, "kind": reference[case_id]["kind"],
                 "profile_id": PROFILE, "quality_id": Q_ID, "repair": "None", "selected_definition_id": 7,
                 "parent": recipe["parent"], "materials": refs}
        case_ref = _json_material(output, f"{directory}/case.json", "case", entry)
        materials.extend(refs.values())
        materials.append(case_ref)
        entries.append({"case_id": case_id, "kind": entry["kind"], "parent": entry["parent"], "case_ref": case_ref})
    for name, value in {
        "reference-review": {"method": "design-to-anchor transcription; source hashes in sources.lock",
                             "reviewer": "codex", "human_review_claimed": False},
        "generation-review": {"method": "fixed authored documents and deterministic literal edits",
                              "qualification": "Generated; actual verification is recorded in run evidence"},
        "oracle-review": {"method": "exact rational geometry, reviewed constants and distinct formulae",
                          "cad_reader_or_kernel_used": False},
        "coverage": {"positive_definitions": 13, "negative_definitions": 36,
                     "product_execution": "NotRun", "import_success_count": None},
    }.items():
        materials.append(_json_material(output, f"review/{name}.json", "review", {"format_version": FORMAT, **value}))
    manifest = {"format_version": FORMAT, "corpus_id": "ap203e2-planar", "corpus_revision": "v1",
                "profile_id": PROFILE, "reference": ref_ref, "cases": entries,
                "materials": sorted(materials, key=lambda item: item["path"])}
    write_json(output / "manifest.json", manifest)
    return {"manifest_sha256": digest((output / "manifest.json").read_bytes()), "cases": len(entries)}


def material_map(root: Path, manifest: dict) -> dict[str, dict]:
    refs = {}
    for item in manifest["materials"]:
        if item["path"] in refs:
            raise InvalidEvidence("Contract.DuplicateMaterialPath", item["path"])
        inside(root, item["path"])
        refs[item["path"]] = item
    return refs


def read_case(root: Path, manifest: dict, case_id: str) -> dict:
    matching = [case for case in manifest["cases"] if case["case_id"] == case_id]
    if len(matching) != 1:
        raise InvalidEvidence("Contract.CaseSelectionInvalid", case_id)
    return parse_json(checked_bytes(root, matching[0]["case_ref"]))


def verify(root: Path, trusted_root: Path | None = None, selected: list[str] | None = None) -> dict:
    manifest = load_json(root / "manifest.json")
    if manifest.get("format_version") != FORMAT:
        raise InvalidEvidence("Contract.FormatVersionUnsupported", manifest.get("format_version"))
    trusted_root = trusted_root or root
    trusted_manifest = load_json(trusted_root / "manifest.json")
    reference = parse_json(checked_bytes(trusted_root, trusted_manifest["reference"]))["cases"]
    expected_ids = sorted(reference)
    ids = [case["case_id"] for case in manifest["cases"]]
    if sorted(ids) != expected_ids or len(ids) != 49:
        raise InvalidEvidence("Contract.CaseCoverageMismatch", ids)
    refs = material_map(root, manifest)
    if manifest.get("profile_id") != PROFILE:
        raise InvalidEvidence("Contract.ProfileConflict", manifest.get("profile_id"))
    if refs.get(manifest["reference"]["path"]) != manifest["reference"]:
        raise InvalidEvidence("Contract.ArtifactReferenceConflict", "reference")
    for entry in manifest["cases"]:
        if refs.get(entry["case_ref"]["path"]) != entry["case_ref"]:
            raise InvalidEvidence("Contract.ArtifactReferenceConflict", entry["case_id"])
    total_input = 0
    for item in refs.values():
        payload = checked_bytes(root, item, 10_000_000)
        if item["role"] == "input":
            total_input += len(payload)
    limits_ref = next(item for item in refs.values() if item["role"] == "resources")
    limits = parse_json(checked_bytes(root, limits_ref))
    quality_refs = [item for item in refs.values() if item["role"] == "quality"]
    if len(quality_refs) != 1 or parse_json(checked_bytes(root, quality_refs[0])) != quality_contract():
        raise InvalidEvidence("Contract.QualityConflict", Q_ID)
    for name in ("max_case_bytes", "max_total_input_bytes", "max_records", "max_report_bytes", "max_case_depth"):
        if type(limits.get(name)) is not int or not 0 < limits[name] <= 10_000_000:
            raise InvalidEvidence("Contract.ResourceLimitInvalid", name)
    if total_input > limits["max_total_input_bytes"]:
        raise InvalidEvidence("Preparation.TotalByteLimit", total_input)
    chosen = sorted(ids) if selected is None else selected
    if not chosen or len(chosen) != len(set(chosen)) or not set(chosen) <= set(ids):
        raise InvalidEvidence("Contract.CaseSelectionInvalid", chosen)
    # Validate prerequisites as well, but never count unselected parents as requested results.
    ordered = []

    def dependency(case_id):
        if case_id in ordered:
            return
        parent = reference[case_id]["parent"]
        if parent:
            dependency(parent)
        ordered.append(case_id)

    for case_id in chosen:
        dependency(case_id)
    results = []
    for case_id in ordered:
        case = read_case(root, manifest, case_id)
        anchor = reference[case_id]
        expected_fields = {"format_version": FORMAT, "case_id": case_id, "kind": anchor["kind"],
                           "profile_id": PROFILE, "quality_id": Q_ID, "repair": "None", "selected_definition_id": 7}
        if any(case.get(key) != value for key, value in expected_fields.items()):
            raise InvalidEvidence("Contract.CaseIdentityConflict", case_id)
        if case.get("parent") != anchor["parent"]:
            raise InvalidEvidence("Preparation.ParentContractMismatch", case_id)
        if set(case["materials"]) != CASE_MATERIALS:
            raise InvalidEvidence("Evidence.RequiredArtifactMissing", case_id)
        for role, item in case["materials"].items():
            if refs.get(item["path"]) != item or item["role"] != role:
                raise InvalidEvidence("Contract.ArtifactReferenceConflict", [case_id, role])
        data = checked_bytes(root, case["materials"]["input"], limits["max_case_bytes"])
        definition = parse_json(checked_bytes(root, case["materials"]["definition"]))
        if definition.get("case_id") != case_id or definition.get("format_version") != FORMAT:
            raise InvalidEvidence("Contract.CaseIdentityConflict", case_id)
        if definition["parent"] != anchor["parent"] or definition["input_sha256"] != digest(data):
            raise InvalidEvidence("Preparation.ParentContractMismatch", case_id)
        if anchor["parent"]:
            parent = read_case(root, manifest, anchor["parent"])
            parent_data = checked_bytes(root, parent["materials"]["input"])
            if definition["parent_input_sha256"] != digest(parent_data):
                raise InvalidEvidence("Preparation.ParentContractMismatch", case_id)
            evolving = parent_data
            for edit in definition["edits"]:
                start, end = edit["start"], edit["end"]
                old, new = edit["old_text"].encode("ascii"), edit["new_text"].encode("ascii")
                if not 0 <= start <= end <= len(evolving) or evolving[start:end] != old:
                    raise InvalidEvidence("Preparation.MutationPreimageMismatch", case_id)
                evolving = evolving[:start] + new + evolving[end:]
            if evolving != data:
                raise InvalidEvidence("Preparation.UnregisteredMutation", case_id)
            if anchor["kind"] == "Negative":
                if len(definition["edits"]) != 1:
                    raise InvalidEvidence("Preparation.MutationCardinality", case_id)
                edit = definition["edits"][0]
                if [edit["old_text"], edit["new_text"]] != anchor["mutation_tokens"]:
                    raise InvalidEvidence("Preparation.MutationContractMismatch", case_id)
                for label in ("old", "new"):
                    decoded_bytes = base64.b64decode(edit[f"{label}_bytes_b64"], validate=True)
                    if decoded_bytes != edit[f"{label}_text"].encode("ascii"):
                        raise InvalidEvidence("Preparation.MutationByteEncodingMismatch", case_id)
        records = inventory(data, limits["max_records"], limits["max_case_depth"])
        actual_inventory = {"format_version": FORMAT, "data_record_occurrences": len(records),
                            "unique_entity_ids": len({r.entity_id for r in records}), "records": ledger(records)}
        claimed_inventory = parse_json(checked_bytes(root, case["materials"]["inventory"]))
        if actual_inventory != claimed_inventory:
            raise InvalidEvidence("Preparation.OccurrenceInventoryMismatch", {
                "case_id": case_id, "actual": len(records),
                "claimed": claimed_inventory.get("data_record_occurrences"),
                "duplicate_occurrences": [r.__dict__ for r in records if r.entity_id == 100],
            })
        input_proof = verify_input_definition(case_id, data, anchor)
        truth = parse_json(checked_bytes(root, case["materials"]["truth"]))
        expected_truth = reference_truth(case_id, anchor)
        if anchor["kind"] == "Positive":
            for metric in truth["measures"].values():
                decode(metric)
            if truth["material_volume"] != expected_truth["material_volume"]:
                raise InvalidEvidence("Contract.ApplicabilityConflict", case_id)
        if truth != expected_truth:
            raise InvalidEvidence("Oracle.ExpectedValueConflict", {"case_id": case_id, "expected": expected_truth})
        assertions = parse_json(checked_bytes(root, case["materials"]["assertions"]))
        expected_assertions = {
            "format_version": FORMAT, "case_id": case_id,
            "expected_result": "Accepted" if anchor["kind"] == "Positive" else "Rejected",
            "model_present": anchor["kind"] == "Positive", "repair": "None",
            "expected_stage": anchor.get("expected_stage"), "diagnostic_key": anchor.get("diagnostic_key"),
            "qualification": "future product execution, not material preparation",
            "mutation_source_span": ([definition["edits"][0]["start"], definition["edits"][0]["start"] +
                                      len(definition["edits"][0]["new_text"].encode("ascii"))]
                                     if anchor["kind"] == "Negative" else None),
        }
        if assertions != expected_assertions:
            raise InvalidEvidence("Contract.ExpectedResultConflict", case_id)
        if case_id not in chosen:
            continue
        results.append({"case_id": case_id, "input_sha256": digest(data), "input_bytes": len(data),
                        "identity": {key: case[key] for key in (
                            "profile_id", "quality_id", "repair", "selected_definition_id", "parent", "materials")},
                        "preparation": {"status": "Passed", "data_record_occurrences": len(records),
                                        "unique_entity_ids": actual_inventory["unique_entity_ids"],
                                        "checks": ["material hashes", "parent/edits replay", "occurrence ledger",
                                                   "independent input definition/facts"]},
                        "oracle": {"status": "Passed", "input_proof": input_proof, "truth": expected_truth,
                                   "truth_sha256": case["materials"]["truth"]["sha256"]},
                        "input_snapshot_b64": base64.b64encode(data).decode("ascii"),
                        "expected": assertions,
                        "subject_configuration": "NotConfigured", "subject_execution": "NotRun",
                        "observed": None, "comparison": "NotChecked"})
    results.sort(key=lambda row: chosen.index(row["case_id"]))
    return {"format_version": FORMAT, "manifest_sha256": digest((root / "manifest.json").read_bytes()),
            "trusted_reference_sha256": trusted_manifest["reference"]["sha256"], "selected": chosen,
            "configuration": {"profile_id": PROFILE, "quality": quality_refs[0], "resources": limits_ref,
                              "sources": next(item for item in refs.values() if item["role"] == "sources")},
            "cases": results, "material_preparation_passed": len(results), "oracle_passed": len(results),
            "subject_not_run": len(results), "import_success_count": None}
