---
change: fix
---

The independent OCCT benchmark measures volume with explicit adaptive integration
at requested relative accuracy `1e-4`, recording the method and native estimate.
This avoids attributing fixed-quadrature differences to imported geometry.

## Scope

[Independent STEP benchmark measurements](../tools/step_import_benchmark/README.md).

## Integrator action

Rebuild the external `step_occt_probe` and keep its measurements separate from
older fixed-integration receipts.

## Compatibility

Reference volume measurements gain additive method/request/estimate metadata and
may change numerically. OCCT read/transfer timing scope, product import paths,
geometry quality settings and native ABI remain unchanged. Neither the stopping
request nor the estimate establishes a global continuous volume-error bound.
