---
change: feature
---

The explicit modified N916 source profile recognizes SPHERICAL_SURFACE and
checks its positive-length radius, retaining source bytes and unit context.
Native spherical construction remains unsupported.

## Scope

[STEP semantics](../source/STEP_SEMANTICS/README.md).

## Integrator action

Rebuild STEP consumers together.

## Compatibility

Existing type identities and other source catalogs remain unchanged. Invalid
radii now receive a source predicate diagnostic instead of an unknown type.
No kernel ABI, repair policy or acceptance budget changes.
