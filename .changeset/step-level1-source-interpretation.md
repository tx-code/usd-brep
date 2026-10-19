---
change: feature
---

Source and semantic inspection add an explicit, unverified interpretation of
Part21 declaration `1` using the existing corrected-encoding reader grammar.
Original bytes, declaration, spans and the strict refusal remain available in
source-owned evidence. All structural, reference, typed/unit and resource checks
still apply; typed inspection requires explicit general interoperability.

This does not implement original 1994 SELECT semantics or enable native imports
of these carriers. Native/default paths remain strict. C++ source inspection
signatures and outcome layout change, so dependent consumers must rebuild.

## Scope

[STEP source inspection](../source/STEP_SOURCE/README.md) and
[source semantics](../source/STEP_SEMANTICS/README.md).

## Integrator action

Rebuild source inspection and all dependent C++ consumers. Applications must
select the interpretation explicitly and retain its source-owned evidence.

## Compatibility

C++ source inspection signatures and outcome layout change. Default source
reports and native carrier policy remain unchanged. The new source-only
assumption does not establish original 1994 SELECT semantics, schema binding
or native import acceptance.
