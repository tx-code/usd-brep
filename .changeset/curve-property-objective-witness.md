---
change: fix
---

Curve local min/max property queries re-evaluate the returned candidate before
reporting its objective, keeping the scalar paired with its returned parameter.
Unevaluable candidates do not expose stale solver values as curvature evidence,
preventing false B-spline validation failures.

B-spline radius checks use a coordinate-length threshold distinct from the
parameter-dependent speed threshold. Changing only a curve's knot parameter
scale no longer enlarges or shrinks the radius threshold. This corrects both
false rejection of ordinary curves on short parameter domains and false
acceptance of small-radius curves on long domains. UV thresholds remain in UV
coordinate units; they are not physical millimetre curvature certificates.

## Scope

Maintenance.

## Integrator action

Rebuild SMLib and use its matching consumers.

## Compatibility

No C++ layout or ownership change. Unevaluable extremum candidates no longer
return a claimed property value. Radius predicate indices 13/14 now use bounding
box size divided by 10,000 instead of average parameter speed divided by 10,000;
the unit-span threshold, speed checks, working tolerances and diagnostic order
remain unchanged. Non-unit-domain validation results can change in either
direction. Existing property-search limitations still apply.
