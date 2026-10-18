# Compile-time STEP catalog design

This records the implemented design for the fixed STEP_SEMANTICS catalogs and
the acceptance requirements for further changes. Current source acceptance
remains owned by [STEP_SEMANTICS](../../source/STEP_SEMANTICS/README.md).

## Scope and intended benefit

Migrate all four profiles: AP203 planar, AP203 general source, AP214e3 source,
and AP214 MBx source, including the general, date/time, presentation, MBx, and
rational-spline extension registrations. Preserve the existing rule-catalog
checks rather than duplicating them.

The required gains are fixed-registration errors rejected during compilation,
no runtime catalog construction/DFS/hash-table setup, string-view lookups without
temporary strings, and static aggregate-dimension views without vector copies.
First access, repeated queries, complete import, and build costs are distinct
measurements. Local baseline observations and raw output stay outside Git;
eliminating initialization alone does not establish overall import speedup.

## Representation and validation

- Keep one private description of shared declarations and each profile's explicit
  differences, with fixed source-artifact/clause references. Add/remove/override
  operations must match the intended unique target; shared transport layout does
  not imply identical schema semantics.
- A consteval builder first validates names, references, inheritance, signatures,
  and dimensions, then computes counts, ancestor membership, and flattened fields.
  Instantiate exactly sized static array pools from those counts. Four immutable
  stores and views live in one owning translation unit; every production profile
  is instantiated in the normal library build.
- Final storage contains static string views, integer IDs, and offset/count
  ranges. Names use a separately sorted lookup index; declaration, parent, field,
  diagnostic, and report order retain their semantic order. Long-lived spans may
  reference only final static storage, never builder temporaries or self-pointers
  baked into a movable result.
- Use one private four-word membership representation for the existing 256-type
  capacity and constant evaluation. Check bounds before indexing/shifting and
  reject 257 types explicitly. This is not a general container-replacement policy.
- Validate unique declarations, valid/acyclic parents, complete ordered complex
  facets/signatures, registered SELECT/entity/enum references, derived
  owner/origin/field relationships, aggregate bounds, and profile operations.
  Preserve explicit exceptions such as the SI-prefix sentinel. These checks
  validate program metadata, not arbitrary external STEP input.

Final catalog definitions must be constant-initialized; moving allocations or
DFS before `main` does not satisfy the design. Fixed validation failures need a
stable catalog diagnostic category, not an incidental compiler error or exhausted
constant-evaluation budget.

## Interface and source ownership

The selected migration includes two C++ representation changes:

- `Snapshot::RegisteredTypes()` becomes `span<const string_view>` instead of
  `const vector<string>&`; callers wanting an editable collection copy explicitly.
- `AttributeView::nestedAggregates` becomes `span<const AggregateDimension>`
  borrowing the static dimension pool. `TypesOf()` remains an owning vector.

Do not retain runtime compatibility caches for the old representations. Migrate
consumers and publish the breaking change with the implementation.
`FieldView` still retains its snapshot; source values and record/facet/slot/span
locations remain owned by that snapshot. Static metadata does not grant source
views an independent lifetime. References must still be the actual values from
the owning source graph, not copied or foreign-snapshot lookalikes.

Unknown types, registered empty populations, ambiguous fields, invalid indices,
and invalid qualifications retain their distinctions. Preserve first-error
priority, source three-valued logic, byte/visit budgets, report structure, and
snapshot publication behavior. Generated catalog optimization does not reduce
the established runtime source-validation charges.

## Implementation and acceptance

1. Establish canonical full-catalog snapshots, allocation measurement with a
   known-allocation positive control, and compile-pass/fail checks using the
   production builder. Compare every profile's source identity, declaration and
   parent order, flattened fields, dimensions, branches, masks, and mappings;
   current CLI observations alone do not cover the full catalog.
2. Migrate the complete description/builder/storage/query representation and
   consumers together, including both interface changes. Cover all real profiles,
   diamonds, derived overrides, aggregate nesting, and membership boundaries.
   Inject one bad registration per negative compile control and require the
   intended validator diagnostic.
3. Run semantic, planar, native-import, witness, and CLI contract checks at their
   relevant scopes. Compare fixed inputs and requests, statuses, diagnostics,
   positions/order, budgets, and failure publication; enumerate any intended
   identity/timing differences instead of ignoring entire report fields.
4. Measure first-access/query allocations, metadata copies, full-import timing,
   clean/incremental builds, compiler peak memory, and binary sizes on matched
   source/toolchain/configurations. Target zero catalog-acquisition/query
   allocations without claiming that the entire import allocates nothing.

Keep computation out of public headers where possible. Record compiler limits
and optimize generation rather than silently restoring runtime construction.
Each implementation batch should be independently reviewable; platform claims
require that platform's compile controls and runtime contracts. Per-run receipts,
source inventories, candidate investigations, and one-off probes stay local.

The [catalog validation tools](../../tools/step_catalog_validation/README.md)
own full-fact comparisons, allocation controls, compiler rejection cases, and
the build-cost measurement procedure. The consteval implementation concentrates
work in one translation unit: assess its peak compiler commit separately from
the aggregate compile time of all replaced catalog translation units. A shorter
build does not establish a lower compiler memory requirement. Keep generation
private so ordinary consumers do not instantiate the builder.
