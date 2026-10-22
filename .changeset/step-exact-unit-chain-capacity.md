---
change: fix
---

Native general STEP import retains exact conversion factors and intermediate unit
scales beyond the native fraction range when their final millimetre ratio fits.
Checked 4096-bit unit arithmetic cross-cancels products without rounding source
values. Arithmetic capacity exhaustion or an unrepresentable final ratio remains
`Indeterminate`; geometry quality, repair permissions and publication stay unchanged.

## Scope

[General length normalization](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild the native importer and consumers together. Use the string-valued
`Numerator()` and `Denominator()` of `ExactUnitRatio` for general conversion evidence.
Retain the bundled Boost Software License with compiled distributions.

## Compatibility

Internal C++ `ConversionUnitDefinition::factorValue` and `scaleMm` change from
`Fraction` to `ExactUnitRatio`; `UnitNormalization::lengthToMm` stays `Fraction`.
Existing report rational objects retain their string num/den format. Native
kernel ABI, native archives and strict planar arithmetic remain unchanged.
