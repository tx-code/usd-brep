---
change: feature
---

Native STEP general import preserves one directed AP214 construction-geometry
representation with independent frames, points, supported finite curves and
unbounded planes. Owning views and complete archive mappings keep supporting
geometry separate from material BRep queries and explicit modeling copies.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md) and
[source semantics](../source/STEP_SEMANTICS/README.md).

## Integrator action

Rebuild C++ consumers. Use SupportingGeometry for supporting primitives and
CopyBrepForModeling when selecting only the material boundary for editing.

## Compatibility

Additive C++ API and changed aggregate ABI require consumer rebuilds. General
reports add supporting_geometry only when present. Native archives contain
explicit frame/point adapters; source identity and role mapping require the
owning STEP snapshot/report. Existing strict/P0, units, quality, repair and
publication contracts retain their limits; no full AP or continuous-error
qualification is added.
