---
change: feature
---

General STEP import constructs derived pole/seam topology for a spherical cap
with one complete, coaxial circular outer boundary. Source geometry, vertices
and edge intervals remain unchanged; north/south and large/small caps retain
their source orientation. Full target inventory, native validity and existing
quality/publication checks remain required.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md). Tilted cuts, multiple loops,
source pcurves and unverified combinations with periodic splitting or support
domain restriction do not acquire this capability.

## Integrator action

Rebuild all STEP consumers with the updated TargetVertexOrigin layout. Derived
vertices may now carry surfacePole / JSON surface_pole provenance instead of a
source vertex ID or source-edge parameter. Automatic normalization can emit
topology_normalization while periodic_normalization_requested remains false.

## Compatibility

No SMLib ABI or archive change. Repair and geometry budgets are unchanged.
Reports retain source bytes and distinguish derived target topology from source
entities. Unverified normalization compositions fail without publishing a model.
