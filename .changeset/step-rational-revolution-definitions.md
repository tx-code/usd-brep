---
change: feature
---

General STEP development import constructs finite B-spline surface-of-revolution
definitions, including rational generators, without fitting or analytic reclassification.
It preserves generator coefficients, weights, knots and parameters and reports the
qualified SI angle to native STEP parameter mapping.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild the native STEP library and consumers together.

## Compatibility

No kernel ABI or quality-default change. Unsupported generator domains and unestablished
angle-unit policies remain explicit refusals. This capability does not establish source
pcurve construction or change surface-model roles. Native copies and archives retain
the target definition but do not inherit STEP import qualification.
