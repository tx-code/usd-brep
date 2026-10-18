---
change: feature
---

Explicit general STEP source profiles retain `TOROIDAL_SURFACE` placement and
both positive length radii. General native construction supports finite standard
ring tori with source outer-bound markers and existing source seams, subject to
the existing UV, validity and publication checks.

Positive horn/spindle source surfaces remain source-checked; native construction
reports `GeneralToroidalRingRequired`. The importer does not apply the separate
CSG `TORUS` radius-ordering rule to these source surfaces.
Source-positive radii at or below the kernel's supported minimum are refused
with `GeneralToroidalKernelRadiusRange` before kernel construction.

## Scope

[Source semantics](../source/STEP_SEMANTICS/README.md) and
[general native STEP construction](../source/STEP_IMPORT/README.md).

## Integrator action

None.

## Compatibility

The default P0 profile, source bytes, units, orientation, geometric quality budgets,
ownership and atomic publication contracts are unchanged. Unmarked toroidal regions
remain deferred; this does not establish complete schema or periodic-trim support.
