---
change: feature
---

An explicit DATA-string preservation policy captures unknown non-ASCII text as
owning `OpaqueString` values with original bytes and unavailable decoded text.
Default parsing and the independent HEADER-name option retain their contracts.

General interoperability may retain this unavailable text only in four exact
nondefining fields after selected-product, representation or layer-attachment
proof. Original unestablished-text diagnostics remain; all other fields and
unsupported protocols fail without publishing a model.

## Scope

[Source capture](../source/STEP_SOURCE/README.md),
[source semantics](../source/STEP_SEMANTICS/README.md), and
[native import](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild STEP C++ libraries and consumers together: `InspectBytes`/`InspectFile`
have a defaulted DATA-policy argument, and owning outcome/native options carry it.
Native CLI requests use `--preserve-opaque-data-strings`, explicit general quality
and `--interop-nongeometric`. Callers must inspect `ValueKind`; an opaque value's
empty text storage does not represent a decoded empty label.

## Compatibility

Strict/P0 requests, ASCII decoding, field identities, source units and geometry,
budgets, repair policy and atomic publication are unchanged. No codec is guessed,
no source is rewritten, and unknown text is not complete STRING/schema conformance.
