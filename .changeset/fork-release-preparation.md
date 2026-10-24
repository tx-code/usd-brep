---
change: feature
---

Maintainers can prepare a fork release from a pinned Git commit, consolidate pending
changesets, and verify the resulting version, release records and note-consumption map.
Changed selected inputs refuse preparation; new parallel notes stay pending. Recoverable
file-operation failures restore the owned release files and already consumed notes.

## Scope

[Fork release process](../docs/release/RELEASE_PROCESS.md).

## Integrator action

Review the generated plan and its compatibility and qualification limits before preparing.
Use an explicit previous-release baseline. Tags and package publication remain separate actions.

## Compatibility

Release tooling and records change. Geometry, native ABI, Python operation semantics,
archive formats and STEP development qualification are unchanged by the preparation tool.
