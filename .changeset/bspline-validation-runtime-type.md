---
change: fix
---

`SmBSplineCurve::AssertValid` limits control-data regularity proofs using the
kernel's virtual runtime type. Registered derived curve types retain validation
through their virtual evaluator and the existing global property searches.

## Scope

Maintenance

## Integrator action

Native subclasses must provide their runtime identity through the existing
`SM_COMMON` / `GetType` convention when overriding the evaluator.

## Compatibility

C++ signatures, object layout, geometry and thresholds are unchanged. This does
not add an RTTI requirement. Derived curves can report findings that the
incorrect static-type gate previously bypassed.
