# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Real corruption controls; candidate rejection and control verdict are separate."""

from __future__ import annotations

from copy import deepcopy
from fractions import Fraction
from pathlib import Path
import shutil

from .common import InvalidEvidence, digest, file_ref, json_bytes, load_json, tool_identity, write_json
from .corpus import read_case, verify
from .evidence import check, identifier, page, run, seal
from .oracle import exact
from .records import inventory, ledger

VARIANTS = ["CTL01-A", "CTL02-A", "CTL03-A", "CTL04-A", "CTL04-B", "CTL05-A", "CTL06-A",
            "CTL07-A", "CTL08-A", "CTL09-A", "CTL10-A", "CTL11-A", "CTL12-A", "CTL12-B"]
EXPECTED = {
    "CTL01-A": "Preparation.HashMismatch", "CTL02-A": "Oracle.ExpectedValueConflict",
    "CTL03-A": "Reference.InputDefinitionMismatch", "CTL04-A": None, "CTL04-B": None,
    "CTL05-A": "Preparation.OccurrenceInventoryMismatch", "CTL06-A": "Preparation.ParentContractMismatch",
    "CTL07-A": "Evidence.StateContradiction", "CTL08-A": "Evidence.RequiredArtifactMissing",
    "CTL09-A": "Evidence.CaseCoverageMismatch", "CTL10-A": "Contract.ApplicabilityConflict",
    "CTL11-A": "Contract.ExactValueEncodingInvalid", "CTL12-A": "Evidence.CommitIdentityOrHashMismatch",
    "CTL12-B": "Evidence.CommitIdentityOrHashMismatch",
}


def snapshot(root: Path) -> dict:
    return {p.relative_to(root).as_posix(): digest(p.read_bytes()) for p in sorted(root.rglob("*")) if p.is_file()}


def reseal_corpus(root: Path) -> None:
    """Injector maintenance only. This never changes a target's bad semantic value."""
    manifest = load_json(root / "manifest.json")
    for entry in manifest["cases"]:
        case_path = root / entry["case_ref"]["path"]
        case = load_json(case_path)
        for role, ref in case["materials"].items():
            case["materials"][role] = file_ref(root, ref["path"], role)
        write_json(case_path, case)
        entry["parent"] = case["parent"]
        entry["case_ref"] = file_ref(root, entry["case_ref"]["path"], "case")
    manifest["materials"] = [file_ref(root, item["path"], item["role"]) for item in manifest["materials"]]
    manifest["reference"] = file_ref(root, manifest["reference"]["path"], "reference")
    write_json(root / "manifest.json", manifest)


def _case_material(root: Path, case_id: str, role: str) -> Path:
    manifest = load_json(root / "manifest.json")
    case = read_case(root, manifest, case_id)
    return root / case["materials"][role]["path"]


def _json_edit(path: Path, change) -> None:
    value = load_json(path)
    change(value)
    write_json(path, value)


def _fresh_input_inventory(root: Path, case_id: str) -> None:
    data = _case_material(root, case_id, "input").read_bytes()
    records = inventory(data)
    _json_edit(_case_material(root, case_id, "definition"), lambda doc: doc.update(input_sha256=digest(data)))
    _json_edit(_case_material(root, case_id, "inventory"), lambda doc: doc.update(
        data_record_occurrences=len(records), unique_entity_ids=len({r.entity_id for r in records}),
        records=ledger(records)))


