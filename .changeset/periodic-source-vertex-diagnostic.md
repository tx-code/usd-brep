---
change: fix
---

Periodic-region refusals identify a complete native near-miss set belonging only
to retained source vertices. The viewer captions this failure more precisely and
keeps the previous accepted scene. Status, diagnostic key, work billing and model
publication decisions remain unchanged.

## Scope

[Native STEP diagnostics](../source/STEP_IMPORT/README.md#诊断与通用入口).

## Integrator action

Rebuild STEP_IMPORT and its consumers; package the matching viewer sources.

## Compatibility

Affected failure reports may add `NativePeriodicSourceVertexNearMiss` in the
existing optional classification field. Public C++ layout, successful report
bytes, geometry/repair budgets and acceptance requirements remain unchanged.
