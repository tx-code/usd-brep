# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Plan and prepare local fork releases; never commit, tag, build or publish."""

from pathlib import Path
import argparse
import datetime
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
if __package__ in (None, ""):
    sys.path.insert(0, str(ROOT))

from tools.agent import changesets
from tools.repoman.wheel_version import repository_version


SCHEMA = 1
INPUTS = (
    "VERSION", "CHANGELOG.md", "RELEASE_NOTES.md", "RELEASE_MANIFEST.json",
    ".changeset/README.md", "CMakeLists.txt", "cmake/RepositoryVersion.cmake",
    "tools/repoman/wheel_version.py",
)
NOTE_PATH = re.compile(r"\.changeset/[a-z0-9]+(?:-[a-z0-9]+)*\.md")
NOTE_KEYS = ("path", "blob", "sha256", "change")


class ReleaseError(ValueError):
    pass


def _git(root, *args, input_bytes=None, missing=False):
    result = subprocess.run(
        ["git", "-C", str(root), *args], input=input_bytes, capture_output=True
    )
    if result.returncode:
        if missing:
            return None
        raise ReleaseError(result.stderr.decode("utf-8", errors="replace").strip())
    return result.stdout


def _commit(root, ref):
    return _git(root, "rev-parse", "--verify", "--end-of-options", f"{ref}^{{commit}}").decode().strip()


def _at(root, ref, path):
    return _git(root, "show", f"{ref}:{path}", missing=True)


def _sha(value):
    return hashlib.sha256(value).hexdigest() if value is not None else None


def _clean(value):
    # Release inputs are repository text. Match Git's CRLF->LF normalization,
    # retaining every other byte; BOMs and content edits do not disappear.
    return value.replace(b"\r\n", b"\n") if value is not None else None


def _path(root, name):
    path = root / name
    if path.is_symlink():
        raise ReleaseError(f"Unsafe release path: {name}")
    if root.resolve() not in path.resolve().parents:
        raise ReleaseError(f"Release path escapes repository: {name}")
    if name.startswith(".changeset/") and (root / ".changeset").is_symlink():
        raise ReleaseError("The changeset directory must not be a symlink")
    return path


def _repository_root(root):
    root = Path(root).resolve()
    actual = Path(_git(root, "rev-parse", "--show-toplevel").decode().strip()).resolve()
    if actual != root:
        raise ReleaseError("--root must name the Git working-tree root")
    return root


def _current(root, name):
    path = _path(root, name)
    return path.read_bytes() if path.exists() else None


def _inventory(root, commit):
    raw = _git(root, "ls-tree", "-rz", commit, "--", ".changeset")
    rows = []
    for item in raw.split(b"\0"):
        if not item:
            continue
        metadata, path = item.split(b"\t", 1)
        name = path.decode("utf-8")
        if name == ".changeset/README.md":
            continue
        mode, kind, oid = metadata.decode().split()
        if not NOTE_PATH.fullmatch(name) or kind != "blob" or mode not in ("100644", "100755"):
            raise ReleaseError(f"Unsupported release-note input: {name}")
        rows.append((name, oid))
    readme = _at(root, commit, ".changeset/README.md")
    if readme is None:
        raise ReleaseError("The pinned changeset format is missing")
    note_format = changesets.NoteFormat.from_readme(readme.decode("utf-8-sig"))
    oids = sorted({oid for _, oid in rows})
    stream = io.BytesIO(_git(root, "cat-file", "--batch", input_bytes="".join(f"{oid}\n" for oid in oids).encode()))
    blobs = {}
    for oid in oids:
        header = stream.readline().decode().split()
        if len(header) != 3 or header[:2] != [oid, "blob"]:
            raise ReleaseError("Cannot read the pinned note objects")
        value = stream.read(int(header[2]))
        if stream.read(1) != b"\n":
            raise ReleaseError("Incomplete pinned note object")
        blobs[oid] = value
    result = []
    for name, oid in sorted(rows):
        value = blobs[oid]
        text = value.decode("utf-8-sig")
        violations = note_format.check_text(text)
        if violations:
            raise ReleaseError(f"{name}: {violations[0][1]}")
        # Reuse the owning parser: headings inside fenced examples are content.
        # A second regex parser could silently truncate a valid note's summary.
        lines = text.lstrip("\ufeff").splitlines()
        metadata, close = changesets.front_matter(lines)
        change = metadata[0][2]
        titles = changesets.headings(lines, close)
        summary = "\n".join(lines[close:titles[0][0]]).strip()
        fields = {}
        for index, (start, title) in enumerate(titles):
            end = titles[index + 1][0] if index + 1 < len(titles) else len(lines)
            fields[title] = "\n".join(lines[start + 1:end]).strip()
        result.append({
            "path": name, "blob": oid, "sha256": _sha(value), "change": change,
            "summary": summary, "scope": fields["Scope"],
            "action": fields["Integrator action"], "compatibility": fields["Compatibility"],
        })
    return result


