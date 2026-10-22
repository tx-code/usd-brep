---
change: fix
---

General STEP import uses bounded single-branch derived UV projection for
nonclosed circle and B-spline edges on B-spline surfaces, including rational
surfaces. This avoids legacy retry-induced edge tolerance growth on exact
on-surface curves while preserving source definitions and acceptance budgets.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild STEP consumers. General reports can contain
`ControlledSplineSurfaceUvProjection` normalization evidence.

## Compatibility

No kernel ABI or archive-format change. Repair remains disabled on these paths;
failed candidates and multi-branch projections do not gain acceptance. Defining
curves/surfaces and source parameter conventions remain immutable.
