# Maintaining this fork

Fork: <https://github.com/tx-code/usd-brep>

Upstream: <https://github.com/NVIDIA-Omniverse/usd-brep>

Use `origin` for this fork and `upstream` for the original repository.
`origin/main` is the long-lived development and default branch. Track the
original project through `upstream/main`; a second local development branch
solely mirroring upstream is unnecessary.

Use temporary feature/fix branches for development and review, then remove
merged branches. Do not force-push `main`. Create a release branch when a
shipped version needs independent maintenance; release tags identify published
versions. Deployment branches have their own lifecycle.

The fork has its own semantic version line. `VERSION` is the single full release
identity; CMake extracts the numeric core and wheel packaging translates its
prerelease suffix. Use numbered `alpha`, `beta` and `rc` previews for explicit
development cuts, and `fork-v<version>` for fork release tags when tagging is
authorized. Preserve upstream tags and provenance separately. An upstream merge
does not replace the fork's version or reinstate retired product components.

Last incorporated upstream commit: `3fd5b9548d19aa8c12c29bc9637a96d42a06f8a5`
(Release 1.0.1). Native consumers must be rebuilt together after this integration;
see [the fork compatibility and migration record](RELEASE_NOTES.md#compatibility-and-migration).

For upstream synchronization, use [the merge skill](.agents/skills/usd-brep-merge-upstream/SKILL.md)
for development boundaries, semantic conflict review, matching-runtime validation, and local fast-forward.

Before an upstream merge, review local behavior changes and run the affected
regressions in a matching build environment. Record incorporated commits and
compatibility changes in Git/release records. Per-run receipts, machine details,
and raw comparisons stay local; a focused local pass establishes only its stated
scope. Use [the release process](docs/release/RELEASE_PROCESS.md) for release work.

Copy-ownership regression has an independent CMake/CTest entry at
`tests/assertvalid_ownership`. Planar-region regression is in the root CMake
validation build and runs through CTest and `tests.bat` / `tests.sh`. Current
build/package commands belong to [README.md](README.md).

Preserve upstream copyright/license notices and applicable third-party license
materials in distributions. Keep fixes and reusable regressions with the code.
Public reproductions use synthetic models; private input libraries stay local.
