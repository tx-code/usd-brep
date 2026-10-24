# Geometry SDK release records

## During development

Each caller-visible fix, feature, or compatibility change carries a
[pending release note](../../.changeset/README.md) in the same change as its code.
API/usage documentation and scoped validation remain with the implementation.
Purely internal refactors and documentation-only edits need no note. Reverting
an unreleased change also removes its pending note.

Daily changes leave published entries in [CHANGELOG.md](../../CHANGELOG.md)
intact. A changeset records observable behavior, integrator action, and contract
impact; it neither chooses a version nor proves that a capability was delivered.
In particular, explicit STEP development profiles retain their documented
qualification limits when described in release notes.

## Fork version policy

The fork owns an independent semantic version line. Upstream versions and
incorporated commits remain provenance in [fork maintenance](../../FORK_MAINTENANCE.md);
an upstream merge does not replace the fork version. Incompatible supported API,
ABI, build or serialization contracts require a major version decision. Compatible
additions normally use a minor release; compatible fixes use a patch release.
Note categories are review input, not an automatic compatibility decision.

Canonical versions use `MAJOR.MINOR.PATCH`, optionally followed by `-alpha.N`,
`-beta.N`, or `-rc.N`. Numbered previews create reproducible development cuts and
can consume the changes they deliver. A final release consolidates its previews;
consumed notes need not be recreated. A prerelease number does not qualify an
experimental STEP profile or promise that all known issues are repaired.

`VERSION` is the single full version source. CMake reads its numeric core through
`cmake/RepositoryVersion.cmake`; do not add another literal to `CMakeLists.txt`.
Packaging maps that full version through `tools/repoman/wheel_version.py`, for
example `2.0.0-alpha.1` to `2.0.0a1`. The packaging template's `0.0.0` stays a
staging placeholder. Fork tags, when authorized, use `fork-v<version>`.

## Prepare a fixed development cut

The commands below require Python 3.10+ and Git. Use a full checkout whose HEAD is
the intended integrated release input. First commit the implementation and its
notes; do not include another session's uncommitted code in the cut.

```shell
python tools/agent/release.py plan --version 2.0.0-alpha.1 --date 2026-10-24 --base v1.0.1 --ref HEAD --output .local/release-plan.json
```

The plan command reads Git without changing tracked files. It records full
commit IDs, selected-note blob IDs and SHA256 hashes, prior release-file hashes,
and the source format. Its output file is created only when explicitly named and
does not overwrite an existing plan. Choose an explicit previous release commit
or tag for `--base`; later fork releases normally use their previous fork cut.

Review and consolidate `entries` in the JSON plan. Each has a unique ID, title,
changelog category, summary, integrator action, compatibility text and note paths.
Every selected note must belong to exactly one entry or to `deferred` with a
reason. Correct obsolete intermediate descriptions when a later integrated change
supersedes them. Review the implementation range for missing caller-visible
changes; a complete note mapping alone does not prove the prose is correct.
Keep `migration` and `limitations` explicit, especially STEP source profiles,
interpretations, development options, numerical evidence and unsupported cases.

```shell
python tools/agent/release.py prepare --plan .local/release-plan.json
python tools/agent/release.py check
python tools/agent/changesets.py check
```

Preparation requires HEAD and all selected inputs to match the plan. It updates
`VERSION`, prepends the consolidated `CHANGELOG.md` entry without rewriting
historical entries, writes consumer-facing `RELEASE_NOTES.md`, and writes
`RELEASE_MANIFEST.json`. The manifest records the source cut, summaries and exact
consumption mapping; it is a durable release input record, not a machine test
receipt. Only consumed notes are deleted. Deferred notes and newly added notes
remain pending. Changed selected notes refuse preparation before writes.

A repository-local lock prevents simultaneous preparation commands. Preflight
and per-file checks detect changed owned inputs, and recoverable file-operation
failures attempt to restore the changed release files and consumed notes. This
does not promise a crash-safe filesystem transaction: preserve file ownership
between sessions and inspect Git after an interrupted preparation.

`check` compares the records and their hashes with the pinned Git inputs, verifies
the full/core/wheel version mapping and consumed/deferred notes, then runs the
pending-note checker. It is read-only. Validation-enabled CTest registers the
version and preparation contract suites, and checks the actual release records
when a manifest is present. Release validation still needs the affected native,
Python, ABI, serialization and STEP checks on a matching build as applicable.
Per-run toolchains, counts, logs and remaining qualification gaps stay local.

Commit the release records, version and explicit consumed paths together. Keep
the note README. Preserve upstream history in its original version context and
keep unintegrated changes out of the prepared release. Do not duplicate version
records under `docs/`.

These tools never commit, create/push tags, build or publish packages. A prepared
record is labelled `prepared`, including a stable-version preparation; publishing
remains a separate action requiring the user's authorization.
