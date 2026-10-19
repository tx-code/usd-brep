---
change: fix
---

General native STEP imports measure final UV trims when parameter endpoint
differences satisfy the native edgeuse validator's parameter comparison policy.
The check keeps paired evaluations at the same parameters and preserves physical
error budgets. Unrelated ranges, invalid domains and later native failures still
prevent publication.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild the native importer and its linked consumers.

## Compatibility

No C++ API/ABI, ownership, native-file or report-format change. Source parameters,
geometry, quality budgets and repair policy are unchanged. A previously refused
parameter-roundoff case can proceed to the remaining checks; this does not grant
full STEP conformance or modeling qualification.
