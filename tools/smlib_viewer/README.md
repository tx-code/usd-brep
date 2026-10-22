# BRep Viewer

Minimal, read-only viewer with a single viewport, HUD, file drag/drop and CLI
startup. The Windows portable directory includes Python, Qt, VTK and the native
import worker. Keep the directory together; a target machine needs no Python
installation or development checkout.

```powershell
.\brep-view.exe "D:\Models\part.step"
.\brep-view.exe "D:\Models\mesh.stl" --edges
.\brep-view.exe --help
.\brep-view.exe --scan "D:\Models" --output "D:\LocalScan\run-001" --thumbnails
```

Open with Ctrl+O; F fits, I selects the isometric view, 1/2/3 select front/top/right,
E toggles boundary lines, W toggles mesh edges, H toggles the HUD, R reloads and
Escape cancels loading. Closing the window also terminates its workers.

Edge lines are off by default. Camera-relative lighting and a satin surface
material distinguish face directions and curved surfaces without outlines.
Redraw preserves the light rig; display shading never changes model positions,
connectivity, source geometry or import acceptance. The toolbar and corner HUD
leave most of the window to the model; fit commands retain a small viewing margin.
The HUD uses an opaque background to avoid stale text from Qt/OpenGL compositing
after window resizing or model reload.

The progress bar stays busy while work remains unknown. Its caption reports the
actual phase and measured stage counts where available. It reaches 100 only
after the complete scene is displayed, then hides after a short delay.
Failure/cancellation stops it at zero,
shows the reason and retains any previous scene with its original filename.

A native refusal classified as `NativeFaceLoopOrientationFailure` is captioned
as a failed face-loop orientation check. The original status, validator entry
key and diagnostic message remain available. This does not identify the source
as invalid, authorize repair or imply a broken pointer; mixed/unknown findings
keep the generic failure caption. The previous accepted scene remains intact.

## Input and quality scope

STEP/STP uses the native importer with an explicitly selected development preview
configuration; see the [import contract](../../source/STEP_IMPORT/README.md).

For one explicit STEP file, `--selection-source-sha256 HASH --select-definition ID`
selects source PRODUCT_DEFINITION identities; repeat the ID option for several
independent definitions. Every source definition must first pass complete source
checking. Only selected definitions are built and displayed, in source order;
one selected definition remains a document with its own local coordinates.
The HUD states the selected/source definition counts and the local result records
`ExplicitSelectedDefinitions`. This is not whole-file native qualification or
source-error recovery. Missing/altered selection receipts are rejected before
scene adoption; directory scans cannot use a one-file selection plan.

An optional single-file face-orientation repair uses `--repair-source-sha256`,
`--max-face-orientation-edits`, `--max-repair-proof-controls`,
`--max-repair-proof-products`, and repeated
`--repair-face-orientation DEFINITION:SHAPE:FACE:ORIGINAL:EFFECTIVE` (`T/F`).
Every budget and source identity must be explicit; these are not default healing
settings. The native core owns scope validation, continuous evidence and complete
publication. A fixed input plan cannot accompany `--scan`, periodic normalization
or support-domain restriction. Source metadata interoperability remains a separate policy.

The worker retains `AcceptedWithRepairs`, original source identity and any
independent source assumptions. The HUD marks the number of repaired face
orientations, plus source interpretation when applicable. Repair-proof, rebuild
and final verification stages retain the whole-request progress/cancellation
contract. A rendered repair remains viewer-pipeline evidence; retain the original
file and mandatory native report for its actual qualification.
The default `--profile development` preserves proven nongeometric interoperability
diagnostics and opaque source text. `--strict-source` disables those exceptions.
No implicit schema assumption, tolerance retry or alternate reader is used.
`--schema-interpretation` explicitly selects `DeclaredOnly` (default),
`UnregisteredAp214As2010`, or `Ap214NameOnlyAs2010`. Independently,
`--carrier-interpretation` selects `DeclaredOnly` (default),
`Level1AsCorrectedEncoding`, or `Level1AsCorrectedEncodingWithLowercaseHex`.
Nondefault interpretations require interoperability and cannot accompany
`--strict-source`. These options work for single files and scans; they never
trigger an automatic retry. When the core applies an assumption, the HUD marks
the source interpretation and receipts retain `AcceptedUnderInterpretation`.
Requested settings are distinct from the returned native status; rendering
remains only viewer-pipeline evidence, not formal STEP qualification.
`--preserve-opaque-file-name-text` additionally opts into lossless preservation of
unknown-encoding FILE_NAME administrative strings. It is independent of schema
and carrier assumptions, cannot accompany `--strict-source`, and is recorded in
worker/scan results. Without it, the existing filename-only behavior remains.
It neither guesses a code page nor permits malformed protocol/geometry data.
`--profile tolerance-study` and `--profile work-tolerance-study` select explicit
research configurations. Optional `--normalize-periodic-faces` and
`--restrict-unused-support-domains` retain their existing verified normalization
contracts. These options do not grant production or whole-STEP qualification.

