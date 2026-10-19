---
change: feature
---

General native STEP import resolves positive conversion-based length chains to
SI METRE, including prefixes and nested factors. Exact factors and source
identities are retained in reports and in the owning model's `LengthUnits()`
observation. Coordinates and length quantities normalize to millimetres while
dimensionless parameters and source declarations remain unchanged.

## Scope

[Native STEP unit normalization](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild native STEP libraries and consumers together. Retain the snapshot's
owning source when interpreting record indices in conversion observations.

## Compatibility

Internal C++ `UnitNormalization` layout changes. Existing SI and proved planar
reports, source checks, quality, repair and publication contracts are preserved.
Unsupported bases and exact-arithmetic capacity failures do not silently select
another scale. Native kernel layout and archive format are unchanged.
