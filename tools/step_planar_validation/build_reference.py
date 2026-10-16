# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Development-only extraction from the pinned EXPRESS, never from the implementation output."""
from __future__ import annotations

import argparse
from pathlib import Path
import re

from tools.step_import_validation.common import digest, load_json, write_json
from tools.step_semantic_validation.oracle import REFERENCE as DECLARATIONS

SCHEMA_SHA = "c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088"


def extract(path):
    raw = path.read_bytes()
    if digest(raw) != SCHEMA_SHA:
        raise ValueError("Unrecognised schema bytes")
    text = raw.decode("utf-8")
    entities = {}
    declarations = load_json(DECLARATIONS)
    for name in declarations["entities"]:
        match = re.search(r"(?im)^ENTITY " + name + r"\b[\s\S]*?END_ENTITY;", text)
        body = match[0]
        sections = {}
        starts = list(re.finditer(r"(?m)^\s*(DERIVE|INVERSE|UNIQUE|WHERE)\s*$", body))
        for i, start in enumerate(starts):
            end = starts[i + 1].start() if i + 1 < len(starts) else body.index("END_ENTITY;")
            chunk = body[start.end():end]
            labels = []
            pattern = r"(?m)^\s*(\w+)\s*:"
            for clause in re.finditer(pattern, chunk):
                offset = match.start() + start.end() + clause.start(1)
                labels.append({"name": clause[1], "line": text.count("\n", 0, offset) + 1})
            sections[start[1]] = {"clauses": labels, "text": chunk.strip()}
        entities[name] = {"line": text.count("\n", 0, match.start()) + 1, "sections": sections}
    rules = []
    for match in re.finditer(r"(?im)^RULE\s+(\w+)\s+FOR\s*\(([^;]+)\);([\s\S]*?)END_RULE;", text):
        rules.append({"name": match[1], "line": text.count("\n", 0, match.start()) + 1,
                      "for": [s.strip().upper() for s in match[2].split(",")],
                      "text": match[0], "sha256": digest(match[0].encode())})
    if len(rules) != 47:
        raise ValueError("Incomplete global rule population")
    types = {}
    for name in ["DIMENSION_COUNT", "NON_NEGATIVE_LENGTH_MEASURE", "POSITIVE_LENGTH_MEASURE"]:
        match = re.search(r"(?im)^TYPE " + name + r"\b[\s\S]*?END_TYPE;", text)
        types[name] = {"line": text.count("\n", 0, match.start()) + 1, "text": match[0]}
    return {"format": "n2635-planar-rule-witness.v1", "schema_sha256": SCHEMA_SHA,
            "entities": entities, "rules": rules, "types": types,
            "provenance": declarations["source"] if "source" in declarations else "Pinned N2635 EXPRESS bytes"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", required=True, type=Path)
    parser.add_argument("--new-output", required=True, type=Path)
    args = parser.parse_args()
    if args.new_output.exists():
        raise ValueError("A witness revision must not be overwritten")
    write_json(args.new_output, extract(args.schema))
