---
change: fix
---

General STEP imports use the axis-aligned bounds midpoint as the reference for
native mass integration, improving conditioning of moment terms. General reports
record `mass_integration_origin=AxisAlignedBoundsMidpoint`.

## Scope

[General STEP measurements](../source/STEP_IMPORT/README.md).

## Integrator action

None.

## Compatibility

Numerical area, volume and centroid observations may change with the integration
reference. The requested relative accuracy remains 1e-4; no absolute error bound
is added. Defining geometry, working tolerances, source semantics, P0 behavior and
ownership are unchanged.
