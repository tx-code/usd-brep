---
change: feature
---

Native STEP source checking preserves AP214 value representation items and point
appearance, including original scalar kinds, unit contexts, marker branches and
selected-part attachment. Property values do not change native geometry or
silently inherit the body's units. Required positive/nonnegative value predicates
also reject previously unchecked invalid N2635 value items.

## Scope

[STEP source semantics](../source/STEP_SEMANTICS/README.md).

## Integrator action

Rebuild C++ consumers. Inspect source diagnostics and each property's own unit
context; scalar transport does not imply canonical quantity conversion or
renderer support.

## Compatibility

The source subset expands only for the fixed AP214 profiles; existing type IDs
stay stable. Invalid N2635 positive/nonnegative value measures now fail source
checking. Strict unit-context rejection and explicit nongeometric interoperability
remain distinct. No physical unit, tolerance, repair or publication relaxation.
