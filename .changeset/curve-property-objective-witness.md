---
change: fix
---

Curve local min/max property queries re-evaluate the returned candidate before
reporting its objective, keeping the scalar paired with its returned parameter.
Unevaluable candidates do not expose stale solver values as curvature evidence,
preventing false B-spline validation failures.

## Scope

Maintenance.

## Integrator action

Rebuild SMLib and use its matching consumers.

## Compatibility

No C++ layout or ownership change. Unevaluable extremum candidates no longer
return a claimed property value. Validation thresholds and checks for actual
small radii remain unchanged.