def prepare_corpus_control(root: Path, variant: str) -> str:
    case_id = {"CTL04-A": "S203-P06", "CTL04-B": "S203-P07", "CTL05-A": "S203-N02",
               "CTL06-A": "S203-N34", "CTL10-A": "S203-P05"}.get(variant, "S203-P01")
    input_path = _case_material(root, case_id, "input")
    truth_path = _case_material(root, case_id, "truth")
    if variant == "CTL01-A":
        data = input_path.read_bytes()
        old = b"#101=CARTESIAN_POINT('',(10.2,7.6,5.));"
        if data.count(old) != 1:
            raise InvalidEvidence("Control.SetupFailed", "CTL01 preimage")
        input_path.write_bytes(data.replace(old, old.replace(b"10.2", b"10.3"), 1))
        return case_id  # Deliberately do not reseal.
    if variant == "CTL02-A":
        _json_edit(truth_path, lambda doc: doc["measures"].update(area=exact(95)))
    elif variant == "CTL03-A":
        data = input_path.read_bytes()
        changes = [(b"#101=CARTESIAN_POINT('',(10.2,7.6,5.));", b"#101=CARTESIAN_POINT('',(9.6,6.8,5.));"),
                   (b"#102=CARTESIAN_POINT('',(3.8,12.4,5.));", b"#102=CARTESIAN_POINT('',(3.2,11.6,5.));"),
                   (b"#201=VECTOR('',#200,12.);", b"#201=VECTOR('',#200,11.);"),
                   (b"#221=VECTOR('',#220,12.);", b"#221=VECTOR('',#220,11.);")]
        for old, new in changes:
            if data.count(old) != 1:
                raise InvalidEvidence("Control.SetupFailed", "CTL03 preimage")
            data = data.replace(old, new, 1)
        input_path.write_bytes(data)
        _fresh_input_inventory(root, case_id)

        def wrong_truth(doc):
            doc["measures"] = {"area": exact(88), "perimeter": exact(38)}
            doc["centroid"] = [exact(Fraction(v)) for v in ("3.1", "4.8", "5")]
            doc["bounds"][1] = [exact(Fraction(v)) for v in ("9.6", "11.6", "5")]

        _json_edit(truth_path, wrong_truth)
    elif variant == "CTL05-A":
        def lost_duplicate(doc):
            doc["records"].pop()
            doc["data_record_occurrences"] = 51
        _json_edit(_case_material(root, case_id, "inventory"), lost_duplicate)
    elif variant == "CTL06-A":
        parent_id = "S203-P11"
        parent_data = _case_material(root, parent_id, "input").read_bytes()
        definition_path = _case_material(root, case_id, "definition")
        definition = load_json(definition_path)
        edit = definition["edits"][0]
        old, new = edit["old_text"].encode("ascii"), edit["new_text"].encode("ascii")
        if parent_data.count(old) != 1:
            raise InvalidEvidence("Control.SetupFailed", "CTL06 preimage")
        start = parent_data.index(old)
        input_path.write_bytes(parent_data[:start] + new + parent_data[start+len(old):])
        definition.update(parent=parent_id, parent_input_sha256=digest(parent_data))
        edit.update(start=start, end=start+len(old))
        write_json(definition_path, definition)
        _json_edit(truth_path, lambda doc: doc["facts"].update(mm_anchor_present=False))
        _fresh_input_inventory(root, case_id)
        _json_edit(root / "cases" / case_id / "case.json", lambda doc: doc.update(parent=parent_id))
    elif variant == "CTL10-A":
        _json_edit(truth_path, lambda doc: doc.update(material_volume={
            "applicability": "Applicable", "value": exact(0), "reason": "asserted material volume"}))
    elif variant == "CTL11-A":
        _json_edit(truth_path, lambda doc: doc["measures"].update(area=96.0))
    elif variant not in {"CTL04-A", "CTL04-B"}:
        raise InvalidEvidence("Control.UnknownVariant", variant)
    reseal_corpus(root)
    return case_id


def prepare_evidence_control(candidate: Path, variant: str, alternate_hash: str | None = None) -> None:
    if variant == "CTL07-A":
        comparison = candidate / "cases/S203-P01/1/comparison.json"
        _json_edit(comparison, lambda doc: doc.update(status="Matched"))
        _json_edit(candidate / "summary.json", lambda doc: doc.update(comparison_matched=1))
        seal(candidate)
    elif variant == "CTL08-A":
        (candidate / "cases/S203-P01/1/oracle.json").unlink()
        seal(candidate)
    elif variant == "CTL09-A":
        def duplicate_row(doc):
            replacement = next(row for row in doc["case_results"] if row["case_id"] == "S203-P12")
            index = next(i for i, row in enumerate(doc["case_results"]) if row["case_id"] == "S203-P13")
            doc["case_results"][index] = deepcopy(replacement)
        _json_edit(candidate / "summary.json", duplicate_row)
        seal(candidate)
    elif variant == "CTL12-A":
        _json_edit(candidate / "completion.json", lambda doc: doc.update(run_id="different-run"))
    elif variant == "CTL12-B":
        if alternate_hash is None or alternate_hash == digest((candidate / "artifacts.json").read_bytes()):
            raise InvalidEvidence("Control.SetupFailed", "need another actual package digest")
        _json_edit(candidate / "completion.json", lambda doc: doc.update(artifacts_sha256=alternate_hash))
    else:
        raise InvalidEvidence("Control.UnknownVariant", variant)


