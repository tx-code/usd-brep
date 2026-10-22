---
change: fix
---

`SmCurve::CalculateTightBoundingBox` retains coordinate extrema at zero-speed
turns, including a quadratic arch after a constant span and collinear curves
that reverse direction. It uses the existing coordinate-stationarity queries
with the original requested interval and solver tolerance.

## Scope

Maintenance. [Curve bounds regressions](../tests/curve_properties/README.md#零长度分段与共线折返的紧包围盒).

## Integrator action

Rebuild SMLib. Bounds previously missing such extrema can become larger.

## Compatibility

No API/ABI, ownership, parameter convention, working-tolerance, or file/report
format change. Other global curve-property queries retain their existing semantics.
