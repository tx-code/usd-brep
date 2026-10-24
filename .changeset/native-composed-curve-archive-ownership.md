---
change: fix
---

Native archive readback of a standalone `SmCrvOnSurf` restores the owning curve
links on its UV curve and support surface. The decoded definition can pass its
existing ownership validation directly, without a later BRep reconstruction or
Copy. Stored geometry, parameters and archive bytes remain unchanged.

## Scope

[Native composed-curve contracts](../tests/curve_copy/README.md).

## Integrator action

Rebuild SMLib. Continue validating decoded geometry under the application's
selected quality requirements; this fix does not grant STEP import qualification.

## Compatibility

No C++ API, ABI or native file-format change. Successful binary and text reads
restore constructor ownership invariants. Existing malformed-input and
decoder-failure contracts are unchanged.
