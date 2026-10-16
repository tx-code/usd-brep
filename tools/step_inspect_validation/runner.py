# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Execute the C++ reader and compare its source-only results with frozen material."""

from __future__ import annotations

import base64
from collections import Counter
import html
import json
from pathlib import Path
import subprocess
import sys

from tools.step_import_validation.common import (
    InvalidEvidence, checked_bytes, digest, file_ref, inside, load_json, parse_json, write_json,
)
from tools.step_import_validation.corpus import verify
from tools.step_import_validation.evidence import identifier, now
from tools.step_import_validation.records import split_args

FORMAT = "t02.evidence.v1"
NATIVE_FORMAT = "step-source.v1"
SOURCE_REJECTIONS = {"S203-N02", "S203-N03"}
REQUIRED = {"input.step", "input.receipt.json", "preparation.json", "oracle.json", "expected.json",
            "inventory.json", "inspect.json", "invocation.json", "comparison.json", "case.html"}


def require(condition, message):
    if not condition:
        raise InvalidEvidence("Ticket02.ObservationConflict", message)


def position(data: bytes, start: int) -> dict:
    return {"line": data[:start].count(b"\n") + 1, "column": start - data.rfind(b"\n", 0, start)}


def raw_span(value: dict, data: bytes) -> str:
    start, end = value["span"]
    require(type(start) is int and type(end) is int and 0 <= start < end <= len(data), "invalid span")
    raw = data[start:end].decode("ascii")
    require(value["raw"] == raw, "span does not describe actual bytes")
    return raw


def check_value(value: dict, data: bytes) -> None:
    raw = raw_span(value, data)
    kind = value["kind"]
    first = raw[0]
    expected_kind = ({"'": "String", "#": "Reference", "$": "Unset", "*": "Derived", "(": "Aggregate",
                      ".": "Enumeration"}.get(first) or
                     ("Typed" if first.isupper() or first == "_" else "Real" if "." in raw else "Integer"))
    require(kind == expected_kind, "lexical value kind")
    children = value["children"]
    if kind in {"Aggregate", "Typed"}:
        begin = 0 if kind == "Aggregate" else raw.index("(")
        require(raw[begin] == "(" and raw.endswith(")"), "container delimiters")
        tokens = split_args(raw[begin + 1:-1])
        require([child["raw"] for child in children] == tokens, "value tree dropped/reordered raw parameters")
        check_parameter_spans(value, begin, children)
        require(value["text"] == ("" if kind == "Aggregate" else raw[:begin]), "typed name")
        for child in children:
            require(value["span"][0] <= child["span"][0] < child["span"][1] <= value["span"][1], "child span")
            check_value(child, data)
    else:
        require(not children, "scalar has children")
        if kind == "String":
            require(raw.startswith("'") and raw.endswith("'"), "string delimiters")
            require(value["text"] == raw[1:-1].replace("''", "'").replace("\\\\", "\\"), "decoded string")
        elif kind == "Reference":
            require(value["text"] == raw[1:].lstrip("0"), "reference identity")
        elif kind == "Enumeration":
            require(value["text"] == raw[1:-1], "enumeration value")
        else:
            require(value["text"] == raw, "exact numeric/marker lexeme")


def check_component(component: dict, data: bytes) -> None:
    raw = raw_span(component, data)
    begin = raw.index("(")
    require(raw[:begin] == component["name"] and raw.endswith(")"), "component name/delimiters")
    require([p["raw"] for p in component["parameters"]] == split_args(raw[begin + 1:-1]), "parameter coverage")
    check_parameter_spans(component, begin, component["parameters"])
    for value in component["parameters"]:
        require(component["span"][0] <= value["span"][0] < value["span"][1] <= component["span"][1], "parameter span")
        check_value(value, data)


