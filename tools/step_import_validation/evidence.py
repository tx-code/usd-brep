# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Append-only evidence packages; Complete never means product import passed."""

from __future__ import annotations

import base64
from datetime import datetime, timezone
import html
import json
from pathlib import Path
import platform
import re
import sys
import uuid

from . import FORMAT
from .common import InvalidEvidence, checked_bytes, digest, file_ref, inside, load_json, tool_identity, write_json
from .corpus import verify

CASE_FILES = {"input.step", "input.receipt.json", "preparation.json", "oracle.json", "observed.json",
              "comparison.json", "diagnostics.json", "case.html"}
CASE_ID = re.compile(r"S203-(P(?:0[1-9]|1[0-3])|N(?:0[1-9]|[12][0-9]|3[0-6]))\Z")


def now() -> str:
    return datetime.now(timezone.utc).isoformat()


def identifier() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + uuid.uuid4().hex[:12]


def page(title: str, payload: object) -> bytes:
    text = json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False)
    return ("<!doctype html><html lang='zh-CN'><meta charset='utf-8'><meta name='viewport' "
            "content='width=device-width'><title>" + html.escape(title) + "</title><style>"
            "body{max-width:1100px;margin:32px auto;padding:0 20px;font:16px/1.7 system-ui;color:#243441}"
            "pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#f3f5f7;padding:20px}"
            "a{color:#185c85}li{margin:6px 0}</style><h1>" + html.escape(title) + "</h1>"
            "<p>材料/独立数学证据。产品导入未配置、未运行；不是AP符合性或native验收。</p>"
            "<pre>" + html.escape(text) + "</pre></html>\n").encode("utf-8")


def seal(directory: Path) -> None:
    """Called only after payload is frozen, or explicitly by a control injector."""
    refs = []
    for path in sorted(directory.rglob("*")):
        if path.is_file() and path.name not in {"artifacts.json", "completion.json"}:
            refs.append(file_ref(directory, path.relative_to(directory).as_posix(), "evidence"))
    write_json(directory / "artifacts.json", {"format_version": FORMAT, "artifacts": refs})
    run = load_json(directory / "run.json")
    write_json(directory / "completion.json", {"format_version": FORMAT, "run_id": run["run_id"],
                                               "artifacts_sha256": digest((directory / "artifacts.json").read_bytes())})


def check(directory: Path, *, staged: bool = False) -> dict:
    directory = directory.resolve()
    if directory.parent.name != ("staging" if staged else "runs"):
        raise InvalidEvidence("Evidence.UncommittedLocation", str(directory))
    run = load_json(directory / "run.json")
    completion = load_json(directory / "completion.json")
    expected_name = run.get("run_id", "") + (".partial" if staged else "")
    if (completion.get("run_id") != run.get("run_id") or directory.name != expected_name or
            completion.get("artifacts_sha256") != digest((directory / "artifacts.json").read_bytes())):
        raise InvalidEvidence("Evidence.CommitIdentityOrHashMismatch", str(directory))
    artifacts = load_json(directory / "artifacts.json")["artifacts"]
    paths = [item["path"] for item in artifacts]
    if len(paths) != len(set(paths)):
        raise InvalidEvidence("Evidence.DuplicateArtifact", paths)
    for item in artifacts:
        checked_bytes(directory, item, 10_000_000)
    selected = run["selected"]
    if len(selected) != len(set(selected)):
        raise InvalidEvidence("Evidence.CaseCoverageMismatch", selected)
    required = {"run.json", "summary.json", "summary.html"}
    required |= {f"cases/{case_id}/1/{name}" for case_id in selected for name in CASE_FILES}
    missing = required - set(paths)
    if missing:
        raise InvalidEvidence("Evidence.RequiredArtifactMissing", sorted(missing))
    for case_id in selected:
        if not isinstance(case_id, str) or not CASE_ID.fullmatch(case_id):
            raise InvalidEvidence("Evidence.CaseCoverageMismatch", selected)
        prefix = directory / "cases" / case_id / "1"
        observed = load_json(prefix / "observed.json")
        comparison = load_json(prefix / "comparison.json")
        if (observed != {"configuration": "NotConfigured", "execution": "NotRun", "result": None,
                         "model_present": None, "reason": "Ticket 01 has no product adapter"} or
                comparison != {"status": "NotChecked", "reason": "No subject observation"}):
            raise InvalidEvidence("Evidence.StateContradiction", case_id)
        receipt = load_json(prefix / "input.receipt.json")
        snapshot = (prefix / "input.step").read_bytes()
        if (receipt.get("sha256") != digest(snapshot) or receipt.get("byte_length") != len(snapshot) or
                receipt.get("case_id") != case_id or receipt.get("attempt_id") != "1"):
            raise InvalidEvidence("Evidence.InputSnapshotMismatch", case_id)
        if (load_json(prefix / "preparation.json").get("status") != "Passed" or
                load_json(prefix / "oracle.json").get("status") != "Passed"):
            raise InvalidEvidence("Evidence.StateContradiction", case_id)
    summary = load_json(directory / "summary.json")
    summary_ids = [row["case_id"] for row in summary["case_results"]]
    if sorted(summary_ids) != sorted(selected) or len(summary_ids) != len(set(summary_ids)):
        raise InvalidEvidence("Evidence.CaseCoverageMismatch", summary_ids)
    if (summary["subject_not_run"] != len(selected) or summary["import_success_count"] is not None or
            summary["comparison_matched"] != 0 or summary["selected_cases"] != len(selected) or
            summary["material_preparation_passed"] != len(selected) or summary["oracle_passed"] != len(selected)):
        raise InvalidEvidence("Evidence.StateContradiction", "summary")
    expected_rows = [{"case_id": case_id, "attempt_id": "1", "preparation": "Passed", "oracle": "Passed",
                      "subject_execution": "NotRun", "comparison": "NotChecked"} for case_id in selected]
    if summary["case_results"] != expected_rows:
        raise InvalidEvidence("Evidence.StateContradiction", "case rows")
    return {"evidence_completeness": "Complete", "selected_cases": len(selected),
            "subject_execution": "NotRun", "import_success_count": None}


