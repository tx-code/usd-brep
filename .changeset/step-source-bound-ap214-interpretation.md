---
change: breaking
---

Native STEP import and the source/native inspection CLIs accept an explicit,
input-bound interpretation using the pinned AP214 2010 source implementation.
The request must match the immutable original SHA256 and a single unbound
`AUTOMOTIVE_DESIGN` declaration. Original identity errors remain in the owning
source and report; complete native results retain `AcceptedUnderInterpretation`.
Default and earlier interpretation policies keep their existing eligibility.

## Scope

[Source interpretation contract](../source/STEP_SEMANTICS/README.md) and
[native import contract](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild the source semantics, native STEP libraries and every C++ consumer
together. Existing enum requests convert to `SchemaInterpretationRequest`;
the new policy requires its original input identity. Keep interpreted results
separate from verified schema binding.
Read the owning request's `.policy` when an enum is needed. Comparing complete
requests also compares input identity; comparing with an enum examines policy.

## Compatibility

Changes internal C++ interpretation parameter signatures and owning request
layouts, including STEP import options. Kernel layout, Python APIs, native
archive format and existing report payloads are unchanged. New requests add
their input identity to interpretation evidence. The standalone viewer and
benchmark do not yet expose this new request. No source rewrite, repair,
geometric-budget relaxation or automatic retry is introduced.
