---
change: fix
---

Native STEP import can preproject nonclosed circle and B-spline edges on conical
surfaces using a bounded surface solve. Numerical approximation accuracy is
separate from source-to-surface distance; both projection and paired-lift checks
retain the original UV acceptance budget.

## Scope

[Native import contract](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild STEP_IMPORT and its consumers; the SMLib ABI is unchanged.

## Compatibility

Source definitions, topology, tolerances and repair policy are unchanged. Reports
may add `ControlledConeUvProjection` observations and native-call charges. Later
UV validity and topology checks remain mandatory and can reject a candidate.
