---
change: fix
---

Mesh redraw preserves the renderer's lights in the viewer and workbench. Faces
retain directional shading after loading or changing display modes, so shapes
remain readable with edge lines disabled. The standalone viewer uses a camera
light rig, satin material, a compact toolbar and a smaller corner HUD. An opaque HUD
prevents doubled text after window resizing or model reload.
Portable application modules can be updated with the matching runtime unchanged.

## Scope

[Viewer](../tools/smlib_viewer/README.md) and the shared GUI viewport.

## Integrator action

Rebuild the optional portable viewer to include the updated display code.

## Compatibility

No SDK or Python API signature, mesh geometry, STEP acceptance or repair changes.
Existing caller-provided lights survive display mesh replacement.
