---
change: fix
---

Face property integration respects actual knot spans of concrete clamped
B-spline UV boundaries. This improves planar annulus area and centroid results
that could miss the caller's requested accuracy scale when an adaptive step
crossed knots. Native `SmApi*` area and mass queries, and native STEP measured
properties, can return corrected numerical values.

## Scope

Maintenance

## Integrator action

Review numerical snapshots against independent geometry when updating expected
area, centroid or volume results. Keep the existing requested accuracy and
native STEP quality configuration.

## Compatibility

C++ signatures, class layout and ownership are unchanged. Derived evaluators
retain their original integration path. The existing numerical control fraction
is allocated across actual positive spans with one-sided endpoint evaluation;
this does not establish a certified global area or volume error bound. Native
STEP still requires its complete report and successful final execution decision
before model publication.
