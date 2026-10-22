---
change: fix
---

The viewer reuses the current hover highlight while the pointer remains on the
same feature, avoiding repeated mesh extraction and rendering. Camera, scene and
selection changes invalidate it; selecting a hovered feature removes its hover
overlay before drawing the selection.

## Scope

[Standalone viewer](../tools/smlib_viewer/README.md).

## Integrator action

Update the viewer Python modules and restart the application.

## Compatibility

No native ABI, topology identity or measurement contract changes.
