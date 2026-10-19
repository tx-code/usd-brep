---
change: feature
---

Native STEP callers can explicitly tighten general BRep numerical integration
with `Options.numericalIntegration` or `--integration-relative-accuracy`.
Reports and owning snapshots retain the actual stopping control and completion
state without claiming a continuous error bound. Unconfigured requests retain
their previous integration and report behavior.

## Scope

[Native STEP numerical measurements](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild the native STEP library and its C++ consumers together. Select a finite
relative stopping control in `[1e-8,1e-4]` only on the general geometry path,
and keep different numerical configurations separate in validation and timing.

## Compatibility

Internal C++ `Options` and `Report` layouts change. The STEP source, native
archive, geometric quality, repair, cancellation and publication contracts are
unchanged. This setting does not alter the SM_API mass query's existing clamp.
