# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Append-only, real-native ticket-03 evidence; no expected-to-observed substitution."""

from __future__ import annotations

import base64
from collections import Counter
import html
import json
from pathlib import Path
import subprocess
import sys

from tools.step_import_validation.common import (
    checked_bytes, digest, file_ref, inside, load_json, parse_json, write_json,
)
from tools.step_import_validation.corpus import verify
from tools.step_import_validation.evidence import identifier, now
from tools.step_inspect_validation.runner import page as raw_page
from .oracle import REFERENCE, REFERENCE_SHA, compare, reference, require

FORMAT = "t03.evidence.v1"
REQUIRED = {"input.step", "input.receipt.json", "inventory.json", "expected.json", "preparation.json",
            "oracle.json", "observed.json", "invocation.json", "comparison.json", "case.html"}


def page(title, body):
    rendered = raw_page(title, body).decode("utf-8")
    rendered = rendered.replace("仅语法、索引和引用检查。AP规则、单位解释、几何导入及SMLib构建均未运行。",
                                "03范围为名称/OID、字段与祖先、单零件和SI单位；全局规则、conversion、几何及SMLib构建未运行。")
    return rendered.encode("utf-8")


def source_identity():
    repo = Path(__file__).resolve().parents[2]
    files = []
    for component in ("STEP_SOURCE", "STEP_SEMANTICS"):
        root = repo / "source" / component
        files += [p for p in root.rglob("*") if p.suffix in {".cpp", ".h", ".lua"} or p.name == "CMakeLists.txt"]
    files += list(Path(__file__).parent.glob("*.py")) + [REFERENCE]
    return {p.relative_to(repo).as_posix(): digest(p.read_bytes()) for p in sorted(files)}


def run(binary: Path, corpus: Path, output_root: Path, selected=None):
    materials = verify(corpus, selected=selected)
    witness = reference()
    binary = binary.resolve(strict=True)
    binary_hash, sources = digest(binary.read_bytes()), source_identity()
    root = output_root.resolve()
    run_id = identifier()
    stage = inside(root, f"staging/{run_id}.partial")
    destination = inside(root, f"runs/{run_id}")
    stage.mkdir(parents=True, exist_ok=False)
    (stage / "reference.json").write_bytes(REFERENCE.read_bytes())
    metadata = {"format_version": FORMAT, "run_id": run_id, "started_utc": now(), "finished_utc": None,
                "selected": materials["selected"], "manifest_sha256": materials["manifest_sha256"],
                "reference_sha256": REFERENCE_SHA, "reader": {"path": str(binary), "sha256": binary_hash},
                "sources": sources, "python": sys.version, "scope": "SinglePartTypesSI",
                "full_import_execution": "NotRun", "native_build": "NotRun", "repair": "None"}
    write_json(stage / "run.json", metadata)
    rows = []
    for case in materials["cases"]:
        case_id = case["case_id"]
        prefix = stage / "cases" / case_id
        prefix.mkdir(parents=True)
        data = base64.b64decode(case["input_snapshot_b64"], validate=True)
        (prefix / "input.step").write_bytes(data)
        write_json(prefix / "input.receipt.json", {"case_id": case_id, "sha256": digest(data), "byte_length": len(data),
                                                  "identity": case["identity"]})
        write_json(prefix / "preparation.json", case["preparation"])
        write_json(prefix / "oracle.json", case["oracle"])
        write_json(prefix / "expected.json", case["expected"])
        inventory_bytes = checked_bytes(corpus, case["identity"]["materials"]["inventory"])
        (prefix / "inventory.json").write_bytes(inventory_bytes)
        invocation = [str(binary), "--fixture-limits", str(prefix / "input.step")]
        actual = subprocess.run(invocation, capture_output=True, timeout=30, check=False)
        (prefix / "observed.json").write_bytes(actual.stdout)
        write_json(prefix / "invocation.json", {"command": invocation, "exit_code": actual.returncode,
                                               "stderr": actual.stderr.decode("utf-8", errors="replace"),
                                               "reader_sha256": binary_hash, "execution": "Completed"})
        observation = parse_json(actual.stdout)
        comparison = compare(case, observation, actual.returncode, data, parse_json(inventory_bytes), witness)
        write_json(prefix / "comparison.json", comparison)
        body = ("<p>03当前范围合同：Matched；完整导入合同：NotChecked。</p>"
                "<p><a href='input.step'>实际输入</a> · <a href='observed.json'>真实C++输出</a> · "
                "<a href='invocation.json'>调用及二进制身份</a> · <a href='comparison.json'>独立比较</a></p>"
                "<h2>完整导入期望（后续票，未验收）</h2><pre>" +
                html.escape(json.dumps(case["expected"], ensure_ascii=False, indent=2)) + "</pre>"
                "<h2>本次实际源语义检查</h2><pre>" +
                html.escape(json.dumps(observation, ensure_ascii=False, indent=2)) + "</pre>")
        (prefix / "case.html").write_bytes(page(case_id, body))
        rows.append({"case_id": case_id, "semantic_status": observation["status"], "scope_contract": "Matched",
                     "first_blocking_key": comparison.get("first_blocking_key"),
                     "full_import_execution": "NotRun", "full_import_contract": "NotChecked"})
    require(binary_hash == digest(binary.read_bytes()) and sources == source_identity(),
            "reader/source changed during execution")
    counts = dict(Counter(row["semantic_status"] for row in rows))
    summary = {"format_version": FORMAT, "cases": rows, "scope_contract_matched": len(rows),
               "semantic_statuses": counts, "full_import_not_run": len(rows), "import_success_count": None}
    write_json(stage / "summary.json", summary)
    blocked = sum(counts.get(status, 0) for status in ("SourceFailure", "Rejected", "Unsupported"))
    body = (f"<p>当前范围合同匹配 {len(rows)} 项；{blocked} 项阻断，{counts.get('Deferred', 0)} 项conversion延后。</p>"
            "<p>Checked只覆盖03：名称/OID、字段/祖先、单零件与SI。其余负例仍须后续票处理。</p>"
            "<pre>" + html.escape(json.dumps(counts, indent=2)) + "</pre>"
            "<table><tr><th>样件</th><th>实际状态</th><th>首个阻断</th><th>03合同</th><th>完整导入</th></tr>")
    for row in rows:
        key = row["case_id"]
        body += (f"<tr><td><a href='cases/{key}/case.html'>{key}</a></td><td>{row['semantic_status']}</td>"
                 f"<td>{html.escape(row['first_blocking_key'] or '')}</td><td>Matched</td><td>NotRun</td></tr>")
    (stage / "summary.html").write_bytes(page("STEP 03：单零件、类型与SI单位验收", body + "</table>"))
    metadata["finished_utc"] = now()
    write_json(stage / "run.json", metadata)
    artifacts = [file_ref(stage, p.relative_to(stage).as_posix(), "evidence")
                 for p in sorted(stage.rglob("*")) if p.is_file()]
    write_json(stage / "artifacts.json", {"format_version": FORMAT, "artifacts": artifacts})
    write_json(stage / "completion.json", {"format_version": FORMAT, "run_id": run_id,
                                           "artifacts_sha256": digest((stage / "artifacts.json").read_bytes())})
    check(stage, staged=True)
    destination.parent.mkdir(parents=True, exist_ok=True)
    require(not destination.exists(), "existing run must not be overwritten")
    require(stage.resolve().is_relative_to(root) and destination.resolve().is_relative_to(root), "publication boundary")
    stage.rename(destination)
    check(destination)
    return destination