CAD display retains the BRep and all imported associated wireframe/supporting
groups. Wireframe curves/points are cyan; construction curves/points and plane
grids are amber, with separate coordinate-frame axes. E toggles only BRep boundary
lines; it never hides required associated content. Source surface-model and
material-solid roles remain distinct. Display meshing uses 0.01 mm chord height and 15 degree angle
tolerances; these are preview settings, separate from import acceptance.

Finite associated curves are sampled through owning snapshot queries across the
whole native parameter interval, respecting source use direction. Quarter/midpoint
chord observations refine 32 initial spans at 0.01 mm; this is sampled display
evidence, not a continuous error bound or a change to defining curves. Export
limits are 10,000 source primitives, 200,000 curve sample points, 500,000 evaluator
calls, 20 s, and 16 MiB of auxiliary JSON. A reached limit fails the entire scene.
IDs, relationships, native domains/parameters and point coordinates remain with
the display projection. Coincident source items retain separate provenance.

Frames are labelled coordinate glyphs, not new STEP point/plane loci or transforms.
An unbounded supporting PLANE uses a marked finite display window around its
source origin, with half-width 0.6 times the BRep bounds diagonal. Its actual
finite native chart sentinels remain metadata and never become source trims.
The HUD marks the plane preview. Body dimensions/topology stay BRep-only, while
fit/clipping includes associated geometry and these display proxies.

CAD scenes use `brep-view-scene.v3` with topology and an explicit auxiliary requirement. The GUI
validates/adopts that whole payload before replacing a scene. Missing/corrupt
required auxiliary data cannot publish a body-only result. Body-only v1 and mixed v2 remain
compatible without topology selection; the SDK's complete `CopyForModeling` still refuses mixed models.
The worker explicitly copies the BRep component and exports every other role
from the unchanged owning source snapshot before publishing a complete manifest.

STL/OBJ/PLY/VTP/VTK inputs must be finite, nonempty PolyData; their source units
remain unknown. The viewer does not repair mesh or CAD input. Preview limits are
64 MiB input, two million cells, six million points, a 120 s worker timeout, and
a Windows worker-tree commit cap of 2 GiB. The GUI/renderer has no hard OS memory
cap. Native import additionally enforces its own budgets and 90 s deadline.
`--report-budget-mib` explicitly selects 16 (default), 32, 64, 128 or 256 MiB
for the mandatory report. Publication uses the minimum of source/native report
allowances, so this option configures both layers; the previous effective default
remains 16 MiB. Input, process-memory, deadline and geometry budgets are unchanged.
The worker/scan record the selected capacity. There is no automatic larger-budget
retry, and requesting more report capacity does not guarantee enough process memory.
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
python -m unittest discover -s tools/smlib_viewer/tests -t . -v
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

Our two application packages are ordinary Python modules under
`_internal/tools/smlib_viewer` and `_internal/tools/smlib_gui`; Qt/VTK remain in the
bundled runtime. With the same pinned dependencies and native worker, UI-only
updates can replace the matching application modules in a fresh copy and restart,
without recollecting the runtime. The frozen entry disables bytecode-cache writes.
SDK/CRT, dependency or entry-point changes require a full bundle build. A worker-only
application rebuild can update a fresh copy when the SDK/CRT bytes and PE import
dependencies match, the worker and Python scene protocol are updated together,
and clean-environment portable tests pass. This does not qualify an ABI migration.

