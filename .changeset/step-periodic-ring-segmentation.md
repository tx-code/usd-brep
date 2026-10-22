---
change: feature
---

General STEP construction preserves complete circular source boundaries when
their ring vertices have different phases. A global target subdivision shares
the same original-basis child edges across adjacent faces. Finite two-ring
conical faces also receive a straight periodic chart cut, with checked SI angle
units and positive radii throughout the generator.

## Scope

[Periodic ring construction and retained correspondence](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild STEP libraries and native consumers, including the viewer worker. Read
target edge and vertex origins instead of assuming one native edge per source
edge. Retain the owning normalization evidence with original STEP inputs.

## Compatibility

Internal C++ TopologyNormalizationInfo expands with owning source-use segment
origins; reports conditionally include topology_normalization.source_use_segments.
Existing aligned cylindrical construction retains its path. Source bytes,
definitions, units, parameters, original vertices, geometry/tolerance budgets
and explicit source-repair permissions remain unchanged. Unsupported composition
and unestablished near-end parameters refuse without partial publication.
Kernel ABI and native archive format are unchanged; native archives and modeling
copies do not inherit STEP normalization or repair qualification.
