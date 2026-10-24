# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Release preparation through real temporary Git repositories and the CLI."""

from pathlib import Path
import copy
import json
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.agent import release
from tools.agent.tests.test_changesets import NOTE, README


ROOT = Path(__file__).resolve().parents[3]


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="fork release ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.git("init", "-q")
        self.write("VERSION", "1.0.1\n")
        self.history = "# Changelog\n\n## [1.0.1] - 2026-10-21\n\nUpstream history stays intact.\n"
        self.write("CHANGELOG.md", self.history)
        self.write(".changeset/README.md", README)
        self.base = self.commit("base")
        self.write(".changeset/fix-one.md", NOTE)
        self.write(".changeset/feature-two.md", NOTE.replace("change: fix", "change: feature"))
        self.cut = self.commit("integrated changes")

    def git(self, *args):
        return subprocess.check_output(
            ["git", "-C", str(self.root), *args], text=True, stderr=subprocess.PIPE
        ).strip()

    def write(self, name, value):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value, encoding="utf-8", newline="")

    def commit(self, message):
        self.git("add", ".")
        self.git("-c", "user.name=Release Test", "-c", "user.email=release@example.invalid", "commit", "-qm", message)
        return self.git("rev-parse", "HEAD")

    def plan(self):
        return release.plan(self.root, "2.0.0-alpha.1", "2026-10-24", self.cut, self.base)

    def snapshot(self):
        return {
            p.relative_to(self.root).as_posix(): p.read_bytes()
            for p in self.root.rglob("*") if p.is_file() and ".git" not in p.parts
        }

    def test_plan_is_read_only_pinned_and_covers_integrated_notes(self):
        before = self.snapshot()
        plan = self.plan()
        self.assertEqual(before, self.snapshot())
        self.assertEqual(self.cut, plan["source_commit"])
        self.assertEqual(
            {".changeset/fix-one.md", ".changeset/feature-two.md"},
            {n["path"] for n in plan["notes"]},
        )
        self.assertEqual("2.0.0a1", plan["wheel_version"])

    def test_prepare_consumes_only_pinned_notes_and_preserves_new_parallel_notes(self):
        plan = self.plan()
        self.write(".changeset/new-parallel.md", NOTE)
        self.write("parallel.cpp", "independent edit\n")
        release.prepare(self.root, plan)
        self.assertEqual("2.0.0-alpha.1\n", (self.root / "VERSION").read_text())
        self.assertFalse((self.root / ".changeset/fix-one.md").exists())
        self.assertTrue((self.root / ".changeset/new-parallel.md").exists())
        self.assertEqual("independent edit\n", (self.root / "parallel.cpp").read_text())
        self.assertTrue(
            (self.root / "CHANGELOG.md").read_text().endswith(self.history.removeprefix("# Changelog\n"))
        )
        self.assertEqual(self.cut, self.git("rev-parse", "HEAD"))
        self.assertEqual("", self.git("tag"))
        self.assertFalse((self.root / "RELEASE_MANIFEST.json").exists())
        self.assertIn("Prepared 2026-10-24", (self.root / "RELEASE_NOTES.md").read_text())
        self.assertIn("2.0.0-alpha.1", release.check(self.root))
        self.assertIn("2 consumed", release.check(self.root, plan))

    def test_changed_selected_note_refuses_without_any_writes(self):
        plan = self.plan()
        self.write(".changeset/fix-one.md", NOTE + "\nConcurrent correction.\n")
        before = self.snapshot()
        with self.assertRaisesRegex(release.ReleaseError, "changed"):
            release.prepare(self.root, plan)
        self.assertEqual(before, self.snapshot())

    def test_changed_version_or_history_refuses_without_consumption(self):
        for path in ("VERSION", "CHANGELOG.md"):
            with self.subTest(path=path):
                plan = self.plan()
                old = (self.root / path).read_bytes()
                self.write(path, "parallel modification\n")
                before = self.snapshot()
                with self.assertRaises(release.ReleaseError):
                    release.prepare(self.root, plan)
                self.assertEqual(before, self.snapshot())
                (self.root / path).write_bytes(old)

    def test_cut_cannot_silently_move_after_plan(self):
        plan = self.plan()
        self.write("later.cpp", "later commit\n")
        self.commit("later")
        before = self.snapshot()
        with self.assertRaisesRegex(release.ReleaseError, "HEAD"):
            release.prepare(self.root, plan)
        self.assertEqual(before, self.snapshot())

    def test_every_note_needs_exactly_one_summary_or_explicit_deferral(self):
        original = self.plan()
        for corrupt in ("unassigned", "duplicate", "path_escape", "forged_hash"):
            with self.subTest(corrupt=corrupt):
                plan = copy.deepcopy(original)
                if corrupt == "unassigned":
                    plan["entries"].pop()
                elif corrupt == "duplicate":
                    plan["entries"].append(copy.deepcopy(plan["entries"][0]))
                elif corrupt == "path_escape":
                    plan["notes"][0]["path"] = "../outside.md"
                else:
                    plan["notes"][0]["sha256"] = "0" * 64
                before = self.snapshot()
                with self.assertRaises(release.ReleaseError):
                    release.prepare(self.root, plan)
                self.assertEqual(before, self.snapshot())

    def test_deferred_note_stays_pending_and_is_recorded(self):
        plan = self.plan()
        entry = plan["entries"].pop()
        plan["deferred"].append({"path": entry["notes"][0], "reason": "Outside this preview's accepted scope."})
        release.prepare(self.root, plan)
        self.assertTrue((self.root / entry["notes"][0]).exists())
        self.assertIn("2.0.0-alpha.1", release.check(self.root))
        self.assertIn("1 deferred", release.check(self.root, plan))

    def test_fenced_markdown_examples_do_not_truncate_note_descriptions(self):
        example = "```md\n## Scope\nExample heading, not a note section.\n```"
        self.write(".changeset/fix-one.md", NOTE.replace("## Scope", example + "\n\n## Scope", 1))
        self.cut = self.commit("note with a fenced example")
        plan = self.plan()
        note = next(item for item in plan["notes"] if item["path"] == ".changeset/fix-one.md")
        self.assertIn(example, note["summary"])
        self.assertEqual("Maintenance", note["scope"])
        self.assertEqual("None.", note["action"])

    def test_prepare_is_not_repeatable_and_modified_records_fail_check(self):
        plan = self.plan()
        release.prepare(self.root, plan)
        before = self.snapshot()
        with self.assertRaises(release.ReleaseError):
            release.prepare(self.root, plan)
        self.assertEqual(before, self.snapshot())
        self.write("RELEASE_NOTES.md", "truncated\n")
        with self.assertRaises(release.ReleaseError):
            release.check(self.root)

    def test_markdown_check_detects_inconsistent_release_records_without_json(self):
        plan = self.plan()
        release.prepare(self.root, plan)
        paths = ("VERSION", "CHANGELOG.md", "RELEASE_NOTES.md")
        original = {name: (self.root / name).read_text() for name in paths}
        cases = (
            ("VERSION", "2.0.0-alpha.2\n"),
            ("RELEASE_NOTES.md", original["RELEASE_NOTES.md"].replace("Prepared 2026-10-24", "Prepared 2026-10-23")),
            ("RELEASE_NOTES.md", original["RELEASE_NOTES.md"].replace("## Qualification limits", "## Other limits")),
            ("CHANGELOG.md", original["CHANGELOG.md"].replace("Upstream history stays intact.", "Edited old history.")),
            ("CHANGELOG.md", original["CHANGELOG.md"].replace(f"{self.base}..{self.cut}", f"{self.base}..{self.base}")),
        )
        for name, modified in cases:
            with self.subTest(name=name):
                self.write(name, modified)
                with self.assertRaises(release.ReleaseError):
                    release.check(self.root)
                self.write(name, original[name])
        self.assertIn("records consistent", release.check(self.root))

    def test_optional_local_plan_checks_exact_rendering_without_persisted_ledger(self):
        plan = self.plan()
        release.prepare(self.root, plan)
        notes = (self.root / "RELEASE_NOTES.md").read_text()
        self.write("RELEASE_NOTES.md", notes.replace(plan["summary"], "Reviewed Markdown correction."))
        self.assertIn("records consistent", release.check(self.root))
        with self.assertRaisesRegex(release.ReleaseError, "record changed"):
            release.check(self.root, plan)
        self.assertFalse((self.root / "RELEASE_MANIFEST.json").exists())

    def test_failed_file_replacement_restores_owned_release_inputs(self):
        plan = self.plan()
        before = self.snapshot()
        original = release.os.replace
        calls = 0

        def replace(source, destination):
            nonlocal calls
            calls += 1
            if calls == 3:
                raise PermissionError("synthetic filesystem refusal")
            return original(source, destination)

        with patch("os.replace", side_effect=replace), self.assertRaises(PermissionError):
            release.prepare(self.root, plan)
        self.assertEqual(before, self.snapshot())

    def test_failed_note_deletion_restores_already_consumed_notes(self):
        plan = self.plan()
        before = self.snapshot()
        original = Path.unlink
        failed = False

        def unlink(path, *args, **kwargs):
            nonlocal failed
            if path == self.root / ".changeset/fix-one.md" and not failed:
                failed = True
                raise PermissionError("synthetic note deletion refusal")
            return original(path, *args, **kwargs)

        with patch.object(Path, "unlink", unlink), self.assertRaises(PermissionError):
            release.prepare(self.root, plan)
        self.assertEqual(before, self.snapshot())

    def test_cli_plan_prepare_and_check_never_publish_or_commit(self):
        script = ROOT / "tools/agent/release.py"
        path = self.root / "plan.json"
        command = [sys.executable, str(script), "--root", str(self.root)]
        subprocess.run(
            [*command, "plan", "--version", "2.0.0-alpha.1", "--date", "2026-10-24",
             "--ref", self.cut, "--base", self.base, "--output", str(path)],
            check=True, capture_output=True,
        )
        subprocess.run([*command, "prepare", "--plan", str(path)], check=True, capture_output=True)
        subprocess.run([*command, "check"], check=True, capture_output=True)
        subprocess.run([*command, "check", "--plan", str(path)], check=True, capture_output=True)
        self.assertEqual(self.cut, self.git("rev-parse", "HEAD"))
        self.assertEqual("", self.git("tag"))


if __name__ == "__main__":
    unittest.main()
