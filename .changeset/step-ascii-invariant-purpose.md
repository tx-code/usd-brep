---
change: feature
---

Native STEP import adds an explicit ASCII-invariant opaque DATA interpretation.
It preserves unknown text and original diagnostics while allowing audited AP214
representation-purpose comparisons under a recorded application assumption.
Applied results are AcceptedUnderInterpretation; defaults and geometric budgets
remain unchanged.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild STEP consumers and explicitly select PreserveAsciiInvariantBytes only
when its carrier assumptions apply. Encoding remains unverified. CLI users may
select --assume-ascii-invariant-data with general nongeometric interoperability.
The viewer option is not yet wired.

## Compatibility

Existing policy enum values, strict defaults and ordinary opaque preservation
retain their behavior. The added option and owning-view queries require rebuilt
STEP consumers. Successful interpreted CLI imports retain exit code 11; this
option does not qualify arbitrary legacy encodings or full schema conformance.
