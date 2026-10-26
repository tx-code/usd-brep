---
change: fix
---

Large STEP assemblies use a small root scene index plus one bounded complete
descriptor instead of failing the root description limit. Every instance and
body remains subject to the existing complete-scene checks. Assembly and source
priority verification share one owning native-report parse and respect the
application's explicitly selected report capacity. Display-adoption diagnostics
remain separate from native import qualification.

## Scope

[Standalone viewer input and display scope](../tools/smlib_viewer/README.md#input-and-quality-scope).

## Integrator action

Deploy matching Python viewer modules and the rebuilt `step_view_export`.

## Compatibility

Small inline scenes, native SDK ABI and STEP/quality contracts are unchanged.
The new v8 envelope requires a matching viewer; it contains a complete v6/v7
descriptor and never publishes a subset. Direct assembly-reader calls retain
their historical default report capacity unless another capacity is selected.
