---
change: fix
---

Exact STEP curve comparison allocates its planned refinement net once instead of
allocating a complete new net for every inserted knot. Valid curve proofs can
complete within the same cumulative control-storage limit. Immutable source and
stored-native definitions, exact arithmetic, physical allowances, cancellation
and complete-model publication checks retain their contracts.

## Scope

[Native STEP curve evidence](../source/STEP_IMPORT/README.md).
Curve common refinement only; tensor refinement and terminal extension retain
their existing allocation paths.

## Integrator action

Rebuild the native STEP import library and linked consumers.

## Compatibility

No C++ interface, native ABI, report or quality-profile change. Implementation
work counts change with the new allocation and copy pattern; caller work and
cumulative control-storage limits remain enforced, with no larger defaults.
