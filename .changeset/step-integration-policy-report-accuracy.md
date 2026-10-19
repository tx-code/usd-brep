---
change: fix
---

Native STEP reports use the explicit numerical integration request in
`measurement_policy.native_requested_relative_accuracy`, consistently with
`numerical_integration`, including failed source checks. Default requests retain
their previous report bytes.

## Scope

[Native STEP numerical measurements](../source/STEP_IMPORT/README.md).

## Integrator action

Regenerate reports made with explicit numerical integration settings when
consuming the older measurement-policy field.

## Compatibility

Explicit-setting report values are corrected. Geometry, calculation, C++ ABI
and native archives are unchanged.
