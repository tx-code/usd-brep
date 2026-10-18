---
change: breaking
---

Fixed STEP profiles use immutable catalogs generated and validated at C++20 compile time.
Catalog acquisition and type-name lookup no longer construct dynamic metadata.
`Snapshot::RegisteredTypes()` returns `std::span<const std::string_view>` and
`AttributeView::nestedAggregates` returns a read-only span of static dimension descriptors.
Source values still borrow their owning snapshot; catalog generation does not qualify additional STEP content.

## Scope

[STEP catalog design](../docs/design/step-catalog.md) and
[source semantic contracts](../source/STEP_SEMANTICS/README.md).

## Integrator action

Rebuild C++ callers. Replace assumptions about vector references or mutable nested dimensions with read-only view access.
Copy names or dimension descriptors explicitly when an editable collection is needed.
Retain the source snapshot or owning FieldView while using source values and locations.

## Compatibility

C++ API and ABI changes affect the two return/member representations above.
Declaration and field order, profile identity, source diagnostics and budgets retain their contracts.
