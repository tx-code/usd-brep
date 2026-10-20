---
change: feature
---

The standalone viewer displays complete imported requests containing associated
wireframe and supporting geometry. Source points, directed finite curves,
coordinate-frame glyphs and marked preview windows for unbounded planes retain
their separate roles and provenance. BRep statistics and source definitions stay
unchanged. Fit and clipping include the associated display geometry.

## Scope

[BRep Viewer](../tools/smlib_viewer/README.md), including its native display worker.

## Integrator action

Update the worker and Python viewer together. Mixed scenes require the v2 scene
protocol; missing or corrupt auxiliary payloads reject the whole scene.

## Compatibility

Existing body-only v1 display scenes remain supported. No SDK API/ABI, source
acceptance, modeling-copy, repair or geometric tolerance changes. Curve samples
and plane windows are derived display projections, not geometric accuracy proofs
or bounded material faces.
