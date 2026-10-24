---
change: feature
---

General STEP development import preserves selected source-master B-spline pcurves as
exact surface/curve compositions. It keeps source parameters and association order,
converts UV coordinates according to the basis surface's parameter units, and checks
auxiliary images without assuming paired parameter equality.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild the native STEP library and consumers together. Retain the source snapshot
and report when source semantics and parameter mappings are required.

## Compatibility

No kernel ABI or quality-default change. The admitted basis and nonrational parameter
curve scope is explicit; unsupported representations are not substituted. Agreement
and final trim sampling remain development observations, not continuous error bounds.
Copies and native archives retain the target definitions but do not inherit STEP
qualification. Surface-model roles and atomic publication contracts remain separate.
