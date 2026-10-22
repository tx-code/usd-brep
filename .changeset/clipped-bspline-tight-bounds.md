---
change: fix
---

`SmCurve::CalculateTightBoundingBox` ignores B-spline knots outside the requested
parameter interval. Clipped curves no longer acquire unrelated endpoints or
continuity breaks in their tight bounds. Existing precision and solver settings
are preserved.

## Scope

Maintenance. [Curve property contracts](../tests/curve_properties/README.md).

## Integrator action

Rebuild SMLib and rerun application bounds regressions if relying on previously
oversized clipped-curve boxes.

## Compatibility

No API/ABI, ownership, native-file, or STEP/report format change. Clipped curve
bounding-box values can become smaller; the requested interval is unchanged.
