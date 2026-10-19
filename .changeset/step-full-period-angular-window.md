---
change: fix
---

General STEP import now forms full-revolution native angular windows by
converting the starting phase once and adding 360 degrees. Independently
converting both radian endpoints could exceed one period by floating-point
roundoff, leaving a closed circular/elliptic edge or cylindrical/conical support without a
constructible native representation at some seam positions.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild STEP_IMPORT and its static consumers. No request or quality-setting
change is needed.

## Compatibility

Source data, topology, parameter conventions, repair policy and quality budgets
are retained. Native parameter windows and stored coefficients may differ by
roundoff; existing error, UV, validation and publication checks still apply.
Partial angular intervals are unchanged.
