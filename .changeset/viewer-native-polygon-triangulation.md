---
change: fix
---

The standalone STEP viewer accepts regular single-loop polygon cells returned by
native tessellation. It converts them to triangles using the native polygon
routine, retaining stored vertices and native-face labels for picking. The
complete projected triangle count is checked against existing display limits
before conversion; failed conversions never publish a partial scene.

## Scope

[Standalone viewer display and input scope](../tools/smlib_viewer/README.md#input-and-quality-scope).
This does not modify the tessellator, defining BRep geometry or STEP acceptance.

## Integrator action

Deploy the rebuilt `step_view_export` with matching native libraries.

## Compatibility

Existing triangle payloads, scene formats, SDK ABI, physical quality profiles
and import qualification retain their contracts. Unsupported polygon loops and
singular rings still refuse the display request; no cell is silently dropped.