def _version_order(version):
    parsed = repository_version(version)
    return (*map(int, parsed.core.split(".")), {"alpha": 0, "beta": 1, "rc": 2, "": 3}[parsed.stage], parsed.number)


def plan(root, version, date, ref="HEAD", base=None):
    """Read a pinned Git tree and produce an editable, complete reconciliation."""
    root = _repository_root(root)
    if base is None:
        raise ReleaseError("An explicit previous-release baseline is required")
    target = repository_version(version)
    datetime.date.fromisoformat(date)
    cut, baseline = _commit(root, ref), _commit(root, base)
    _git(root, "merge-base", "--is-ancestor", baseline, cut)
    old = _at(root, cut, "VERSION")
    if old is None or _version_order(target.version) <= _version_order(old.decode()):
        raise ReleaseError("Target version must advance the pinned VERSION")
    notes = _inventory(root, cut)
    if not notes:
        raise ReleaseError("The pinned tree has no pending notes to reconcile")
    entries = [{
        "id": Path(note["path"]).stem,
        "title": Path(note["path"]).stem.replace("-", " ").capitalize(),
        "category": {"fix": "Fixed", "feature": "Added", "breaking": "Changed"}[note["change"]],
        "summary": note["summary"], "action": note["action"],
        "compatibility": note["compatibility"], "notes": [note["path"]],
    } for note in notes]
    return {
        "schema": SCHEMA, "version": target.version, "core_version": target.core,
        "wheel_version": target.wheel, "date": date, "source_commit": cut, "base_commit": baseline,
        "inputs": {name: _sha(_at(root, cut, name)) for name in INPUTS},
        "notes": notes, "entries": entries, "deferred": [],
        "summary": "Fork SDK release preparation for the explicitly documented capabilities.",
        "migration": [
            "Rebuild matching native libraries, headers and consumers; review the compatibility notes below."
        ],
        "limitations": [
            "Release records do not widen qualification. STEP development profiles retain their documented "
            "options, checks and limits."
        ],
    }


def _text(value, label):
    if not isinstance(value, str) or not value.strip():
        raise ReleaseError(f"Nonempty {label} is required")
    return value


def _validate(root, data, current):
    if data.get("schema") != SCHEMA:
        raise ReleaseError("Unsupported release-plan schema")
    target = repository_version(data["version"])
    if data.get("core_version") != target.core or data.get("wheel_version") != target.wheel:
        raise ReleaseError("Plan version representations disagree")
    datetime.date.fromisoformat(data["date"])
    for key in ("source_commit", "base_commit"):
        if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", data[key]) or _commit(root, data[key]) != data[key]:
            raise ReleaseError("Release commits must be immutable full object IDs")
    _git(root, "merge-base", "--is-ancestor", data["base_commit"], data["source_commit"])
    old = _at(root, data["source_commit"], "VERSION")
    if old is None or _version_order(target.version) <= _version_order(old.decode()):
        raise ReleaseError("Target version must advance the pinned VERSION")
    actual_notes = _inventory(root, data["source_commit"])
    identity = lambda note: tuple(note[key] for key in NOTE_KEYS)
    if sorted(map(identity, data["notes"])) != sorted(map(identity, actual_notes)):
        raise ReleaseError("Pinned note inventory or hashes changed")
    frozen_inputs = {name: _sha(_at(root, data["source_commit"], name)) for name in INPUTS}
    if data["inputs"] != frozen_inputs:
        raise ReleaseError("Pinned release inputs changed")
    assigned, ids = [], set()
    for entry in data["entries"]:
        identifier = _text(entry["id"], "entry ID")
        if identifier in ids or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", identifier):
            raise ReleaseError("Summary entry IDs must be unique kebab-case values")
        ids.add(identifier)
        if entry["category"] not in ("Added", "Changed", "Fixed", "Removed"):
            raise ReleaseError("Unknown changelog category")
        for key in ("title", "summary", "action", "compatibility"):
            _text(entry[key], key)
        if not entry["notes"]:
            raise ReleaseError("Each summary must identify its consumed notes")
        assigned.extend(entry["notes"])
    for item in data["deferred"]:
        _text(item["reason"], "deferral reason")
        assigned.append(item["path"])
    if len(assigned) != len(set(assigned)) or set(assigned) != {n["path"] for n in actual_notes}:
        raise ReleaseError("Every pinned note needs exactly one summary or explicit deferral")
    _text(data["summary"], "release summary")
    for key in ("migration", "limitations"):
        if not isinstance(data[key], list) or not data[key]:
            raise ReleaseError(f"Explicit {key} are required")
        for value in data[key]:
            _text(value, key)
    if current:
        if _commit(root, "HEAD") != data["source_commit"]:
            raise ReleaseError("HEAD moved after release planning; create a fresh plan")
        for name, digest in frozen_inputs.items():
            if _sha(_clean(_current(root, name))) != digest:
                raise ReleaseError(f"Release input changed: {name}")
        for note in actual_notes:
            if _sha(_clean(_current(root, note["path"]))) != note["sha256"]:
                raise ReleaseError(f"Selected note changed: {note['path']}")
    return actual_notes


