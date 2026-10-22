---
change: feature
---

The standalone viewer and scanner expose `--preserve-opaque-file-name-text` for
the native importer's explicit administrative-header preservation policy. Source
bytes remain unchanged and the selected option is retained in worker/scan records.

## Scope

[Standalone viewer](../tools/smlib_viewer/README.md).

## Integrator action

Update the worker and Python application together. Select the option explicitly
for affected files; it is incompatible with strict source mode.

## Compatibility

Existing defaults and worker argument forms remain supported. The worker accepts
one optional final boolean. Geometry, units, tolerances and repair policies do not
change, and no automatic interpretation or retry is added.
