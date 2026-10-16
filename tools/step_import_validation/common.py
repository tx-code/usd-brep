# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Small deterministic file primitives shared by evidence producers/consumers."""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path


class InvalidEvidence(ValueError):
    def __init__(self, code: str, detail: object):
        self.code = code
        self.detail = detail
        super().__init__(f"{code}: {detail}")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=4, sort_keys=True, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(json_bytes(value))


def _unique_object(pairs: list[tuple]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise InvalidEvidence("Contract.DuplicateJsonKey", key)
        result[key] = value
    return result


def load_json(path: Path) -> object:
    if not path.is_file():
        raise InvalidEvidence("Evidence.RequiredArtifactMissing", str(path))
    if path.stat().st_size > 10_000_000:
        raise InvalidEvidence("Contract.MetadataByteLimit", str(path))
    return parse_json(path.read_bytes())


def _nonfinite(value: str):
    raise InvalidEvidence("Contract.NonFiniteJson", value)


def parse_json(data: bytes) -> object:
    try:
        result = json.loads(data, object_pairs_hook=_unique_object, parse_constant=_nonfinite)
    except (json.JSONDecodeError, UnicodeDecodeError, RecursionError) as exc:
        raise InvalidEvidence("Contract.MalformedJson", str(exc)) from exc
    pending = [(result, 0)]
    while pending:
        value, depth = pending.pop()
        if isinstance(value, float) and not math.isfinite(value):
            raise InvalidEvidence("Contract.NonFiniteJson", str(value))
        if depth > 64:
            raise InvalidEvidence("Contract.MetadataDepthLimit", depth)
        if isinstance(value, dict):
            pending.extend((item, depth + 1) for item in value.values())
        elif isinstance(value, list):
            pending.extend((item, depth + 1) for item in value)
    return result


def inside(root: Path, relative: str) -> Path:
    if not isinstance(relative, str) or not relative or Path(relative).is_absolute():
        raise InvalidEvidence("Contract.ArtifactPathInvalid", relative)
    result = (root / relative).resolve()
    if not result.is_relative_to(root.resolve()) or result == root.resolve():
        raise InvalidEvidence("Contract.ArtifactPathInvalid", relative)
    return result


def file_ref(root: Path, relative: str, role: str) -> dict:
    data = inside(root, relative).read_bytes()
    return {"path": relative, "role": role, "sha256": digest(data), "byte_length": len(data)}


def checked_bytes(root: Path, ref: dict, maximum: int = 1_000_000) -> bytes:
    path = inside(root, ref["path"])
    if not path.is_file():
        raise InvalidEvidence("Evidence.RequiredArtifactMissing", ref["path"])
    if path.stat().st_size > maximum:
        raise InvalidEvidence("Preparation.ByteLimit", ref["path"])
    data = path.read_bytes()
    if digest(data) != ref["sha256"] or len(data) != ref["byte_length"]:
        raise InvalidEvidence("Preparation.HashMismatch", ref["path"])
    return data


def tool_identity() -> dict:
    root = Path(__file__).parent
    files = {path.name: digest(path.read_bytes()) for path in sorted(root.glob("*.py"))}
    return {"files": files, "sha256": digest(json_bytes(files))}