def _links(text):
    # Pending notes live one directory below the rendered root release records.
    return re.sub(r"(?<=\]\()\.\./", "", text)


def _release_notes(data):
    channel = "fork development preview" if repository_version(data["version"]).stage else "fork SDK release"
    lines = [f"# SDK {data['version']}", "", f"Prepared {data['date']} as a {channel}.", "",
             "This is a local release-preparation record. It does not create a tag or publish a package.", "",
             data["summary"], "", "## Delivered changes", ""]
    for entry in data["entries"]:
        lines += [f"### {entry['title']}", "", _links(entry["summary"]), "",
                  "Integrator action: " + _links(entry["action"]), "",
                  "Compatibility: " + _links(entry["compatibility"]), ""]
    lines += ["## Compatibility and migration", ""]
    lines += [f"- {_links(item)}" for item in data["migration"]]
    lines += ["", "## Qualification limits", ""]
    lines += [f"- {_links(item)}" for item in data["limitations"]]
    lines += ["", "## Release inputs", "",
              f"Integrated source: `{data['source_commit']}`; comparison baseline: `{data['base_commit']}`.", "",
              "[RELEASE_MANIFEST.json](RELEASE_MANIFEST.json) maps every consumed note to its consolidated entry.", ""]
    return "\n".join(lines).encode("utf-8")


def _changelog(root, data):
    old = _at(root, data["source_commit"], "CHANGELOG.md")
    if old is None or not old.startswith(b"# Changelog\n"):
        raise ReleaseError("The pinned changelog heading is missing")
    channel = "Fork development preview" if repository_version(data["version"]).stage else "Fork release preparation"
    lines = ["# Changelog", "", f"## [{data['version']}] - {data['date']}", "",
             f"{channel}; release preparation does not imply publication.", "", data["summary"], ""]
    for category in ("Added", "Changed", "Fixed", "Removed"):
        entries = [entry for entry in data["entries"] if entry["category"] == category]
        if entries:
            lines += [f"### {category}", ""]
            for entry in entries:
                lines += [f"#### {entry['title']}", "", _links(entry["summary"]), ""]
    lines += [f"Source range: `{data['base_commit']}..{data['source_commit']}`.", ""]
    return "\n".join(lines).encode("utf-8") + old.removeprefix(b"# Changelog\n")


def _atomic_write(path, value):
    descriptor, temporary = tempfile.mkstemp(prefix=".release-", dir=path.parent)
    temporary = Path(temporary)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(value)
        os.chmod(temporary, path.stat().st_mode if path.exists() else 0o644)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def prepare(root, data):
    """Validate the complete reviewed plan before writing any owned release file."""
    root = _repository_root(root)
    lock = root / ".release-prepare.lock"
    try:
        descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError as error:
        raise ReleaseError("Another release preparation owns the lock") from error
    os.close(descriptor)
    try:
        notes = _validate(root, data, current=True)
        outputs = {
            "VERSION": (data["version"] + "\n").encode(),
            "CHANGELOG.md": _changelog(root, data),
            "RELEASE_NOTES.md": _release_notes(data),
        }
        deferred = {item["path"] for item in data["deferred"]}
        consumed = [{key: note[key] for key in NOTE_KEYS} for note in notes if note["path"] not in deferred]
        recorded_plan = dict(data)
        recorded_plan["notes"] = [{key: note[key] for key in NOTE_KEYS} for note in notes]
        manifest = {
            "schema": SCHEMA, "state": "prepared", "version": data["version"],
            "consumed": consumed, "plan": recorded_plan,
            "records_sha256": {name: _sha(value) for name, value in outputs.items()},
        }
        outputs["RELEASE_MANIFEST.json"] = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
        names = [*outputs, *(note["path"] for note in consumed)]
        before = {name: _current(root, name) for name in names}
        changed = []
        try:
            for name, value in outputs.items():
                if _current(root, name) != before[name]:
                    raise ReleaseError(f"Release input changed during preparation: {name}")
                _atomic_write(_path(root, name), value)
                changed.append(name)
            for note in consumed:
                name = note["path"]
                if _current(root, name) != before[name]:
                    raise ReleaseError(f"Selected note changed during preparation: {name}")
                _path(root, name).unlink()
                changed.append(name)
        except (OSError, ReleaseError):
            for name in reversed(changed):
                path = _path(root, name)
                if before[name] is None:
                    path.unlink(missing_ok=True)
                else:
                    _atomic_write(path, before[name])
            raise
    finally:
        lock.unlink(missing_ok=True)
    return manifest


