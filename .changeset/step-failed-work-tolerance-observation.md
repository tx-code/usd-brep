---
change: fix
---

General STEP import retains the finite native working-tolerance observation that
caused `GeneralUnexpectedToleranceGrowth`, including its topology kind and source
identity. Failed candidates remain unpublished and are released.

## Scope

[Native import contract](../source/STEP_IMPORT/README.md).

## Integrator action

None.

## Compatibility

Failure reports may contain one additional tolerance observation. Successful
report order, acceptance budgets, repair policy and public ABI remain unchanged.
