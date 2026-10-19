---
change: fix
---

General STEP material-solid imports reuse each face's integration for model area, volume and centroid and all reported face areas and centroids. This avoids repeated quadrature and unused second moments while retaining the requested precision, complete reports and cooperative cancellation.

## Scope

[Native general STEP development import](../source/STEP_IMPORT/README.md)

## Integrator action

Rebuild STEP_IMPORT against the matching SDK.

## Compatibility

No STEP/report schema, source identity, quality, repair or publication change. Relative accuracy remains 1e-4; finite-precision integrals can differ in their last digits. Each fused face query retains its resource and cancellation checkpoint; checkpoint order is implementation-specific. Existing SM_API queries retain their behavior.
