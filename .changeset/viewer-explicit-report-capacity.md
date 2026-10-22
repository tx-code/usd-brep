---
change: feature
---

The standalone viewer and scanner accept `--report-budget-mib` for large mandatory
import reports. The explicit capacity configures both source and native report
allowances and is retained in worker/scan records.

## Scope

[Standalone viewer](../tools/smlib_viewer/README.md).

## Integrator action

Distribute the updated worker and Python application together. Select a larger
capacity explicitly when the default report allowance is insufficient.

## Compatibility

The effective default remains 16 MiB. Input, process-memory, geometry, tolerance,
repair and deadline policies are unchanged; no automatic retry is introduced.
The worker retains prior argument forms and accepts an optional final capacity.
