---
change: fix
---

General STEP construction uses bounded single-branch UV projection for nonclosed
B-spline edges on cylinders, preventing legacy projection retries from enlarging
edge tolerance on exact rational circular boundaries. Source curves, parameter
intervals and all final geometry/tolerance checks remain unchanged.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild STEP consumers. General import reports may now contain
`ControlledCylinderSplineUvProjection` normalization evidence.

## Compatibility

No kernel ABI or native archive change. Existing quality and repair budgets
remain unchanged. This does not fix the legacy kernel projection API globally.