def check_parameter_spans(parent: dict, opening: int, children: list[dict]) -> None:
    # Frozen ticket-01 records have no embedded comments; this checker is deliberately narrow.
    raw = parent["raw"]
    cursor = opening + 1
    for index, child in enumerate(children):
        while raw[cursor].isspace():
            cursor += 1
        start = cursor
        cursor += len(child["raw"])
        require(child["span"] == [parent["span"][0] + start, parent["span"][0] + cursor], "parameter slot span")
        while raw[cursor].isspace():
            cursor += 1
        require(raw[cursor] == ("," if index + 1 < len(children) else ")"), "parameter slot delimiter")
        cursor += 1


def compare(case: dict, inspection: dict, exit_code: int, data: bytes, inventory: dict) -> dict:
    """Checks fixed ticket-01 spelling, not a second general-purpose STEP parser."""
    case_id = case["case_id"]
    require(inspection["format_version"] == NATIVE_FORMAT, "reader format")
    require(inspection["scope"] == "SyntaxIndexReferences", "reader scope")
    require(inspection["input"]["capture_complete"] is True, "input not completely captured")
    require(inspection["input"]["sha256"] == digest(data), "reader input hash")
    require(inspection["input"]["byte_length"] == len(data), "reader input length")
    require(inspection["schema_validation"] == inspection["native_build"] == "NotRun", "scope overclaim")
    if case_id in SOURCE_REJECTIONS:
        require(exit_code == 1 and inspection["status"] == "Rejected", "required source rejection")
        require(inspection["source_complete"] is False, "failed source published as complete")
        require(inspection["statistics"]["complete"] is False, "failed statistics published as complete")
        require(all(inspection[key] is None for key in ("header", "records", "references")), "partial graph escaped")
        require(len(inspection["diagnostics"]) == 1, "source rejection evidence")
        diagnostic = inspection["diagnostics"][0]
        require(diagnostic["key"] == case["expected"]["diagnostic_key"], "frozen diagnostic key")
        require(diagnostic["stage"] == case["expected"]["expected_stage"], "frozen diagnostic stage")
        require(diagnostic["location"] == position(data, diagnostic["span"][0]), "diagnostic line/column")
        if case_id == "S203-N02":
            matches = [r for r in inventory["records"] if r["source_id"] == 100]
            spans = [[r["span"][0], r["span"][0] + 4] for r in matches]
            require(diagnostic["span"] == spans[1], "duplicate second occurrence")
            require(diagnostic["related"] == [{"span": spans[0], "location": position(data, spans[0][0])}],
                    "duplicate first occurrence")
            require(diagnostic["record_id"] == diagnostic["target_id"] == "100", "duplicate ID identity")
        else:
            start = data.index(b"#99999")
            require(diagnostic["span"] == [start, start + 6], "missing reference token location")
            require(diagnostic["record_id"] == "9" and diagnostic["target_id"] == "99999", "missing reference IDs")
            require(diagnostic["component"] == "SHAPE_DEFINITION_REPRESENTATION" and
                    diagnostic["parameter_path"] == [1], "#9.definition positional role")
        return {"scope": "SyntaxIndexReferences", "status": "Matched", "checks": ["input identity", "source rejection",
                "stage/key", "exact source evidence", "no partial source"], "full_import_contract": "NotChecked"}
    require(exit_code == 0 and inspection["status"] == "Inspected" and inspection["source_complete"] is True,
            "complete source inspection")
    require(inspection["statistics"]["complete"] is True, "incomplete statistics")
    require(inspection["diagnostics"] == [], "unexpected source diagnostic")
    require(inspection["input"]["raw"].encode("ascii") == data, "source snapshot bytes")
    expected_records = inventory["records"]
    actual_records = inspection["records"]
    require(len(actual_records) == inventory["data_record_occurrences"], "complete DATA coverage")
    for actual, expected in zip(actual_records, expected_records):
        raw = raw_span(actual, data)
        require(actual["id"] == str(expected["source_id"]), "record ID")
        require(actual["record_occurrence"] == expected["record_occurrence"], "occurrence order")
        require(raw == f"#{expected['source_id']}={expected['raw']};", "raw record value")
        require(actual["span"] == [expected["span"][0], expected["span"][1] - 1], "record span")
        require(actual["location"] == position(data, actual["span"][0]), "record line/column")
        components = actual["components"]
        for component in components:
            check_component(component, data)
        if actual["complex"]:
            require("(" + " ".join(c["raw"] for c in components) + ")" == expected["raw"],
                    "complex constituent coverage")
        else:
            require(len(components) == 1 and components[0]["raw"] == expected["raw"], "simple record coverage")
    require([h["name"] for h in inspection["header"]] == ["FILE_DESCRIPTION", "FILE_NAME", "FILE_SCHEMA"],
            "complete header")
    for header in inspection["header"]:
        check_component(header, data)
    expected_header = [line[:-1] for line in data.decode("ascii").splitlines() if line.startswith("FILE_")]
    require([h["raw"] for h in inspection["header"]] == expected_header, "all authored HEADER values")

    walked = []

    def walk(value, r, c, path):
        if value["kind"] == "Reference":
            walked.append((value["text"], value["span"], r, c, path))
        for index, child in enumerate(value["children"], 1):
            walk(child, r, c, path + [index])

    for r, record in enumerate(actual_records):
        for c, component in enumerate(record["components"]):
            for p, value in enumerate(component["parameters"], 1):
                walk(value, r, c, [p])
    actual_refs = inspection["references"]
    require(len(walked) == len(actual_refs) == sum(len(r["references"]) for r in expected_records),
            "reference coverage")
    index = {r["id"]: i for i, r in enumerate(actual_records)}
    for expected, actual in zip(walked, actual_refs):
        target, span, r, c, path = expected
        require(actual == {"target_id": target, "span": span, "record_index": r, "component_index": c,
                           "parameter_path": path, "target_record_index": index[target]},
                "resolved reference index")
    require(inspection["statistics"]["data_records"] == len(actual_records), "statistics record count")
    require(inspection["statistics"]["references"] == len(actual_refs), "statistics reference count")
    return {"scope": "SyntaxIndexReferences", "status": "Matched", "checks": ["input identity", "full HEADER/DATA",
            "all values/constituents", "raw spans", "all reference paths and targets"],
            "full_import_contract": "NotChecked", "later_expected_diagnostic": case["expected"]["diagnostic_key"]}


