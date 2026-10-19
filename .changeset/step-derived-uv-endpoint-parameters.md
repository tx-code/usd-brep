---
change: fix
---

General native STEP imports can rebuild an inaccurate derived UV trim while
retaining its closed endpoints and the fresh projection's parameterization.
This avoids parameter distortion from trimming and restoring the old interval.
Raw and matched paired residuals must meet the existing physical budget; native
seam, continuity and validity failures still reject the complete model.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild the native importer and linked consumers. Consumers of normalization
records may encounter the new `DerivedUvEndpointMatching` kind.

## Compatibility

No public C++ API/ABI or native-file change. Source data, defining geometry,
quality configuration and `Repair=None` are unchanged. Additional bounded work
is charged only when an installed derived trim fails its existing budget;
required reports retain actual projection/matching measurements. Accepted
ordinary paths retain their original execution and report behavior.
