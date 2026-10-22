---
change: feature
---

`ImportModelFile/Bytes` dispatches a complete general-source request to a single
model, material collection or independent product document after one source
capture/check. The standalone viewer displays all qualified independent material
definitions through a local-coordinate definition selector, validating every
required payload before adopting the request.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md) and
[standalone viewer](../tools/smlib_viewer/README.md).

## Integrator action

Rebuild native import consumers with matching C++20 headers/static libraries and
deliver the updated native display worker with the viewer Python modules.

## Compatibility

The internal C++ API adds `FileModel`, `FileOutcome` and whole-file entry points.
Existing single-part/collection/document selection contracts remain unchanged.
Viewer scene v5 separates definition-local frames and retains whole-request
budgets, cancellation and publication. No partial acceptance, assembly placement,
source rewrite, native-file change or quality-budget relaxation is introduced.
