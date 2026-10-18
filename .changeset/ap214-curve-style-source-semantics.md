---
change: feature
---

Native STEP source inspection preserves AP214 curve styles and predefined
draughting fonts, validates their required fields and exact names, and retains
all three supported SIZE_SELECT branches with their source values and units.
Curve-style colour and assignment constraints are checked, including the
zero-assignment case for styled curves. Length-measure TYPE predicates also
apply through generic measure-with-unit references.

Explicit general nongeometric interoperability can retain a malformed string
width with its original type diagnostic after selected-presentation ownership
and physical-dependency exclusion are proved. The value remains a string;
the importer does not invent a width or a geometric ownership root.

## Scope

[STEP source semantics](../source/STEP_SEMANTICS/README.md).

## Integrator action

Use the existing explicit general BRep and nongeometric interoperability options
when this metadata-preservation behavior is wanted. Inspect preserved diagnostics
before interpreting appearance values. Strict mode continues to reject malformed
width fields.

## Compatibility

The fixed AP214 source catalogs gain declarations and stricter appearance checks;
positive/nonnegative length quantities are checked against their declared TYPE
predicates. Reports classify preserved width type errors under type fields.
The P0 catalog and native model ownership contracts are unchanged. This adds no
full-schema conformance, rendering, or geometric-accuracy qualification.
