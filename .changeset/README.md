# Pending release notes

Add one descriptive kebab-case `.md` file here with each logical caller-visible
fix, feature, or compatibility change. Include it with the code and update the
affected API or usage guide. Purely internal refactors and documentation-only
edits need no changeset. Remove a pending note when its change is reverted before
release; keep this README when notes are consumed.

Use this format:

```md
---
change: fix
---

Describe what a caller observes, in present tense. Name the affected operation
or contract rather than the implementation task.

## Scope

Write `Maintenance`, or link to the affected API guide or accepted design scope.
The link identifies scope; it does not claim capability acceptance.

## Integrator action

State the required action, or write `None.`

## Compatibility

Name the affected C++ API/ABI, Python API, ownership, native-file, STEP/report,
or quality contract and any migration limits, or write `No contract change.`
```

`change` is `fix`, `feature` or `breaking`. This sorts pending notes; it does not
select or bump the SDK version. Do not include a target version, progress status,
test counts, or commit ranges. Notes record intended observable behavior; they
do not grant STEP conformance, geometric accuracy, or modeling qualification.

Check notes without building the SDK or installing dependencies (Python 3.10+):

```shell
python tools/agent/changesets.py check
```

The checker reads the format from this page's single `md` template and the
allowed values from the sentence beginning with `` `change` is ``. It checks
the template itself, kebab-case file names, the sole front-matter field,
description text, and the required sections appearing once, in order, with
content. Headings inside fenced code blocks are content. Change the format here;
the checker follows it. It validates structure, not the truth of a note or the
presence of notes for every relevant code change; reviewers check those.

The validation-enabled CMake build registers `changesets` and
`changesets_contracts` in CTest, including native-only builds (which need a host
Python interpreter for these checks). Exit codes are 0 for success, 1 for format
violations, and 2 when the format or input cannot be read. Diagnostics use
`.changeset/file.md:line: rule`.

At release freeze, follow the [release process](../docs/release/RELEASE_PROCESS.md):
check notes against integrated code and the accepted release scope, look for
missing observable changes, then synthesize the release records. Delete only
consumed notes in that release-record change; Git retains their original text.

`python tools/agent/release.py plan` captures a fixed commit and every pending
note's hash. Consolidate its entries and explicitly defer anything outside the
release before running `prepare`. The tool consumes only that reviewed inventory;
later notes stay pending. Preview releases can consume their delivered changes.
The release manifest records the mapping; `check` verifies it against Git and the
rendered records. These commands do not create commits, tags or packages.
