---
change: feature
---

General STEP import constructs associated finite wireframe curves and independent
points alongside the material BRep. Owning views retain source groups, units,
trim direction and parameter correspondence; native archives contain all geometry.
Required failures and cancellation discard the complete unpublished candidate.

## Scope

[Native STEP linked geometry contract](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild C++ consumers. Use `Wireframes()` for associated geometry and explicitly
choose `CopyBrepForModeling()` when a mixed result needs a body-only modeling copy.
Retain the report/source for STEP identities that the native part format does not encode.

## Compatibility

New C++ view/query APIs and aggregate fields extend the internal C++ ABI. Existing
BRep-only modeling copies and reports retain their behavior. Mixed results reject
implicit partial modeling copies; BRep statistics and material queries keep their
body scope. Explicit general options and existing quality/repair policies remain required.