def check(root: Path, staged=False):
    metadata, completion = load_json(root / "run.json"), load_json(root / "completion.json")
    require(metadata["format_version"] == completion["format_version"] == FORMAT, "evidence format")
    require(root.parent.name == ("staging" if staged else "runs"), "uncommitted evidence location")
    require(root.name == metadata["run_id"] + (".partial" if staged else ""), "run name/identity")
    require(completion["run_id"] == metadata["run_id"] and completion["artifacts_sha256"] ==
            digest((root / "artifacts.json").read_bytes()), "completion binding")
    refs = load_json(root / "artifacts.json")["artifacts"]
    paths = [ref["path"] for ref in refs]
    require(len(paths) == len(set(paths)), "duplicate artifacts")
    selected = metadata["selected"]
    require(selected and len(selected) == len(set(selected)), "case selection")
    required = {"run.json", "reference.json", "summary.json", "summary.html"}
    required |= {f"cases/{key}/{name}" for key in selected for name in REQUIRED}
    require(required <= set(paths), "required artifact missing")
    for ref in refs:
        checked_bytes(root, ref, 20_000_000)
    require(digest((root / "reference.json").read_bytes()) == metadata["reference_sha256"] == REFERENCE_SHA,
            "independent witness identity")
    witness = load_json(root / "reference.json")
    require(metadata["scope"] == "SinglePartTypesSI" and
            metadata["full_import_execution"] == metadata["native_build"] == "NotRun", "run scope")
    summary = load_json(root / "summary.json")
    require([r["case_id"] for r in summary["cases"]] == selected, "selected case coverage")
    require(summary["scope_contract_matched"] == summary["full_import_not_run"] == len(selected) and
            summary["import_success_count"] is None, "scope counters")
    require(summary["semantic_statuses"] == dict(Counter(r["semantic_status"] for r in summary["cases"])),
            "status counters")
    for row in summary["cases"]:
        prefix = root / "cases" / row["case_id"]
        data, receipt = (prefix / "input.step").read_bytes(), load_json(prefix / "input.receipt.json")
        require(receipt["case_id"] == row["case_id"] and receipt["byte_length"] == len(data) and
                receipt["sha256"] == digest(data),
                "input receipt")
        for role, file in (("assertions", "expected.json"), ("inventory", "inventory.json")):
            require(digest((prefix / file).read_bytes()) == receipt["identity"]["materials"][role]["sha256"],
                    "frozen material identity")
        invocation = load_json(prefix / "invocation.json")
        require(invocation["execution"] == "Completed" and invocation["reader_sha256"] == metadata["reader"]["sha256"],
                "reader identity")
        case = {"case_id": row["case_id"], "identity": receipt["identity"],
                "expected": load_json(prefix / "expected.json")}
        observation = load_json(prefix / "observed.json")
        comparison = compare(case, observation, invocation["exit_code"], data,
                             load_json(prefix / "inventory.json"), witness)
        require(comparison == load_json(prefix / "comparison.json"), "comparison/evidence state")
        require(row["semantic_status"] == observation["status"] and row["scope_contract"] == "Matched", "case status")
        require(row["first_blocking_key"] == comparison.get("first_blocking_key") and
                row["full_import_execution"] == "NotRun" and row["full_import_contract"] == "NotChecked",
                "full-import overclaim")
    return {"evidence_completeness": "Complete", "selected": len(selected), "scope_contract_matched": len(selected),
            "full_import_execution": "NotRun"}
