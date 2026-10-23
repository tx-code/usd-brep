---
change: feature
---

`STEP_SEMANTICS` adds an owning, read-only assembly occurrence binding plan for
declared NAUO/CDSR/item-defined transformed relationships. It preserves repeated
usages and parent/child source roles, rejecting ambiguous or incomplete bindings
without a partial plan.
Transformation items must belong directly to their declared representation
endpoints; swapped or missing items are rejected without automatic correction.
Owning `SourceBindings` views expose existing named type/field/reference bindings
to subsequent assembly checks without fabricating a qualified part snapshot.

## Scope

[Assembly binding prerequisite](../source/STEP_SEMANTICS/README.md#assembly-occurrence-binding-prerequisite).
This is not native assembly import or numeric transform qualification.

## Integrator action

Rebuild STEP_SEMANTICS and consumers together: `RepresentationTransformLink`
now carries direct item membership observations. Keep downstream
unit, frame, closure and native checks separate from binding success.

## Compatibility

Additive C++20 API with an internal struct layout extension; existing admission,
report fields and native files are unchanged. Assembly declaration inspection
charges additional visits for indexed representation-item membership.
Product structure observations retain the shared typed binding owner; source
and field storage are reused, not duplicated.
