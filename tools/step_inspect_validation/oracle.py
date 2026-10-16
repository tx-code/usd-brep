# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Independent raw-observation comparison; has no execution/publication dependency."""
from __future__ import annotations

from tools.step_import_validation.common import InvalidEvidence, digest
from tools.step_import_validation.records import split_args

NATIVE_FORMAT = "step-source.v1"
SOURCE_REJECTIONS = {"S203-N02", "S203-N03"}


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
