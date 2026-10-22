---
change: fix
---

General AP214 source checks report multiple FACE_OUTER_BOUND markers as
`SourceRuleViolation:FACE.WR2` / source rejection, instead of unsupported scope.
Nongeometric interoperability does not suppress this region-role error.

## Scope

[Source semantic contract](../source/STEP_SEMANTICS/README.md).

## Integrator action

Update diagnostic handling if it depended on the previous unsupported key or
semantic CLI exit code 2; this violation now returns rejection code 1.

## Compatibility

No previously rejected model becomes accepted. Source bounds are not relabeled,
and zero-outer-bound deferral and existing planar profile behavior are unchanged.