def page(title: str, body: str) -> bytes:
    return ("<!doctype html><html lang='zh-CN'><meta charset='utf-8'><meta name='viewport' "
            "content='width=device-width'><title>" + html.escape(title) + "</title><style>"
            "body{max-width:1100px;margin:32px auto;padding:0 20px;font:16px/1.7 system-ui;color:#243441}"
            "pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#f3f5f7;padding:16px}"
            "table{border-collapse:collapse;width:100%;display:block;overflow-x:auto}"
            "td,th{padding:8px;border-bottom:1px solid #ddd;text-align:left}a{color:#185c85}</style><h1>" +
            html.escape(title) + "</h1><p>仅语法、索引和引用检查。AP规则、单位解释、几何导入及SMLib构建均未运行。</p>" +
            body + "</html>\n").encode("utf-8")


def source_identity() -> dict:
    repo = Path(__file__).resolve().parents[2]
    files = list((repo / "source/STEP_SOURCE").rglob("*.cpp"))
    files += list((repo / "source/STEP_SOURCE").rglob("*.h"))
    files += [repo / "source/STEP_SOURCE/CMakeLists.txt", repo / "source/STEP_SOURCE/premake5.lua"]
    files += list(Path(__file__).parent.glob("*.py"))
    return {p.relative_to(repo).as_posix(): digest(p.read_bytes()) for p in sorted(files)}


