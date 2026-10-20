# BRep Viewer

Minimal, read-only viewer with a single viewport, HUD, file drag/drop and CLI
startup. The Windows portable directory includes Python, Qt, VTK and the native
import worker. Keep the directory together; a target machine needs no Python
installation or development checkout.

```powershell
.\brep-view.exe "D:\Models\part.step"
.\brep-view.exe "D:\Models\mesh.stl" --edges
.\brep-view.exe --help
```

Open with Ctrl+O; F fits, I selects the isometric view, 1/2/3 select front/top/right,
E toggles boundary lines, W toggles mesh edges, H toggles the HUD, R reloads and
Escape cancels loading. Closing the window also terminates its workers.

Edge lines are off by default. Camera-relative lighting and a satin surface
material distinguish face directions and curved surfaces without outlines.
Redraw preserves the light rig; display shading never changes model positions,
connectivity, source geometry or import acceptance. The toolbar and corner HUD
leave most of the window to the model; fit commands retain a small viewing margin.

The progress bar stays busy while work remains unknown. Its caption reports the
actual phase and measured stage counts where available. It reaches 100 only
after the complete scene is displayed, then hides after a short delay.
Failure/cancellation stops it at zero,
shows the reason and retains any previous scene with its original filename.

## Input and quality scope

STEP/STP uses the native importer with an explicitly selected development preview
configuration; see the [import contract](../../source/STEP_IMPORT/README.md).
The default `--profile development` preserves proven nongeometric interoperability
diagnostics and opaque source text. `--strict-source` disables those exceptions.
No implicit schema assumption, tolerance retry or alternate reader is used.
`--profile tolerance-study` and `--profile work-tolerance-study` select explicit
research configurations. Optional `--normalize-periodic-faces` and
`--restrict-unused-support-domains` retain their existing verified normalization
contracts. These options do not grant production or whole-STEP qualification.

Initial CAD display supports complete BRep-only requests. A request containing
associated wireframe/support geometry is explicitly refused until the viewer can
display that required content. Source surface-model and material-solid roles
remain distinct. Display meshing uses 0.01 mm chord height and 15 degree angle
tolerances; these are preview settings, separate from import acceptance.

STL/OBJ/PLY/VTP/VTK inputs must be finite, nonempty PolyData; their source units
remain unknown. The viewer does not repair mesh or CAD input. Preview limits are
64 MiB input, two million cells, six million points, a 120 s worker timeout, and
a Windows worker-tree commit cap of 2 GiB. The GUI/renderer has no hard OS memory
cap. Native import additionally enforces its own budgets and 90 s deadline.
The worker waits for the parent's start handshake before allocating model data;
Windows Job Objects cover its subsequent children and cleanup.

All processing is local. Temporary payloads belong to a request and are deleted
after adoption/failure. `--screenshot PATH` explicitly saves a local viewport image (the Qt HUD is not embedded);
`--quit-after-load` exits with 0 on complete display and 1 on failure (2 for CLI
misuse). A picture is display evidence, not geometric correctness evidence.

## Development and packaging

Use the root pinned `gui` dependency group. Build the C++20 SDK with
`SMLIB_BUILD_VALIDATION=ON` as described in the
[repository build guide](../../README.md); `step_view_export` is the display
worker target. Use the same configuration, CRT and layout macros as the SDK.
For a source launch, set `BREP_VIEW_WORKER` to the built worker or point
`SMLIB_CMAKE_BUILD_DIR` at the build tree, then run:

```powershell
python tools/scripts/brep_view.py "D:\Models\part.step"
python -m unittest discover -s tools/smlib_viewer/tests -v
```

Install `requirements-build.txt` into that GUI environment. Package a Release
worker together with its matching adjacent SMLib.dll/SM_API.dll and the matching
Microsoft x64 redistributable CRT directory:

```powershell
python -m tools.smlib_viewer.build_portable --worker BUILD/Release/step_view_export.exe `
  --crt REDIST/x64/Microsoft.VC145.CRT --output _build/viewer-portable
```

Output must be new; the build never cleans an existing directory. PyInstaller's
onedir bundle lives in `dist/brep-view`. Preserve its dependency licenses.
Private models, inventories and screenshots are not packaging inputs.

The native worker publishes a complete request-relative scene manifest only
after import, modeling-copy validation, full tessellation and payload writing.
The application rejects stale generations and failed workers before adopting a
scene. CAD owners never cross processes. The shared GUI viewport and camera
helpers draw disposable projections; the workbench's modeling APIs are unchanged.

## Next batch interface

Directory scanning is the next layer above this same load/display pipeline:
content deduplication, isolated files, categorized failures, per-stage timing and
optional local thumbnails. Start sequentially; add concurrency after measuring
memory and renderer limits. A successful scan must distinguish import, meshing
and display success. This release does not yet provide a directory scan command.
