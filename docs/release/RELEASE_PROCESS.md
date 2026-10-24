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

## When preparing a requested release

1. Confirm the accepted release scope and target version. Reconcile pending notes
   against integrated code and relevant consumer evidence; inspect the
   implementation range for missing caller-visible changes. Keep unintegrated or
   out-of-scope notes pending. Follow [fork maintenance](../../FORK_MAINTENANCE.md)
   when the release incorporates upstream changes.
2. Update the full fork version in `VERSION`. CMake reads its numeric core through
   `cmake/RepositoryVersion.cmake`; do not add a second version literal to
   `CMakeLists.txt`. Packaging derives its wheel version from `VERSION`
   through `tools/repoman/wheel_version.py`. Canonical versions use
   `MAJOR.MINOR.PATCH` with optional `-alpha.N`, `-beta.N`, or `-rc.N`;
   the `0.0.0` value in the packaging
   template remains a staging placeholder. A `change` category does not perform
   this version update or determine compatibility policy.
3. Summarize verified changes in the root `CHANGELOG.md` for engineering history
   and reproducible implementation scope. Write root `RELEASE_NOTES.md` when
   preparing the release, for SDK consumers: delivered capabilities, required
   actions, and limits. Use matching version/date entries; preserve historical
   claims in their original version context. Do not create duplicate per-version
   records under `docs/`.
4. Check C++ API/ABI and build requirements, Python signatures and mutation/
   ownership, native serialization, and STEP schema/profile/report/quality
   contracts as applicable. Report unestablished checks accurately; a mesh,
   native validator pass, or other kernel's agreement alone does not establish
   exact BRep correctness.
5. Remove the notes consumed by the release records, keeping `.changeset/README.md`.
   Run `python tools/agent/changesets.py check` and the affected build/test checks
   on the final release tree; record the tested toolchain, configuration, and
   limitations. Commit explicit paths and preserve unrelated work.

Release preparation does not itself publish a package or create/push a tag.
Perform those actions only when included in the user's authorized scope.