The native worker publishes a complete request-relative scene manifest only
after import, modeling-copy validation, full tessellation and payload writing.
The application rejects stale generations and failed workers before adopting a
scene. CAD owners never cross processes. The shared GUI viewport and camera
helpers draw disposable projections; the workbench's modeling APIs are unchanged.

## Local directory scan

`--scan DIRECTORY` recursively selects the supported model extensions; repeat it
to select multiple roots. Directory symlinks/junctions are not traversed. Input
enumeration errors fail explicitly. `--output` must name a new directory outside
all selected roots. Existing results are never overwritten.

The scanner hashes each selected file and processes unique content sequentially.
STEP/STP share a deduplication key; mesh reader formats remain distinct. Duplicate
paths/content are reported separately and never counted as additional successful
imports. File identities are checked before/after each attempt and bound to the
worker-captured bytes on success. Originals are never rewritten.

Each unique file runs the actual viewer in a fresh, hidden process, including
scene adoption, the same lighting/camera, and framebuffer readback. On Windows
the process starts suspended, enters a 4 GiB job, then resumes; this includes
bootloader/venv descendants and the existing native-worker isolation. The default
150 s whole-file `--timeout` includes startup, import, meshing and rendering;
existing native and display budgets still apply. Timeout/crash/failure is recorded
before continuing. Ctrl+C terminates the active tree, retains completed results,
marks the summary incomplete, and returns 130. This edition is qualified on
Windows; POSIX uses process groups without the Windows memory cap.
Worker scratch directories belong to the scanner parent, so hard-killed GUI
processes cannot abandon their unpublished payloads in the system temp directory.
The result records cleanup completion; an unsuccessful cleanup cannot qualify a
successful attempt. Diagnostic case records remain in the requested output.

`records.jsonl` appends each observation once; `scan.json` atomically checkpoints
small counters after each attempt and embeds all records once on normal/cancelled
completion. During a run, use the journal's first `summary.processed` rows; a hard
crash may leave a partial last row outside that committed prefix. This keeps
report I/O linear in recorded data. The final report records settings,
input identities, duplicates, outcomes, and separate import/mesh/render completion
observations. Unestablished native acceptance remains null. Entering a phase is
not evidence of completing it. Phase timestamps are observed transitions, not CPU
profiles or continuous error bounds; process elapsed time includes startup.
Success means this viewer pipeline completed, not full STEP or SDK qualification.
`--thumbnails` additionally saves local viewport PNGs. Per-file `result.json` and
any diagnostics remain in their local case directories. All paths, hashes,
geometry, dimensions and images in these results are private local data.

Exit codes: 0 when all unique attempts render, 1 for file failures, 2 for invalid
selection/configuration, 130 for cancellation. Unsupported/rejected models remain
failures; scan never retries with relaxed settings or a different reader. Explicit
profile/normalization/source-policy options match single-file viewing.

Single-file automation can use `--result NEW.json --quit-after-load` without a scan.
The frozen onedir runtime reuses its Matplotlib font cache under
`%LOCALAPPDATA%/BRepViewer/Cache`, keyed by runtime directory and executable identity.
Moving/replacing the installation selects a new cache. This cache contains font
discovery data; source models and render payloads are not cached there.

### Native topology selection

Hover or click a face, edge or native vertex; Ctrl-click retains up to two selections,
and Ctrl+D clears them. Two selected vertices display their Euclidean distance in mm.
Orbit drags do not select. The existing HUD displays measurements without another overlay.
Selection survives display-style redraws, clears on successful replacement, and remains
with the prior scene when replacement fails. Unknown face labels remain unselectable.

Scene v3 carries bounded, validated topology tables and cell labels, and explicitly states
whether auxiliary geometry is required. Adoption requires every required payload. Distribute
the matching worker and viewer together. Legacy v1/v2 and mesh scenes remain viewable without
native CAD selection. Picks nominate IDs in the native display copy using the visible mesh
and boundary; the worker has exited, so these are not live kernel ray queries or STEP IDs.

