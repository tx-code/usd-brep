# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Inventory for our frozen one-record-per-line ASCII fixtures, NOT a STEP loader.

No comments, multiline DATA records, alternate encodings or arbitrary input models
are supported. Duplicate occurrences and raw tokens deliberately remain intact.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from fractions import Fraction

from .common import InvalidEvidence

LINE = re.compile(rb"#([0-9]+)=(.*);\n\Z")
SIMPLE = re.compile(r"([A-Z][A-Z_0-9]*)\((.*)\)\Z", re.S)


def nesting_depth(text: str) -> int:
    # Strip quoted literal contents before counting; complex records are included.
    text = re.sub(r"'(?:[^']|'')*'", "''", text)
    depth = maximum = 0
    for ch in text:
        depth += (ch == "(") - (ch == ")")
        maximum = max(maximum, depth)
        if depth < 0:
            raise InvalidEvidence("Preparation.FixtureTokenShapeInvalid", "unbalanced parentheses")
    if depth:
        raise InvalidEvidence("Preparation.FixtureTokenShapeInvalid", "unbalanced parentheses")
    return maximum


def split_args(text: str) -> list[str]:
    result, start, depth, quoted, i = [], 0, 0, False, 0
    while i < len(text):
        ch = text[i]
        if ch == "'":
            if quoted and i + 1 < len(text) and text[i + 1] == "'":
                i += 2
                continue
            quoted = not quoted
        elif not quoted:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            elif ch == "," and depth == 0:
                result.append(text[start:i].strip())
                start = i + 1
        i += 1
    if quoted or depth != 0:
        raise InvalidEvidence("Preparation.FixtureTokenShapeInvalid", text)
    if text:
        result.append(text[start:].strip())
    return result


@dataclass(frozen=True)
class Record:
    occurrence: int
    entity_id: int
    kind: str
    args: tuple[str, ...]
    raw: str
    start: int
    end: int
    references: tuple[int, ...]


def inventory(data: bytes, record_limit: int = 1000, depth_limit: int = 16) -> list[Record]:
    if len(data) > 1_000_000 or b"\r" in data or data.startswith(b"\xef\xbb\xbf"):
        raise InvalidEvidence("Preparation.FixtureEncodingOrLimit", len(data))
    try:
        data.decode("ascii")
    except UnicodeDecodeError as exc:
        raise InvalidEvidence("Preparation.FixtureTextProfile", str(exc)) from exc
    if data.count(b"\nDATA;\n") != 1 or not data.endswith(b"ENDSEC;\nEND-ISO-10303-21;\n"):
        raise InvalidEvidence("Preparation.FixtureEnvelopeInvalid", "expected frozen single-DATA layout")
    begin = data.index(b"\nDATA;\n") + len(b"\nDATA;\n")
    end = len(data) - len(b"ENDSEC;\nEND-ISO-10303-21;\n")
    records, offset = [], begin
    for line in data[begin:end].splitlines(keepends=True):
        match = LINE.fullmatch(line)
        if not match:
            raise InvalidEvidence("Preparation.FixtureRecordShapeInvalid", offset)
        raw = match[2].decode("ascii")
        if nesting_depth(raw) > depth_limit:
            raise InvalidEvidence("Preparation.DepthLimit", depth_limit)
        simple = SIMPLE.fullmatch(raw)
        # Reference extraction excludes strings, including doubled apostrophes.
        unquoted = re.sub(r"'(?:[^']|'')*'", "''", raw)
        refs = tuple(int(value) for value in re.findall(r"#([0-9]+)", unquoted))
        records.append(Record(len(records) + 1, int(match[1]), simple[1] if simple else "COMPLEX",
                              tuple(split_args(simple[2])) if simple else (), raw, offset, offset + len(line), refs))
        offset += len(line)
        if len(records) > record_limit:
            raise InvalidEvidence("Preparation.RecordLimit", record_limit)
    return records


def by_id(records: list[Record]) -> dict[int, Record]:
    result = {}
    for record in records:
        if record.entity_id in result:
            raise InvalidEvidence("Oracle.AmbiguousEntityIdentity", record.entity_id)
        result[record.entity_id] = record
    return result


def ref(token: str) -> int:
    if not re.fullmatch(r"#[0-9]+", token):
        raise InvalidEvidence("Preparation.FixtureReferenceToken", token)
    return int(token[1:])


def aggregate(token: str) -> list[str]:
    if not token.startswith("(") or not token.endswith(")"):
        raise InvalidEvidence("Preparation.FixtureAggregateToken", token)
    return split_args(token[1:-1])


def vector(token: str) -> tuple[Fraction, ...]:
    return tuple(Fraction(value) for value in aggregate(token))


def ledger(records: list[Record]) -> list[dict]:
    return [{"record_occurrence": record.occurrence, "source_id": record.entity_id,
             "kind": record.kind, "raw": record.raw, "span": [record.start, record.end],
             "references": list(record.references)} for record in records]
