---
change: feature
---

Native STEP general imports can explicitly select the existing unverified
corrected-encoding interpretation for carrier declaration `1`. Complete models
use `AcceptedUnderInterpretation` when either a carrier or schema assumption
applies. Original bytes, declarations and strict diagnostics remain owned;
schema binding does not authenticate an assumed carrier.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md) and its independent
benchmark probe. Grammar, source checks, native checks and publication budgets
are unchanged. Strict/P0 imports refuse the option.

## Integrator action

Rebuild the native library and all C++ consumers for the extended Options/Report
layout. Select general quality, interoperability and carrier interpretation
explicitly. Check both schema and carrier qualifications; keep interpreted
results separate from ordinary success and retain the report with an archive.
Owning face, wireframe and supporting views expose their source's immutable
carrier receipt. A failed parse can retain the receipt without a partial graph.

## Compatibility

Default source/refusal/report behavior is retained. The native CLI now accepts
`--interpret-level1-as-corrected` with the required explicit general options;
interpreted success returns 11. `AcceptedUnderInterpretation` now covers carrier
assumptions as well as schema assumptions. This does not implement original
pre-corrigendum SELECT semantics, rewrite or verify `1` as `2;1`, relax any
required dependency, or grant full STEP/production qualification.
