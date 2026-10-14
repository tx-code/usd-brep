# Planar region regression

Synthetic geometry only. The suite covers unordered/reversed curves, arbitrary
nesting, holes and islands, disjoint regions, circular boundaries, absolute
tolerance thresholds, plane orientation and translation, curved nonplanarity,
open/branched/duplicate/intersecting boundaries, resource limits, input lifetime,
section and selected projection boundaries. Accepted BReps are checked with
`AssertValid`. Windows additionally checks live heap allocation balance after
repeated success and rejection paths; working set is not a leak oracle.

The `planar_regions` target is included in the normal Premake build and invoked
by `tests.bat` / `tests.sh` (and therefore the existing `repo test` entry point).
No external CAD files or USD stage are needed by this suite.

For an already-built SDK, the independent CMake entry point is also available:

```sh
cmake -S tests/planar_regions -B build/planar-regions -DSMLIB_LIBRARY_DIR=<library-dir> -DSMLIB_RUNTIME_DIR=<runtime-dir>
cmake --build build/planar-regions --config Release
ctest --test-dir build/planar-regions -C Release --output-on-failure
```

On Windows, add TBB and other runtime dependency locations to `PATH` before
running CTest. On Linux/macOS use the SDK's usual library search path setup.
The suite must load matching `SM_API` and `SMLib` libraries.
