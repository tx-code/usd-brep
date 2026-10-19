---
change: fix
---

Curve-cache construction avoids repeated curve-to-Bézier deviation searches when an actual-coefficient certificate establishes the existing error criterion for supported quadratic NURBS. Other curves and inconclusive evidence retain the numerical check.

## Scope

[Quadratic cache certificate and regression controls](../tests/bezier_deviation/README.md).

## Integrator action

Rebuild the SDK. No call-site changes are required.

## Compatibility

No public API/ABI, RTTI, ownership, STEP/report schema or quality configuration change. Actual native parameter conventions, incoming tolerances, numerical fallbacks, independent validators, complete reports and cancellation remain in force. The proof envelope limits the fast path; it does not reject otherwise supported geometry or enlarge a tolerance.
