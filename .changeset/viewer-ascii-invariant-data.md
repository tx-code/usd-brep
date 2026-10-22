---
change: feature
---

The standalone viewer and scanner accept --assume-ascii-invariant-data as an
explicit, default-off STEP interpretation. Worker receipts distinguish requested
and applied interpretation; the HUD marks applied text encoding unverified.
Strict mode rejects the option before loading.

## Scope

[Standalone viewer](../tools/smlib_viewer/README.md).

## Integrator action

Distribute the updated viewer modules and step_view_export worker together.
The source interpretation does not change geometry budgets or enable repair.

## Compatibility

Existing invocations keep their behavior. The worker accepts legacy argument
counts; the new appended option requires the updated worker. Public scene fields
are additive. Kernel DLL and runtime requirements are unchanged.
