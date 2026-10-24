---
change: feature
---

Native STEP import, its CLI and benchmark, and the standalone viewer expose an
explicit finer-work-zone study configuration. It retains the ordinary general
profile's geometric and query budgets, fixes native work tolerance at 0.0001 mm
with zero growth, and keeps existing profiles and their refusal behavior.
The viewer requires a matching quality receipt before adopting a study scene.

## Scope

[Native STEP quality contract](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild the STEP library and consumers to use `Quality::GeneralFineWorkToleranceStudy()`
or its explicit CLI/viewer selection. Keep study qualifications and measurements
separate from ordinary-profile results.

## Compatibility

Adds a source-compatible C++ factory and optional CLI quality/profile choices.
Existing C++ layouts, kernel ABI, default report/scene payloads and native archive
formats are unchanged. No automatic fallback, repair or production precision
default is introduced.
