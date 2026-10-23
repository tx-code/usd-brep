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
`InspectAssemblyInputs` adds all-or-nothing parent/child unit-context observations,
reusing existing unit checks. It retains mixed SI contexts and defers conversion
arithmetic; successful observation stays `Deferred` without a qualified model.
The internal native-import planner evaluates right-handed item-defined frame
matrices using the shared source frame and exact unit routines. Its numeric
candidates do not grant native assembly import or a geometric error certificate.
`PlanAssemblyDefinitions` separates definition-local representation scopes from
occurrence placement links, retaining all directly listed geometry and placement
items. Scope success is not whole-document ownership or geometry qualification.
Nominal solid/surface/placement roles and all direct representation memberships
are retained. Empty leaf geometry and unsupported extra items are not silently
discarded; a placement-only parent is not represented as an empty material solid.
Definition scopes retain reachable faces and apply the shared outer-bound source
predicate, preserving duplicate-outer diagnostics and unmarked-region deferrals.

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