def execute(corpus: Path, output_root: Path) -> Path:
    baseline_result = verify(corpus)
    run_id = identifier()
    directory = output_root.resolve() / "controls" / run_id
    directory.mkdir(parents=True, exist_ok=False)
    baseline = run(corpus, directory / "baseline")
    alternate = run(corpus, directory / "alternate-baseline", ["S203-P01"])
    protected_before = {"corpus": snapshot(corpus), "baseline": snapshot(baseline), "alternate": snapshot(alternate)}
    results = []
    for variant in VARIANTS:
        attempt = directory / variant / "1"
        attempt.mkdir(parents=True)
        corpus_control = variant not in {"CTL07-A", "CTL08-A", "CTL09-A", "CTL12-A", "CTL12-B"}
        base = corpus if corpus_control else baseline
        candidate = attempt / ("corpus" if corpus_control else f"evidence/runs/{baseline.name}")
        candidate.parent.mkdir(parents=True, exist_ok=True)
        # Every target is newly created beneath this attempt; no existing user content is overwritten.
        if not candidate.resolve().is_relative_to(directory.resolve()):
            raise InvalidEvidence("Control.PathBoundary", str(candidate))
        shutil.copytree(base, candidate)
        original = snapshot(candidate)
        write_json(attempt / "baseline.receipt.json", {"baseline": str(base), "files": original,
                                                     "reference_sha256": baseline_result["trusted_reference_sha256"]})
        actual_code = None
        details = None
        verdict = "SetupFailed"
        try:
            if corpus_control:
                case_id = prepare_corpus_control(candidate, variant)
                target_call = lambda: verify(candidate, trusted_root=corpus, selected=[case_id])
            else:
                prepare_evidence_control(candidate, variant, digest((alternate / "artifacts.json").read_bytes()))
                target_call = lambda: check(candidate)
            changed = {path: {"old": original.get(path), "new": value} for path, value in snapshot(candidate).items()
                       if original.get(path) != value}
            removed = sorted(set(original) - set(snapshot(candidate)))
            if corpus_control:
                allowed = {"manifest.json"} | {f"cases/{case_id}/{name}" for name in (
                    "input.step", "case.json", "recipe.json", "mutation.json", "inventory.json", "truth.json")}
                # Re-sealing may not modify another case or the trusted reference.
            else:
                allowed = {"artifacts.json", "completion.json", "summary.json",
                           "cases/S203-P01/1/comparison.json", "cases/S203-P01/1/oracle.json"}
            if not (set(changed) | set(removed)) <= allowed:
                raise InvalidEvidence("Control.SetupFailed", "changes exceed the registered scope")
            if variant not in {"CTL04-A", "CTL04-B"} and not changed and not removed:
                raise InvalidEvidence("Control.SetupFailed", "no injected change")
            write_json(attempt / "setup.receipt.json", {"variant": variant, "changed": changed, "removed": removed})
            try:
                actual_result = target_call()
                details = {key: value for key, value in actual_result.items() if key != "cases"}
            except InvalidEvidence as exc:
                actual_code, details = exc.code, exc.detail
            verdict = "Passed" if actual_code == EXPECTED[variant] else "Failed"
        except Exception as exc:
            actual_code = exc.code if isinstance(exc, InvalidEvidence) else type(exc).__name__
            details = str(exc)
            verdict = "SetupFailed" if actual_code == "Control.SetupFailed" else "UnexpectedFailure"
        protection = protected_before == {
            "corpus": snapshot(corpus), "baseline": snapshot(baseline), "alternate": snapshot(alternate)}
        if not protection:
            verdict = "Failed"
        receipt = {"variant": variant, "verdict": verdict, "expected_tool_diagnostic": EXPECTED[variant],
                   "actual_tool_diagnostic": actual_code, "actual_evidence": details,
                   "protected_materials_unchanged": protection, "tool": tool_identity(),
                   "subject_execution": "NotRun"}
        write_json(attempt / "validator-trace.json", {"actual_diagnostic": actual_code, "evidence": details})
        write_json(attempt / "verdict.json", receipt)
        results.append(receipt)
    summary = {"control_groups": 12, "variants": len(results),
               "passed": sum(row["verdict"] == "Passed" for row in results),
               "all_passed": all(row["verdict"] == "Passed" for row in results),
               "results": results, "product_sample_count": 49, "product_execution": "NotRun",
               "protected_materials_unchanged": protected_before == {
                   "corpus": snapshot(corpus), "baseline": snapshot(baseline), "alternate": snapshot(alternate)}}
    write_json(directory / "summary.json", summary)
    (directory / "summary.html").write_bytes(page("01票：工具控制实际结果", summary))
    return directory
