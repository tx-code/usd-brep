---
change: feature
---

Native STEP import and the standalone viewer accept an explicitly named, bounded
circle-vertex correction within a source-bound cylindrical-band repair plan.
Original input and defining curves/surfaces remain; separate displacement,
cumulative and local-feature checks precede complete repaired-model publication.
Reports retain changed vertices and owning final budget evidence. Ordinary
imports and boundary-role-only repair retain their zero-displacement behavior.

## Scope

[Native STEP repair contract](../source/STEP_IMPORT/README.md#同一计划中的受限圆边顶点修复).

## Integrator action

Rebuild STEP libraries and all C++ consumers together. Supply the explicit vertex
plan and independent exact millimetre budgets to use this development repair.

## Compatibility

Internal STEP C++ request, evidence, history and report layouts grow. Existing
requests retain their report format when the new repair is absent. New repaired
requests record a separate configuration and vertex evidence; the viewer requires
matching worker receipts. Kernel ABI and native archive formats are unchanged.
