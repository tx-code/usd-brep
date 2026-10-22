---
change: fix
---

Native STEP import preprojects partial circle edges on cylindrical surfaces with
a bounded surface solve. This avoids implicit edge-tolerance growth from the
legacy edgeuse retry when a single verified derived UV candidate is available.

## Scope

[Native import contract](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild STEP_IMPORT and its consumers. The SMLib SDK ABI is unchanged.

## Compatibility

Source geometry, units, topology, quality budgets and repair policy are unchanged.
Affected reports add normalization/measurement records and native-call charges.
Unestablished projections retain the existing path and rejection checks. General
kernel projection behavior outside this importer remains unchanged.