def check(root):
    root = _repository_root(root)
    path = _path(root, "RELEASE_MANIFEST.json")
    if not path.exists():
        raise ReleaseError("No prepared release manifest")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("schema") != SCHEMA or manifest.get("state") != "prepared":
        raise ReleaseError("Unsupported release record")
    data = manifest["plan"]
    notes = _validate(root, data, current=False)
    _git(root, "merge-base", "--is-ancestor", data["source_commit"], _commit(root, "HEAD"))
    current_version = _current(root, "VERSION")
    if current_version is None:
        raise ReleaseError("Prepared VERSION is missing")
    if (manifest["version"] != data["version"] or
            repository_version(current_version.decode()).version != data["version"]):
        raise ReleaseError("Prepared version representations disagree")
    deferred = {item["path"] for item in data["deferred"]}
    consumed = [{key: note[key] for key in NOTE_KEYS} for note in notes if note["path"] not in deferred]
    if manifest["consumed"] != consumed:
        raise ReleaseError("Consumed-note mapping disagrees with the pinned plan")
    expected = {
        "VERSION": _sha((data["version"] + "\n").encode()),
        "CHANGELOG.md": _sha(_changelog(root, data)),
        "RELEASE_NOTES.md": _sha(_release_notes(data)),
    }
    if manifest["records_sha256"] != expected:
        raise ReleaseError("Release record hashes disagree with the reviewed summaries")
    for name, digest in expected.items():
        if _sha(_clean(_current(root, name))) != digest:
            raise ReleaseError(f"Prepared release record changed: {name}")
    for note in consumed:
        if _path(root, note["path"]).exists():
            raise ReleaseError(f"Consumed note is still pending: {note['path']}")
    for name in deferred:
        if not _path(root, name).exists():
            raise ReleaseError(f"Deferred note disappeared: {name}")
    status, message = changesets.report(root)
    if status:
        raise ReleaseError(message)
    return f"release check: {data['version']}, {len(consumed)} consumed, {len(deferred)} deferred; prepared only"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    commands = parser.add_subparsers(dest="command", required=True)
    planning = commands.add_parser("plan", help="read a pinned tree and write an editable review plan")
    planning.add_argument("--version", required=True)
    planning.add_argument("--date", required=True)
    planning.add_argument("--ref", default="HEAD")
    planning.add_argument("--base", required=True, help="explicit previous release commit or tag")
    planning.add_argument("--output", type=Path)
    preparing = commands.add_parser("prepare", help="apply the reviewed plan; no Git publication actions")
    preparing.add_argument("--plan", type=Path, required=True)
    commands.add_parser("check", help="verify prepared records, version and consumption")
    args = parser.parse_args(argv)
    try:
        if args.command == "plan":
            data = plan(args.root, args.version, args.date, args.ref, args.base)
            rendered = json.dumps(data, ensure_ascii=False, indent=2) + "\n"
            if args.output:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                with args.output.open("x", encoding="utf-8", newline="\n") as stream:
                    stream.write(rendered)
            else:
                print(rendered, end="")
        elif args.command == "prepare":
            data = json.loads(args.plan.read_text(encoding="utf-8"))
            manifest = prepare(args.root, data)
            print(
                f"release prepared: {manifest['version']}; {len(manifest['consumed'])} notes consumed; "
                "no tag or package published"
            )
        else:
            print(check(args.root))
    except (ReleaseError, changesets.FormatError, OSError, UnicodeError, ValueError, KeyError, TypeError) as error:
        print(f"release: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
