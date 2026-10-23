---
change: feature
---

The standalone viewer accepts `--integration-relative-accuracy` for native STEP
material integrals, including explicit assemblies and STEP scan children. Local
results preserve requested/applied values and completion; mismatched evidence
prevents scene adoption.

## Scope

[Viewer input and quality scope](../tools/smlib_viewer/README.md#input-and-quality-scope).

## Integrator action

Rebuild the display worker and package it with the matching viewer sources.

## Compatibility

Default computation, scene payloads, picking-query controls and native import
API remain unchanged. Explicit observations add a scene field. This numerical
stopping control does not loosen geometry/repair budgets or certify error bounds.
