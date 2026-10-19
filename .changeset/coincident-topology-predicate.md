---
change: fix
---

`SmBrep::ContainsCoincidentTopology` returns whether a geometric coincidence was
found, independently of requested diagnostics and optional output arrays.
Either output array can be requested on its own. Root LEVEL2 `AssertValid` test
15 rejects a found coincidence and retains its pair diagnostic; lower levels
do not run this LEVEL2 query.
Explicit test requests run it only when they include test 15 in both build modes.

## Scope

[Coincidence query and validation contracts](../tests/coincident_topology/README.md).

## Integrator action

Rebuild SMLib and rerun model qualification against the new validator. Previously
accepted overlapping or coincident topology can now fail LEVEL2 validation;
retain the diagnosis rather than merging source topology or widening tolerance.

## Compatibility

C++ signatures and layouts are unchanged. Query booleans and native validation
results change where the old predicate confused suppressed reports with a found
coincidence. This does not establish complete geometric validity or change STEP
repair and publication policy.
