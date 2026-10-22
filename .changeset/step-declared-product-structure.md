---
change: feature
---

General AP214 and fixed modified-N916 semantic inspection exposes an owning declared product structure
for multiple definitions and assembly usage/representation links. It retains
source identities, repeated usage edges and uncomputed transform references, and
checks usage uniqueness, cycles and the required relationship predicates.
Protocol-specific required descriptions and inverse clauses remain distinct.

## Scope

[Declared product structure](../source/STEP_SEMANTICS/README.md#declared-product-structure).

## Integrator action

Rebuild source, semantic, planar and native C++ consumers together. Callers may
inspect `Outcome::productStructure` separately from the qualified part snapshot.

## Compatibility

Internal C++ Outcome layout changes. The optional `product_structure` report
section is structure-only evidence; it grants no unit/transform evaluation or
multi-part native import. Existing single-part admission, source bytes, default
native selection and complete-result publication retain their contracts.
