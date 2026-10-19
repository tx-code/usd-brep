---
change: fix
---

Native STEP `ModelSnapshot::ClassifyMaterial` retries a region ray that hits an
edge or vertex instead of accepting a potentially grazing region answer.
Points on the actual model boundary retain the face, edge and vertex checks.
If bounded retries cannot establish a region, the query returns `Indeterminate`.

## Scope

[Native STEP import queries](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild native STEP consumers to use the corrected query implementation.

## Compatibility

No public C++ signature, source/report format or native-file change. Model
geometry, working tolerances, import checks and publication are unchanged.
Previously incorrect material classifications may change; exhausted ray
evidence returns `Indeterminate` rather than an invented region.
