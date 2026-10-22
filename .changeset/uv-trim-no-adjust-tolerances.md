---
change: fix
---

`SmFace::CreateUVTrimCurves(FALSE, ...)` preserves edge and vertex working
tolerances while creating and installing derived UV trims. Projection retries
also honor an enclosing validation-only operation. Measured gaps remain
available to callers; explicitly permitted tolerance adjustment retains its
existing behavior.

## Scope

Maintenance. See the [native STEP import contract](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild SMLib. Continue checking reported gaps and final native validity;
preserving a tolerance does not establish an accurate UV representation.

## Compatibility

No C++ API/ABI, Python API, ownership or native-file format change. No STEP
quality budget, source geometry or parameter convention is relaxed. Callers
requesting no adjustment can no longer rely on implicit tolerance growth to
make an approximate trim pass subsequent validation.
