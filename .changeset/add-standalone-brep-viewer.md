---
change: feature
---

The optional BRep Viewer launches models from the CLI or drag/drop in a minimal
window with a HUD, view controls and actual loading-stage progress. Native CAD
loading and display meshing run in a cancellable isolated worker. Failed or stale
loads retain the previous scene and never report completion at 100 percent.

## Scope

[Viewer contract](../tools/smlib_viewer/README.md).

## Integrator action

Build the optional step_view_export tool and distribute the complete portable
directory with its matching SDK and runtime dependencies.

## Compatibility

No SDK C++/Python API, native file, STEP acceptance or repair contract changes.
The initial viewer displays complete BRep-only requests and finite PolyData.
Development/research quality selection remains explicit; previews establish no
additional import qualification.
