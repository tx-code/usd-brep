---
change: feature
---

Explicit general STEP source profiles preserve `TRIMMED_CURVE`, mixed trimming
SELECT members, sense and preference. Exact SET membership, WR1/WR2 and finite
basis dependencies are checked without changing source records or parameters.

General native construction supports a single trimmed circle/ellipse with
established cuts, checks both supplied representations, composes trim and edge
orientation, and prevents topological edges extending outside the source trim.
Reports distinguish wrapper and basis with optional `source_basis_geometry`.

## Scope

[Source semantics](../source/STEP_SEMANTICS/README.md) and
[general native STEP construction](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild native consumers together with the C++ library. `ParameterMapping` has
an additional optional owning basis identity; existing JSON mappings omit it.

## Compatibility

P0, existing type identities and non-trimmed mappings remain unchanged. Native
trimmed-conic construction has an explicit limited angle/unit scope. Other bases,
ambiguous/multiple periods and unestablished UVs do not publish a model. This is
not complete schema, informal-proposition or continuous-geometry qualification;
quality, repair, cancellation and atomic publication contracts are unchanged.
