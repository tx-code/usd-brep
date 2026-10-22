---
change: feature
---

STEP source inspection offers an explicit per-definition diagnostic mode that
continues part/region and unit-context observations after a sibling fails. The
owning result retains original source identities, each failure, unapproved
metadata facts and incomplete traversal state without publishing qualified source
or a model. Ordinary source checking and native admission retain their contracts.

## Scope

[Independent definition diagnostic contract](../source/STEP_SEMANTICS/README.md#diagnostic-observations-for-independent-definitions).

## Integrator action

Rebuild internal STEP consumers for the expanded Outcome layout. Use
InspectIndependentDefinitions or --diagnose-independent-definitions explicitly;
retain its owning result when reading diagnostic/source views.

## Compatibility

The C++ source API and opt-in semantic report expand. Diagnostic observations
never become Snapshot/ProductDocument qualification, source-error isolation,
local syntax recovery, repair authorization or partial native publication.
Kernel ABI, native archive and default source/report behavior are unchanged.
