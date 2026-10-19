---
change: feature
---

The existing AP214 source profiles retain `PLANE_ANGLE_MEASURE_WITH_UNIT`,
plane-angle measure values and conversion-based angle-unit declarations.
Dimensional consistency and the measure subtype's nominal angle-unit rule
are checked separately, with original values and unit references preserved.
Source admission does not establish or apply a native angular conversion.

## Scope

[AP214 source checking](../source/STEP_SEMANTICS/README.md).

## Integrator action

Rebuild the STEP semantic library and its consumers to enable the added source
subset. Continue to inspect native construction results for unsupported angular
conversions or later geometry failures.

## Compatibility

The two AP214 source catalogs add declarations and measure/signature admission;
their existing declaration identities retain their order. The AP203 catalogs,
C++ API/ABI, ownership, native format, quality and repair contracts are unchanged.
