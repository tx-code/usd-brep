---
change: feature
---

General STEP import classifies unmarked cylindrical and conical face regions
after UV installation and gap checks. Admission requires a unique outer region,
correct containment and orientation, closed boundaries, and no unresolved seam,
pole or unpaired non-area-loop problems. Source SET order does not select the
material region.

Cylindrical and conical classifications retain all involved source bounds in
representation-normalization records. A derived periodic cut does not relabel
either original ring as an outer source bound.

## Scope

[General native STEP construction](../source/STEP_IMPORT/README.md).

## Integrator action

None.

## Compatibility

This extends the opt-in general BRep development path. Other unsupported supports
remain deferred. Source geometry, orientation, quality budgets, working tolerance,
ownership and atomic publication contracts are unchanged. It adds no repair or
full-schema conformance claim.
