#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Check pending release-note structure against the format in .changeset/README.md."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]


class FormatError(ValueError):
    """An input cannot supply the documented, deliberately limited note syntax."""

    def __init__(self, line: int, message: str):
        super().__init__(message)
        self.line = line


def front_matter(lines: list[str]) -> tuple[list[tuple[int, str, str]], int]:
    """Read literal key/value lines, preserving duplicate fields and source locations."""
    if not lines or lines[0].rstrip() != "---":
        raise FormatError(1, "front matter: the note must start with `---`")
    fields = []
    for index, line in enumerate(lines[1:], 1):
        if line.rstrip() == "---":
            return fields, index + 1
        if line.strip():
            key, separator, value = line.partition(":")
            if not separator:
                raise FormatError(index + 1, "front matter: expected `key: value`")
            fields.append((index + 1, key.strip(), value.strip()))
    raise FormatError(1, "front matter: missing closing `---`")


def headings(lines: list[str], start: int = 0) -> list[tuple[int, str]]:
    """Find level-two headings outside matching backtick or tilde fences."""
    found = []
    fence = None
    for index in range(start, len(lines)):
        line = lines[index]
        marker = re.match(r"^ {0,3}(`{3,}|~{3,})(.*)$", line)
        if fence:
            # A different fence character or shorter run is code, not a close.
            if marker and marker[1][0] == fence[0] and len(marker[1]) >= len(fence) and not marker[2].strip():
                fence = None
        elif marker:
            fence = marker[1]
        elif line.startswith("## "):
            found.append((index, line[3:].rstrip()))
    return found


@dataclass(frozen=True)
class NoteFormat:
    key: str
    values: tuple[str, ...]
    sections: tuple[str, ...]
    template: str
    template_line: int

    @classmethod
    def from_readme(cls, text: str) -> NoteFormat:
        # README is the format authority. Missing/ambiguous rules are errors,
        # never a reason to fall back to a second hard-coded schema.
        lines = text.lstrip("\ufeff").splitlines()
        starts = [index for index, line in enumerate(lines) if line.rstrip() == "```md"]
        if len(starts) != 1:
            raise FormatError(1, "format: expected exactly one ```md template")
        start = starts[0] + 1
        end = next((index for index in range(start, len(lines)) if lines[index].rstrip() == "```"), None)
        if end is None:
            raise FormatError(start, "format: the template fence is not closed")
        template = lines[start:end]
        try:
            fields, close = front_matter(template)
        except FormatError as error:
            raise FormatError(start + error.line, f"format template: {error}") from error
        if len(fields) != 1 or not re.fullmatch(r"[a-z][a-z0-9_-]*", fields[0][1]):
            raise FormatError(start + 1, "format: the template must declare exactly one field")
        key = fields[0][1]
        sections = tuple(title for _, title in headings(template, close))
        if not sections or not all(sections) or len(set(sections)) != len(sections):
            raise FormatError(start + 1, "format: expected unique, nonempty `## ` sections")
        declarations = [
            (index, line[len(f"`{key}` is ") :].partition(".")[0])
            for index, line in enumerate(lines)
            if line.startswith(f"`{key}` is ")
        ]
        if len(declarations) != 1:
            raise FormatError(1, f"format: expected one sentence beginning with `{key}` is")
        declaration_line, declaration = declarations[0]
        values = tuple(re.findall(r"`([^`]+)`", declaration))
        if not values or len(set(values)) != len(values):
            raise FormatError(declaration_line + 1, "format: expected unique backtick-quoted allowed values")
        return cls(key, values, sections, "\n".join(template), start + 1)

    def check_text(self, text: str) -> list[tuple[int, str]]:
        lines = text.lstrip("\ufeff").splitlines()
        try:
            fields, close = front_matter(lines)
        except FormatError as error:
            return [(error.line, str(error))]
        issues = []
        seen = False
        for line, key, value in fields:
            if key != self.key:
                issues.append((line, f"front matter: unknown field `{key}`; expected only `{self.key}`"))
            elif seen:
                issues.append((line, f"front matter: duplicate `{key}` field"))
            else:
                seen = True
                if value not in self.values:
                    choices = ", ".join(self.values)
                    issues.append((line, f"front matter: `{key}: {value}` is not allowed; choose {choices}"))
        if not seen:
            issues.append((close, f"front matter: missing `{self.key}` field"))
        titles = headings(lines, close)
        first = titles[0][0] if titles else len(lines)
        if not any(line.strip() for line in lines[close:first]):
            issues.append((min(first + 1, len(lines)), "description: expected text before the first section"))
        next_section = 0
        for position, (index, title) in enumerate(titles):
            if title not in self.sections:
                issues.append((index + 1, f"sections: unexpected `## {title}`"))
                continue
            expected = self.sections.index(title)
            if expected < next_section:
                issues.append((index + 1, f"sections: `## {title}` is duplicated or out of order"))
                continue
            for missing in self.sections[next_section:expected]:
                issues.append((index + 1, f"sections: missing `## {missing}` before `## {title}`"))
            next_section = expected + 1
            end = titles[position + 1][0] if position + 1 < len(titles) else len(lines)
            if not any(line.strip() for line in lines[index + 1 : end]):
                issues.append((index + 1, f"sections: `## {title}` is empty"))
        for missing in self.sections[next_section:]:
            issues.append((max(len(lines), 1), f"sections: missing `## {missing}`"))
        return sorted(issues, key=lambda issue: issue[0])


def report(root: Path) -> tuple[int, str]:
    """Return 0 (valid), 1 (violations), or 2 (unreadable input/format), without writes."""
    directory = root / ".changeset"
    path = directory / "README.md"
    try:
        note_format = NoteFormat.from_readme(path.read_text(encoding="utf-8-sig"))
        issues = [
            f".changeset/README.md:{note_format.template_line + line - 1}: {message}"
            for line, message in note_format.check_text(note_format.template)
        ]
        # Include the bare `.md` name so it is diagnosed as an invalid note name,
        # rather than disappearing because pathlib treats it as extensionless.
        notes = sorted(
            path
            for path in directory.iterdir()
            if path.is_file() and path.name.endswith(".md") and path.name != "README.md"
        )
        for path in notes:
            violations = note_format.check_text(path.read_text(encoding="utf-8-sig"))
            if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*\.md", path.name):
                violations.append((1, "file name: expected lowercase kebab-case `.md`"))
            issues.extend(f".changeset/{path.name}:{line}: {message}" for line, message in sorted(violations))
    except FormatError as error:
        return 2, f".changeset/README.md:{error.line}: {error}\n"
    except (OSError, UnicodeError) as error:
        return 2, f"{path}: cannot read changeset input: {error}\n"
    if issues:
        return 1, "\n".join([*issues, f"changesets: {len(issues)} problem(s); format: .changeset/README.md"]) + "\n"
    return 0, f"changesets: {len(notes)} pending note(s) and the README template match .changeset/README.md\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("check",))
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root (defaults to this script's checkout)")
    args = parser.parse_args(argv)
    status, message = report(args.root)
    print(message, end="", file=sys.stdout if status == 0 else sys.stderr)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
