---
change: fix
---

`SmCurve::CalculateTightBoundingBox` computes supported circle/ellipse bounds from their actual quadratic NURBS coefficients. Clipped queries exclude points outside the requested native interval and avoid repeated general coordinate-extremum solves. Registered derived evaluators and unsupported bases/arithmetic retain the existing solver.

## Scope

[Native conic tight-bound contract and controls](../tests/conic_bounds/README.md)

## Integrator action

Rebuild the SDK. No call-site changes are required.

## Compatibility

No public C++ signature, object layout, RTTI requirement, Python API or STEP/report schema change. Clipped conic bounds may become tighter; outward numerical enclosures can change last digits. Actual parameter conventions and native failures are preserved. Import precision, surface-interior checks, quality, repair, complete reports and cancellation remain unchanged.
