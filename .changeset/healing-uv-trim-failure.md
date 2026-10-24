---
change: fix
---

The optional native healing UV-trim stage returns a failed face-generation status
after its diagnostic, rather than reporting success and reading incomplete gap outputs.
An attempted failure stops the stage; preceding in-place edits remain.

## Scope

[Native healing](../.agents/operations/heal_import.md) with explicit UV-trim generation.

## Integrator action

Update the matching SMLib runtime and handle the returned status. No source migration.

## Compatibility

Public signatures, layouts, tolerances and parameter conventions are unchanged.
The ordinary `SmApiHealBrep` default remains unchanged. Unsupported faces deferred
by earlier classification remain deferred; success does not establish complete repair.
