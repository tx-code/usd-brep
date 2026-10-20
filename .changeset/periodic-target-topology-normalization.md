---
change: feature
---

Native STEP general development import offers explicit periodic cylinder/cone
target normalization with owning source-to-target topology correspondence.
The default import path and source definitions remain unchanged.

## Scope

[Native STEP development import](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild C++ consumers. Explicitly select `Options.normalizePeriodicFaces` or
`--normalize-periodic-faces` for the general development path and retain the
normalization report alongside native archives.

## Compatibility

C++ `Options`, `Report`, and snapshot APIs gain fields/accessors; their layouts
change. Opt-in reports add target inventory and source correspondence. Target
face counts/order may include multiple children of one source face, and native
edge parameters are distinguished from STEP parameters. No default quality,
repair, source interpretation, or native-file format changes.
