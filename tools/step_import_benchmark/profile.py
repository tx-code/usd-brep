# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Read-only lexical census. It is not a STEP conformance or geometric validator."""
from collections import Counter, defaultdict
import hashlib
import json
import mmap
import os
from pathlib import Path
import re

_LITERAL = rb"'(?:''|[^'])*'"
_COMMENT = rb"/\*.*?\*/"
_HEAD = re.compile(_COMMENT + rb"|(?P<string>" + _LITERAL + rb")|#\s*[0-9]+\s*=\s*(?:"
                   rb"(?P<simple>[A-Z_][A-Z0-9_]*)\s*\(|(?P<complex>\())", re.S | re.I)
_END = re.compile(_COMMENT + rb"|" + _LITERAL + rb"|(?P<end>;)", re.S)
_FACET = re.compile(_COMMENT + rb"|" + _LITERAL + rb"|(?P<name>[A-Z_][A-Z0-9_]*)\s*\(|(?P<bracket>[()])", re.S | re.I)
_SCHEMA = re.compile(rb"\bFILE_SCHEMA\s*\(\s*\((.*?)\)\s*\)\s*;", re.S | re.I)
SIZE_EDGES = (65536, 262144, 1048576, 4194304, 16777216, 67108864)


def write_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def _complex_names(data, start):
    end = next((m.end() for m in _END.finditer(data, start) if m.group("end")), None)
    if end is None:
        raise ValueError("Unterminated lexical complex record")
    depth = 0
    names = []
    for token in _FACET.finditer(data, start, end):
        if token.group("name"):
            if depth == 0:
                names.append(token.group("name").decode("ascii").upper())
            depth += 1
        elif token.group("bracket") == b"(":
            depth += 1
        elif token.group("bracket") == b")":
            depth -= 1
            if depth == -1:
                break
    return names


def inspect_file(path, root):
    initial = path.stat()
    counts = Counter()
    complex_records = multiline_strings = records = 0
    with path.open("rb") as stream:
        if initial.st_size:
            with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
                sha = hashlib.sha256(data).hexdigest()
                header = data[:131072]
                schema = _SCHEMA.search(header)
                schemas = [m[1:-1].replace(b"''", b"'").decode("latin-1")
                           for m in re.findall(_LITERAL, schema[1])] if schema else []
                for token in _HEAD.finditer(data):
                    if token.group("simple"):
                        records += 1
                        counts[token.group("simple").decode("ascii").upper()] += 1
                    elif token.group("complex"):
                        records += 1
                        complex_records += 1
                        counts.update(_complex_names(data, token.end()))
                    elif token.group("string"):
                        text = token.group("string")
                        multiline_strings += b"\n" in text or b"\r" in text
        else:
            sha, schemas = hashlib.sha256(b"").hexdigest(), []
    final = path.stat()
    if (initial.st_size, initial.st_mtime_ns) != (final.st_size, final.st_mtime_ns):
        raise ValueError("Input changed during census")
    return dict(relative_path=path.relative_to(root).as_posix(), bytes=initial.st_size, sha256=sha,
                mtime_ns=initial.st_mtime_ns, schemas=schemas,
                source_directory=path.parent.relative_to(root).as_posix(),
                size_bucket=sum(initial.st_size > edge for edge in SIZE_EDGES), entity_records=records,
                complex_records=complex_records, multiline_string_literals=multiline_strings,
                entity_components=dict(counts))


def census(root, output, progress=print):
    root, output = Path(root).resolve(strict=True), Path(output)
    files = []
    skipped = []
    for directory, dirs, names in os.walk(root, followlinks=False):
        def reparse(path):
            return path.is_symlink() or bool(getattr(path.stat(follow_symlinks=False), "st_file_attributes", 0) & 0x400)
        dirs[:] = sorted(name for name in dirs if not reparse(Path(directory)/name))
        for name in sorted(names):
            path = Path(directory)/name
            if path.suffix.lower() not in {".step", ".stp"}:
                continue
            if reparse(path):
                skipped.append(str(path))
            else:
                files.append(path)
    rows, errors = [], []
    for i, path in enumerate(sorted(files), 1):
        try:
            rows.append(inspect_file(path, root))
        except (OSError, ValueError) as error:
            errors.append(dict(relative_path=path.relative_to(root).as_posix(), error=str(error)))
        if i % 100 == 0 or i == len(files):
            progress(f"Lexical census {i}/{len(files)}")
            write_json(output.with_name("census-progress.json"), dict(completed=i, total=len(files), errors=errors))
    component_files, components, schema_files = Counter(), Counter(), Counter()
    for row in rows:
        components.update(row["entity_components"])
        component_files.update(row["entity_components"].keys())
        schema_files.update(row["schemas"] or ["Unidentified"])
    report = dict(format="step-library-census.v1", root=str(root), files=rows, errors=errors, skipped_reparse=skipped,
                  scope="Lexical census only; strings/comments excluded from record heads; no schema or validity claim",
                  summary=dict(discovered_files=len(files), profiled_files=len(rows),
                               total_bytes=sum(r["bytes"] for r in rows),
                               unique_contents=len({r["sha256"] for r in rows}), schema_files=dict(schema_files),
                               multiline_string_files=sum(r["multiline_string_literals"] > 0 for r in rows),
                               entity_component_instances=dict(components.most_common()),
                               entity_component_files=dict(component_files.most_common())))
    write_json(output, report)
    return report


def select_cohorts(report, development=60, holdout=20, large=8):
    unique = {}
    for row in sorted(report["files"], key=lambda r: r["relative_path"]):
        unique.setdefault(row["sha256"], row)
    large_rows = sorted((r for r in unique.values() if r["bytes"] > SIZE_EDGES[-1]),
                        key=lambda r: (r["bytes"], r["sha256"]))
    selected_large = []
    if large_rows:
        count = min(large, len(large_rows))
        selected_large = [large_rows[(len(large_rows)-1)*i//max(count-1, 1)] for i in range(count)]
    strata = defaultdict(list)
    for row in unique.values():
        if row["bytes"] <= SIZE_EDGES[-1]:
            strata[(row["source_directory"], tuple(row["schemas"]), row["size_bucket"])].append(row)
    for rows in strata.values():
        rows.sort(key=lambda r: hashlib.sha256(("step-benchmark-v1:"+r["sha256"]).encode()).hexdigest())
    ordered = []
    while any(strata.values()):
        for key in sorted(strata):
            if strata[key]:
                ordered.append(strata[key].pop())
    # Holdout hashes, including duplicate paths, are excluded from every native/reader experiment.
    blind = ordered[:holdout]
    dev = ordered[holdout:holdout+development]
    return dict(format="step-library-cohorts.v1", seed="step-benchmark-v1", unit="distinct content SHA256",
                development=dev, holdout=blind, large=selected_large,
                policy="Holdout excluded from reader/import experiments; lexical census is not geometric tuning")
