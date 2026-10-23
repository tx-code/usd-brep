---
change: feature
---

`STEP_SEMANTICS` adds an owning, read-only assembly occurrence binding plan for
declared NAUO/CDSR/item-defined transformed relationships. It preserves repeated
usages and parent/child source roles, rejecting ambiguous or incomplete bindings
without a partial plan.

## Scope

[Assembly binding prerequisite](../source/STEP_SEMANTICS/README.md#assembly-occurrence-binding-prerequisite).
This is not native assembly import or numeric transform qualification.

## Integrator action

Rebuild STEP_SEMANTICS before using `StepAssemblyBindings.h`. Keep downstream
unit, frame, closure and native checks separate from binding success.

## Compatibility

Additive C++20 API; existing admission, report formats and native files are unchanged.
