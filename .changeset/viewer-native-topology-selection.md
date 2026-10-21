---
change: feature
---

The standalone viewer supports face, edge and native-vertex hover and selection.
It displays worker-computed face areas and edge lengths, native vertex coordinates,
and distance between two selected vertices. Unavailable measurements retain their reason.

## Scope

[Standalone viewer](../tools/smlib_viewer/README.md).

## Integrator action

Distribute the updated viewer together with its matching `step_view_export` worker.

## Compatibility

The worker emits scene v3 with required topology and explicit auxiliary requirements.
The viewer still reads v1/v2 without CAD selection. IDs belong to the current native
display copy, not STEP entities. This adds no STEP qualification or continuous error bound.