Face area and edge length come from native queries before export, never mesh measures.
Requested query accuracies are recorded separately (relative area 1e-4, length 1e-5 mm);
they do not establish a continuous error bound. Failed queries and measurement-budget
exhaustion remain unavailable, not zero. Vertex coordinates come from the native vertex
table, not tessellation vertices. Current body occlusion prevents picking hidden topology.


`--assume-ascii-invariant-data` explicitly selects the native
`PreserveAsciiInvariantBytes` interpretation. It is independent of schema,
implementation-level and FILE_NAME choices, remains off by default, and is
incompatible with `--strict-source`. No guessed encoding or automatic retry is
introduced. Both scan settings and worker receipts record the request separately
from actual application. Applied imports remain `AcceptedUnderInterpretation`;
the HUD marks the text encoding unverified. An ASCII-only file does not acquire
this marker merely because the option was requested. Deliver the updated worker
and Python application modules together (the appended worker argument is rejected
by older workers rather than silently ignored).


## Material collections

The native worker uses canonical `ImportModelFile`: one capture/check, followed by
single-body, whole-collection or independent-document dispatch from the checked source inventory.
Single-body associated geometry retains its existing complete export. Multiple
material bodies are independent native display copies; overlapping/coincident
points are not welded. The application explicitly selects CompactV2 for its
mandatory import report and retains the existing capacity setting.

Collection scenes use `brep-view-scene.v4`. Every required body mesh, boundary
and topology sidecar must validate before scene adoption. Root counts and
cumulative mesh, boundary, topology and measurement budgets cover all bodies.
Missing/corrupt later bodies cannot replace a previous scene with a partial one.
The GUI assembles display projections without a Boolean union. Body-local topology
indices become globally unique display IDs, retaining body ordinal and local ID;
the HUD identifies the selected body alongside native measurements. Two selected
vertices can still measure distance in the definition's physical mm frame across bodies. Picks remain display
proxies, not live kernel ray queries or source STEP face/edge/vertex IDs.

The HUD reports an entity collection and body count. Mesh bounds fit the entire
scene. Scans and receipts retain native import qualification and whole-scene
completion. Unsupported collection associated geometry and assembly transforms
retain their native refusal; the viewer does not drop them or retry another path.

## Independent product definitions

Qualified independent material definitions use `brep-view-scene.v5`. The worker
exports every body before publishing the manifest; the loader validates all
required meshes, boundaries, topology inventories and per-definition ownership
before replacing the previous request. Budgets and payload-path uniqueness cover
the complete file. A source shape may be shared by distinct definitions, each
with its own unit normalization; this is not a duplicate body to discard.

Definitions have separate source coordinate frames. A small toolbar selector
shows one definition at a time, labelled **local coordinates**. All other
validated definition scenes remain owned by the accepted request. Fit/clipping,
Hover, selection and measurements use only the displayed definition. Switching
clears selection and invalidates old picker hits, so a distance cannot bridge
unrelated definitions. Bodies within the same definition retain their shared
frame and existing collection measurements. No assembly placement, Boolean
union, cross-definition extent or total material volume is inferred.

Scans/receipts retain the complete imported/exported inventory and explicitly
record `DefinitionLocal` display scope and initial definition index 0. A single
thumbnail shows that initial definition, not every definition or an assembly.
Missing/corrupt later payloads, cancellation and display failure retain the
previous accepted scene. Native assembly placement and document-level associated
geometry outside the qualified material subset remain explicit unsupported cases.


`--carrier-interpretation BlankLevelAsCorrectedEncoding` explicitly handles only
empty/ASCII-space-only implementation-level declarations using the existing
corrected grammar. It is unavailable in strict-source mode, never selected by
schema/year or a failed load, and retains native interpreted qualification.
Required unit/geometry errors still prevent a scene. Deliver a matching worker
when selecting this additional closed enum value.


`--unit-interpretation VerifiedExplicitUnitDimensions` separately requests exact
validation of redundant AP214 SI/conversion-length dimension declarations. It
requires interoperability, retains original source errors, and never rounds a
conversion scale or changes geometry. Native acceptance still requires its full
scale and geometry checks. The HUD/receipts mark an applied unit interpretation;
an unused request does not acquire the marker. This option requires the matching
worker and is independent of schema/carrier/opaque-text choices.
