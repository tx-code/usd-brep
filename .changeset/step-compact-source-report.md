---
change: feature
---

General STEP import adds explicit CompactV2 reporting (--compact-source-report).
It factors repeated attribute declarations and reconstructible raw fragments while
retaining the complete original bytes, source spans, diagnostics and acceptance
observations. Default expanded reports and all acceptance/resource limits remain
unchanged; no automatic fallback or partial JSON publication is introduced.

## Scope

[STEP reporting](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild STEP consumers. Select CompactV2 explicitly and recognize its nested
step-semantics.attributes.v2 and step-source.spans.v2 encodings before consuming
attributes or raw fragments. Follow the component READMEs to recover v1 fields.

## Compatibility

Existing invocations retain expanded v1. Compact mode changes serialization only,
not source interpretation or native capability. Report-local descriptor IDs must
not be persisted as schema identities. The internal C++ report-composition API
has an additional encoding argument with an expanded default.