def run(binary: Path, corpus: Path, output_root: Path, selected: list[str] | None = None) -> Path:
    materials = verify(corpus, selected=selected)
    binary = binary.resolve(strict=True)
    binary_hash = digest(binary.read_bytes())
    root = output_root.resolve()
    run_id = identifier()
    stage = inside(root, f"staging/{run_id}.partial")
    destination = inside(root, f"runs/{run_id}")
    stage.mkdir(parents=True, exist_ok=False)
    metadata = {"format_version": FORMAT, "run_id": run_id, "started_utc": now(), "finished_utc": None,
                "selected": materials["selected"], "manifest_sha256": materials["manifest_sha256"],
                "reference_sha256": materials["trusted_reference_sha256"],
                "reader": {"path": str(binary), "sha256": binary_hash}, "python": sys.version,
                "sources": source_identity(),
                "scope": "SyntaxIndexReferences", "full_import_execution": "NotRun", "native_build": "NotRun"}
    write_json(stage / "run.json", metadata)
    rows = []
    for case in materials["cases"]:
        case_id = case["case_id"]
        prefix = stage / "cases" / case_id
        prefix.mkdir(parents=True)
        data = base64.b64decode(case["input_snapshot_b64"], validate=True)
        (prefix / "input.step").write_bytes(data)
        write_json(prefix / "input.receipt.json", {"sha256": digest(data), "byte_length": len(data),
                                                  "identity": case["identity"], "case_id": case_id})
        write_json(prefix / "preparation.json", case["preparation"])
        write_json(prefix / "oracle.json", case["oracle"])
        write_json(prefix / "expected.json", case["expected"])
        command = [str(binary), "--fixture-limits", str(prefix / "input.step")]
        actual = subprocess.run(command, capture_output=True, timeout=30, check=False)
        (prefix / "inspect.json").write_bytes(actual.stdout)
        write_json(prefix / "invocation.json", {"command": command, "exit_code": actual.returncode,
                                               "stderr": actual.stderr.decode("utf-8", errors="replace"),
                                               "reader_sha256": binary_hash, "execution": "Completed"})
        observation = parse_json(actual.stdout)
        inventory_bytes = checked_bytes(corpus, case["identity"]["materials"]["inventory"])
        (prefix / "inventory.json").write_bytes(inventory_bytes)
        inventory = parse_json(inventory_bytes)
        comparison = compare(case, observation, actual.returncode, data, inventory)
        write_json(prefix / "comparison.json", comparison)
        body = ("<p>源检查合同：Matched；完整导入合同：NotChecked。</p>"
                "<p><a href='input.step'>实际输入</a> · <a href='inspect.json'>原始读取器输出</a> · "
                "<a href='invocation.json'>实际调用</a> · <a href='comparison.json'>独立比较</a></p>"
                "<h2>完整导入期望（后续票，尚未验收）</h2><pre>" +
                html.escape(json.dumps(case["expected"], ensure_ascii=False, indent=2)) + "</pre>"
                "<h2>实际源检查</h2><pre>" + html.escape(json.dumps(observation, ensure_ascii=False, indent=2)) + "</pre>")
        (prefix / "case.html").write_bytes(page(case_id, body))
        rows.append({"case_id": case_id, "source_status": observation["status"], "source_contract": "Matched",
                     "full_import_execution": "NotRun", "full_import_contract": "NotChecked"})
    require(digest(binary.read_bytes()) == binary_hash, "reader binary changed during execution")
    summary = {"format_version": FORMAT, "cases": rows, "source_contract_matched": len(rows),
               "source_statuses": dict(Counter(r["source_status"] for r in rows)),
               "full_import_not_run": len(rows), "import_success_count": None}
    write_json(stage / "summary.json", summary)
    body = ("<p>源检查合同匹配 " + str(len(rows)) + " 项；完整导入未运行 " + str(len(rows)) + " 项。</p>"
            "<p>Inspected表示原始文件图可检查，不表示源AP/几何合法。其余反例仍须后续票拒绝。</p>"
            "<table><tr><th>样件</th><th>实际源检查</th><th>02合同</th><th>完整导入</th></tr>")
    for row in rows:
        key = row["case_id"]
        body += (f"<tr><td><a href='cases/{key}/case.html'>{key}</a></td><td>{row['source_status']}</td>"
                 "<td>Matched</td><td>NotRun</td></tr>")
    (stage / "summary.html").write_bytes(page("STEP 02：自研源解析与inspect验收", body + "</table>"))
    metadata["finished_utc"] = now()
    write_json(stage / "run.json", metadata)
    refs = [file_ref(stage, p.relative_to(stage).as_posix(), "evidence")
            for p in sorted(stage.rglob("*")) if p.is_file()]
    write_json(stage / "artifacts.json", {"format_version": FORMAT, "artifacts": refs})
    write_json(stage / "completion.json", {"format_version": FORMAT, "run_id": run_id,
                                           "artifacts_sha256": digest((stage / "artifacts.json").read_bytes())})
    check(stage, staged=True)
    destination.parent.mkdir(parents=True, exist_ok=True)
    require(stage.resolve().is_relative_to(root) and destination.resolve().is_relative_to(root), "publication path")
    stage.rename(destination)
    check(destination)
    return destination


