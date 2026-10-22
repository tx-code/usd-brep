---
change: feature
---

Native STEP import explicitly qualifies and atomically publishes all independent
product definitions through `ImportDocumentFile/Bytes` and CLI
`--independent-products`. Each definition retains its own geometry, unit context,
source identity and single-body/material-collection result. One source capture,
shared bindings and a request-wide execution/report ledger cover the document.

## Scope

[Native independent definitions](../source/STEP_IMPORT/README.md) and
[source qualification](../source/STEP_SEMANTICS/README.md#qualified-independent-definitions).

## Integrator action

Rebuild source, semantic, planar and native static libraries and consumers
together. Select the document interface explicitly; use definition/body indices
for queries and preserve the owning source/report with native archives.

## Compatibility

Internal C++ Snapshot/Outcome/Report layouts extend; kernel ABI and native file
format stay unchanged. Legacy single-part selection does not change. Assemblies,
placements and document-level associated physical geometry remain unsupported;
no geometry is dropped, united or upgraded in role. Quality, repair, cancellation
and complete-result publication retain their contracts.
