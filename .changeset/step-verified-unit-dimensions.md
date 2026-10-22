---
change: feature
---

STEP adds explicit VerifiedExplicitUnitDimensions interpretation for redundant
AP214 SI/conversion-length dimension references in derived slots. Exact exponent,
nominal quantity, dependency and ownership checks retain original source errors
and bytes. Native scale resolution and geometric acceptance remain required;
applied successful results are separately interpreted, never source-conforming.

## Scope

[Source unit interpretation](../source/STEP_SEMANTICS/README.md).

## Integrator action

Rebuild STEP consumers. Select --interpret-explicit-unit-dimensions with general
interoperability; the viewer option is --unit-interpretation
VerifiedExplicitUnitDimensions and needs the matching worker. Read source-owned
unit receipts separately from nongeometric preserved diagnostics.

## Compatibility

DeclaredOnly and existing overloads retain strict derived-slot behavior. Old
property-only mass exceptions retain their scope. No source rewrite, scale
rounding, tolerance/resource growth or partial publication is introduced.
