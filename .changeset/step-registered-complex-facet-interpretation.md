---
change: feature
---

General STEP import can explicitly interpret a nonstandard complex-entity order
through complete registered facet-name bindings. It preserves source order,
values, units and original diagnostics, and keeps actual interpretation in the
owning source/model qualification. Later source or native failures still refuse
publication. Strict/default behavior is unchanged.

## Scope

[Registered complex-facet contract](../source/STEP_SEMANTICS/README.md#explicit-registered-complex-facet-interpretation).

## Integrator action

Rebuild STEP_SEMANTICS, STEP_IMPORT and their C++/CLI/viewer consumers together.
Select the new policy explicitly; retain the owning source and its interpretation
evidence when exporting native models or declared product graphs.

## Compatibility

Internal semantic checking signatures, source/product-graph evidence and native
Options expand. Native success under an applied policy uses the existing
AcceptedUnderInterpretation qualification; explicit repairs retain their distinct
status. Default report bytes, kernel ABI, geometry budgets and native archive
format are unchanged. This is not general syntax recovery, incomplete-field
acceptance or a production precision preset.