def run(corpus: Path, output_root: Path, selected: list[str] | None = None) -> Path:
    started = now()
    result = verify(corpus, selected=selected)
    run_id = identifier()
    output_root = output_root.resolve()
    stage = inside(output_root, f"staging/{run_id}.partial")
    destination = inside(output_root, f"runs/{run_id}")
    stage.mkdir(parents=True, exist_ok=False)
    run_metadata = {"format_version": FORMAT, "run_id": run_id, "selected": result["selected"],
                    "started_utc": started, "finished_utc": None,
                    "manifest_sha256": result["manifest_sha256"],
                    "reference_sha256": result["trusted_reference_sha256"], "tool": tool_identity(),
                    "configuration": result["configuration"],
                    "python": {"executable": sys.executable, "version": sys.version, "platform": platform.platform()},
                    "mode": "PreparationAndOracle", "subject_configuration": "NotConfigured",
                    "reader_binary": None, "kernel_binary": None, "repair": "None"}
    write_json(stage / "run.json", run_metadata)
    summary_rows = []
    for case in result["cases"]:
        case_id = case["case_id"]
        prefix = stage / "cases" / case_id / "1"
        prefix.mkdir(parents=True)
        snapshot = base64.b64decode(case["input_snapshot_b64"], validate=True)
        (prefix / "input.step").write_bytes(snapshot)
        write_json(prefix / "input.receipt.json", {"case_id": case_id, "attempt_id": "1",
                                                  "sha256": digest(snapshot), "byte_length": len(snapshot),
                                                  "identity": case["identity"],
                                                  "complete": True})
        write_json(prefix / "preparation.json", case["preparation"])
        write_json(prefix / "oracle.json", case["oracle"])
        write_json(prefix / "observed.json", {"configuration": "NotConfigured", "execution": "NotRun",
                                              "result": None, "model_present": None,
                                              "reason": "Ticket 01 has no product adapter"})
        write_json(prefix / "comparison.json", {"status": "NotChecked", "reason": "No subject observation"})
        write_json(prefix / "diagnostics.json", {"expected": case["expected"], "subject_observed": None,
                                                 "tool_observed": []})
        case_display = {key: value for key, value in case.items() if key != "input_snapshot_b64"}
        (prefix / "case.html").write_bytes(page(case_id, case_display))
        summary_rows.append({"case_id": case_id, "attempt_id": "1", "preparation": "Passed", "oracle": "Passed",
                             "subject_execution": "NotRun", "comparison": "NotChecked"})
    summary = {"format_version": FORMAT, "case_results": summary_rows, "selected_cases": len(summary_rows),
               "material_preparation_passed": result["material_preparation_passed"],
               "oracle_passed": result["oracle_passed"], "subject_not_run": result["subject_not_run"],
               "comparison_matched": 0, "import_success_count": None}
    write_json(stage / "summary.json", summary)
    links = "".join(f"<li><a href='cases/{row['case_id']}/1/case.html'>{row['case_id']}</a></li>"
                    for row in summary_rows)
    display = page("AP203e2：材料与独立真值验收", summary).decode("utf-8")
    (stage / "summary.html").write_bytes(display.replace("<pre>", "<ul>" + links + "</ul><pre>", 1).encode("utf-8"))
    run_metadata["finished_utc"] = now()
    write_json(stage / "run.json", run_metadata)
    seal(stage)
    check(stage, staged=True)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        raise InvalidEvidence("Evidence.ExistingRun", str(destination))
    # Both resolved paths are children of the caller's configured root; same filesystem.
    if not stage.resolve().is_relative_to(output_root) or not destination.resolve().is_relative_to(output_root):
        raise InvalidEvidence("Evidence.PathBoundary", str(destination))
    stage.rename(destination)
    check(destination)
    return destination
