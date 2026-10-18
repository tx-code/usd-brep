# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Pending-note contracts: README authority, diagnostics, CLI outcomes, and read-only checks."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.agent import changesets


README = (changesets.ROOT / ".changeset/README.md").read_text(encoding="utf-8")
NOTE = """---
change: fix
---

Cut preserves the source model when invoked on a copy.

## Scope

Maintenance

## Integrator action

None.

## Compatibility

No contract change.
"""


class ChangesetTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="smlib changesets ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.directory = self.root / ".changeset"
        self.directory.mkdir()
        self.write("README.md", README)

    def write(self, name, text):
        (self.directory / name).write_text(text, encoding="utf-8", newline="")

    def assert_violation(self, text, message, name="cut-copy.md"):
        self.write(name, text)
        status, output = changesets.report(self.root)
        self.assertEqual(1, status, output)
        self.assertIn(f".changeset/{name}:", output)
        self.assertIn(message, output)
        return output

    def test_empty_pending_directory_and_each_documented_change_kind_pass(self):
        status, output = changesets.report(self.root)
        self.assertEqual(0, status, output)
        self.assertIn("0 pending note(s)", output)
        for kind in ("fix", "feature", "breaking"):
            with self.subTest(kind=kind):
                self.write("cut-copy.md", NOTE.replace("change: fix", f"change: {kind}"))
                status, output = changesets.report(self.root)
                self.assertEqual(0, status, output)
                self.assertIn("1 pending note(s)", output)

    def test_metadata_diagnostics_preserve_the_original_line(self):
        cases = (
            ("change: patch", ":2: front matter: `change: patch` is not allowed"),
            ("change: 'fix'", ":2: front matter: `change: 'fix'` is not allowed"),
            ("change: fix\nchange: feature", ":3: front matter: duplicate `change`"),
            ("kind: fix", ":2: front matter: unknown field `kind`"),
            ("change fix", ":2: front matter: expected `key: value`"),
            ("", "front matter: missing `change`"),
        )
        for metadata, message in cases:
            with self.subTest(metadata=metadata):
                self.assert_violation(NOTE.replace("change: fix", metadata), message)
        self.assert_violation(NOTE.removeprefix("---\n"), ":1: front matter: the note must start")
        self.assert_violation("---\nchange: fix\n", ":1: front matter: missing closing")

    def test_the_description_cannot_be_replaced_by_sections(self):
        text = NOTE.replace("Cut preserves the source model when invoked on a copy.\n", "")
        self.assert_violation(text, "description: expected text before the first section")

    def test_sections_are_required_once_in_order_with_content(self):
        cases = (
            (NOTE.replace("\n## Scope\n\nMaintenance\n", ""), "missing `## Scope`"),
            (NOTE.replace("\n## Compatibility\n\nNo contract change.\n", ""), "missing `## Compatibility`"),
            (NOTE.replace("None.\n", " \n"), "`## Integrator action` is empty"),
            (NOTE + "\n## Scope\n\nMaintenance\n", "`## Scope` is duplicated or out of order"),
            (NOTE + "\n## Extra\n\nText\n", "unexpected `## Extra`"),
            (NOTE.replace("## Scope", "## Compatibility", 1), "missing `## Scope`"),
            (NOTE.replace("## Scope", "## scope", 1), "unexpected `## scope`"),
        )
        for text, message in cases:
            with self.subTest(message=message):
                self.assert_violation(text, message)

    def test_code_fences_preserve_embedded_headings_as_content(self):
        examples = (
            "```md\n## Extra\n```",
            "~~~text\n## Extra\n~~~",
            "````md\n```\n## Extra\n~~~\n````",
        )
        for example in examples:
            with self.subTest(example=example):
                self.write("cut-copy.md", NOTE.replace("None.", f"Use:\n\n{example}"))
                status, output = changesets.report(self.root)
                self.assertEqual(0, status, output)

    def test_crlf_and_bom_are_accepted(self):
        (self.directory / "cut-copy.md").write_bytes(("\ufeff" + NOTE.replace("\n", "\r\n")).encode("utf-8"))
        (self.directory / "README.md").write_bytes(("\ufeff" + README.replace("\n", "\r\n")).encode("utf-8"))
        status, output = changesets.report(self.root)
        self.assertEqual(0, status, output)

    def test_file_names_must_use_the_documented_kebab_case(self):
        for name in ("Cut-copy.md", "cut_copy.md", "cut--copy.md", "-cut.md", ".md"):
            with self.subTest(name=name):
                self.assert_violation(NOTE, ":1: file name: expected lowercase kebab-case", name=name)
                (self.directory / name).unlink()
        self.write("cut-copy-2.md", NOTE)
        self.assertEqual(0, changesets.report(self.root)[0])

    def test_the_readme_is_the_only_schema_authority(self):
        alternate = """# Format

```md
---
kind: small
---

Describe the change.

## Action

None.
```

`kind` is `small` or `large`.
"""
        self.write("README.md", alternate)
        self.write("a-note.md", "---\nkind: large\n---\n\nText.\n\n## Action\n\nNone.\n")
        self.assertEqual(0, changesets.report(self.root)[0])
        self.assert_violation(NOTE, "unknown field `change`")

    def test_ambiguous_or_unreadable_format_never_falls_back(self):
        cases = (
            README.replace("```md", "```text"),
            README + "\n```md\nsecond template\n```\n",
            README.replace("`change` is `fix`, `feature` or `breaking`.", "No values declared."),
            README.replace("`fix`, `feature` or `breaking`", "`fix`, `fix`"),
            README.replace("change: fix\n", "change: fix\nother: value\n", 1),
            README.replace("## Compatibility", "## Scope", 1),
            "# Format\n```md\n---\nchange: fix\n---\n",
        )
        for readme in cases:
            with self.subTest(readme=readme):
                self.write("README.md", readme)
                status, output = changesets.report(self.root)
                self.assertEqual(2, status, output)
                self.assertIn(".changeset/README.md:", output)

    def test_template_value_drift_is_reported_at_its_readme_line(self):
        self.write("README.md", README.replace("change: fix\n", "change: patch\n", 1))
        status, output = changesets.report(self.root)
        self.assertEqual(1, status, output)
        line = README[: README.index("change: fix")].count("\n") + 1
        self.assertIn(f".changeset/README.md:{line}: front matter:", output)

    def test_missing_input_and_invalid_utf8_are_check_failures(self):
        (self.directory / "README.md").unlink()
        self.assertEqual(2, changesets.report(self.root)[0])
        self.write("README.md", README)
        (self.directory / "broken.md").write_bytes(b"\xff")
        status, output = changesets.report(self.root)
        self.assertEqual(2, status, output)
        self.assertIn("broken.md", output)

    def test_multiple_notes_are_checked_in_deterministic_file_order(self):
        self.write("z-note.md", NOTE.replace("change: fix", "change: patch"))
        self.write("a-note.md", "---\nchange: fix\n---\n\nText only.\n")
        status, output = changesets.report(self.root)
        self.assertEqual(1, status, output)
        self.assertLess(output.index(".changeset/a-note.md:"), output.index(".changeset/z-note.md:"))
        self.assertIn("missing `## Scope`", output)
        self.assertIn("missing `## Integrator action`", output)
        self.assertIn("missing `## Compatibility`", output)

    def test_check_is_read_only_and_does_not_consume_notes_or_change_versions(self):
        self.write("cut-copy.md", NOTE)
        (self.root / "VERSION").write_text("1.0.0\n", encoding="utf-8")
        (self.root / "CHANGELOG.md").write_text("Published history.\n", encoding="utf-8")
        before = {path: path.read_bytes() for path in self.root.rglob("*") if path.is_file()}
        self.assertEqual(0, changesets.report(self.root)[0])
        self.assertEqual(before, {path: path.read_bytes() for path in self.root.rglob("*") if path.is_file()})

    def test_cli_runs_without_sdk_or_working_directory_assumptions(self):
        for expected in (0, 1, 2):
            with self.subTest(status=expected):
                if expected == 1:
                    self.write("cut-copy.md", NOTE.replace("change: fix", "change: patch"))
                elif expected == 2:
                    (self.directory / "README.md").unlink()
                result = subprocess.run(
                    [sys.executable, str(Path(changesets.__file__).resolve()), "check", "--root", str(self.root)],
                    cwd=self.root,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(expected, result.returncode, result.stdout + result.stderr)
                self.assertTrue(result.stdout if expected == 0 else result.stderr)


if __name__ == "__main__":
    unittest.main()
