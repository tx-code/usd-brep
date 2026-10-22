---
change: feature
---

General STEP source inspection preserves every registered material-solid item
in one part's representation. Reports distinguish MaterialSolidCollection and
list source shape/shell identities without duplicate singleton role keys.
Native import still refuses the complete collection before allocation, with
GeneralMaterialCollectionNotImplemented; it never publishes one surviving body.

## Scope

[Source semantics](../source/STEP_SEMANTICS/README.md).

## Integrator action

Rebuild STEP consumers for the extended Part layout. Use Part::shapes to inspect
a collection; do not infer native support from a successful source check.

## Compatibility

Single-body reports and the planar multiplicity guard remain unchanged.
Some general multi-body requests now advance from a source multiplicity refusal
to a later checked-source/native-unsupported result. No partial native publication
or new geometry tolerance is introduced.