def check(root: Path, *, staged: bool = False) -> dict:
    metadata = load_json(root / "run.json")
    completion = load_json(root / "completion.json")
    require(metadata["format_version"] == completion["format_version"] == FORMAT, "evidence format")
    require(metadata["scope"] == "SyntaxIndexReferences" and
            metadata["full_import_execution"] == metadata["native_build"] == "NotRun", "run scope")
    require(root.parent.name == ("staging" if staged else "runs"), "uncommitted evidence location")
    require(root.name == metadata["run_id"] + (".partial" if staged else ""), "run directory identity")
    require(completion["run_id"] == metadata["run_id"] and completion["artifacts_sha256"] ==
            digest((root / "artifacts.json").read_bytes()), "completion identity")
    refs = load_json(root / "artifacts.json")["artifacts"]
    paths = [ref["path"] for ref in refs]
    require(len(paths) == len(set(paths)), "duplicate evidence path")
    required = {"run.json", "summary.json", "summary.html"}
    selected = metadata["selected"]
    require(len(selected) == len(set(selected)) and selected, "invalid selection")
    required |= {f"cases/{key}/{name}" for key in selected for name in REQUIRED}
    require(required <= set(paths), "required evidence missing")
    for ref in refs:
        checked_bytes(root, ref, 20_000_000)
    summary = load_json(root / "summary.json")
    require([row["case_id"] for row in summary["cases"]] == selected, "case coverage")
    require(summary["source_contract_matched"] == len(selected) and summary["full_import_not_run"] == len(selected),
            "scope counts")
    require(summary["import_success_count"] is None, "invented import success count")
    require(summary["source_statuses"] == dict(Counter(row["source_status"] for row in summary["cases"])),
            "source status counts")
    for row in summary["cases"]:
        prefix = root / "cases" / row["case_id"]
        observation = load_json(prefix / "inspect.json")
        invocation = load_json(prefix / "invocation.json")
        comparison = load_json(prefix / "comparison.json")
        receipt = load_json(prefix / "input.receipt.json")
        data = (prefix / "input.step").read_bytes()
        require(receipt["sha256"] == digest(data) == observation["input"]["sha256"], "captured identity")
        require(receipt["case_id"] == row["case_id"] and receipt["byte_length"] == len(data), "case receipt")
        inventory_hash = receipt["identity"]["materials"]["inventory"]["sha256"]
        require(digest((prefix / "inventory.json").read_bytes()) == inventory_hash,
                "frozen occurrence inventory identity")
        expected_hash = receipt["identity"]["materials"]["assertions"]["sha256"]
        require(digest((prefix / "expected.json").read_bytes()) == expected_hash, "frozen expected identity")
        require(invocation["execution"] == "Completed" and invocation["reader_sha256"] == metadata["reader"]["sha256"],
                "reader observation identity")
        require(comparison["status"] == row["source_contract"] == "Matched", "source comparison state")
        require(row["full_import_execution"] == "NotRun" and row["full_import_contract"] ==
                comparison["full_import_contract"] == "NotChecked", "scope state")
        require(row["source_status"] == observation["status"], "source status state")
        rerun = compare({"case_id": row["case_id"], "expected": load_json(prefix / "expected.json")}, observation,
                        invocation["exit_code"], data, load_json(prefix / "inventory.json"))
        require(rerun == comparison, "saved comparison is inconsistent with raw observation")
    return {"evidence_completeness": "Complete", "selected": len(selected),
            "source_contract_matched": len(selected), "full_import_execution": "NotRun"}
