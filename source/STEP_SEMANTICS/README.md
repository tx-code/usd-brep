# STEP source semantics: ticket 03

Internal C++20 checks on the immutable [STEP_SOURCE](../STEP_SOURCE/README.md)
snapshot. No OCCT, SMLib, USD, STEPcode runtime or EXPRESS compiler is linked.
The existing raw reader and its public behavior remain unchanged.

The internal type catalog owns the fixed complex signatures, exclusive subtype groups, reference SELECT branches,
enumeration and measure choices, SI-prefix powers, and declaration-qualified derived-slot descriptions. It checks
descriptor references against the registered declarations during initialization. The checker retains traversal order,
source diagnostics, budgets and product/unit interpretation; field queries use the same derived-slot descriptions.
This does not extend the admitted profile; existing indexed Snapshot queries retain their contracts.

The checker tracks binding, types/fields, part/closure and units with private typed progress in that order.
The existing Outcome strings are written when Check returns; failure preserves completed phases and leaves later
phases NotRun. Conversion-based units still yield Deferred only after the preceding semantic checks complete.

`Snapshot::Field(record, name)` and `Field(record, declaringType, name)` read shared bindings created during the existing
field-validation visits. The returned FieldView owns the immutable snapshot; Value/Declaration references borrow that
lifetime. Location returns independent source facts with owning strings/path. Qualified lookup uses the original
declaring type, preserving inherited and derived slots; `$` and `*` remain source values and are not evaluated.
Missing/ambiguous fields and reference-query misuse are caller errors. FieldView reference queries preserve identity
and aggregate order. Snapshot::ReferenceTarget accepts only the actual reference Value from its owning source graph,
rejecting copied values and values from a distinct source snapshot. Existing indexed Attribute queries are unchanged.

```powershell
cmake -S source/STEP_SEMANTICS -B C:/temp/step-semantic-build
cmake --build C:/temp/step-semantic-build --config Debug
ctest --test-dir C:/temp/step-semantic-build -C Debug --output-on-failure
& C:/temp/step-semantic-build/Debug/step_semantic_inspect.exe --fixture-limits tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P01/input.step
```

Choose the appropriate local compiler/generator. CMake includes STEP_SOURCE as a
subdirectory and builds both source and semantic contract tests. Root Premake also
registers the static library and CLI, outside the USD stack. This is not a kernel build.

```cpp
#include "StepSemantics.h"

auto raw = step_source::InspectFile(path, step_source::Limits::PlanarFixtures());
auto result = step_semantics::Check(raw, step_semantics::Limits{});
if (result.status == step_semantics::Status::Checked)
{
    auto snapshot = result.snapshot; // Owning read-only source interpretation.
    const auto* loops = snapshot->Instances("LOOP");
    const auto users = snapshot->UsedIn("1", "APPLICATION_CONTEXT_ELEMENT", "frame_of_reference");
}
```

This stage implements:

| Check | Contract |
|---|---|
| Binding | Exact AP203e2 name and numeric OID `{1 0 10303 403 2 1 2}` bind the pinned N2635 artifact; no alias/year inference |
| Fields | The approved 36 simple leaves and six complete complex signatures; local/inherited slot counts, optional/required values, transport types, SELECT branches, BOOLEAN, enums, aggregates and derived-slot markers |
| Types | All admitted entity ancestors, including diamonds; explicit constituents and original source identity are retained |
| Part | Unique AC/APD/PC/PDC/product/formation/PD/PDS/SDR/representation/context and consistent reference identities |
| Profile guards | Roles, purposes, category, one shape, explicit outer bound, complete ownership; unknown content is not skipped |
| SI | Context dimension 3, nominal length/plane-angle/optional-solid-angle assignment, the fixed SI prefixes, exact scale to mm/rad/sr |
| Uses | Complete source inverse index, filtered by owner type and declared role; RI use paths do not pass through arbitrary management/unit edges |

Schema identity is the previously pinned MBx N2635 implementation artifact SHA256
`c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088`.
It is not represented as a certified ISO publication. See the
[context](../../docs/step-import/ap203e2-planar-context-closure.md) and
[geometry](../../docs/step-import/ap203e2-planar-geometry-closure.md) audit tables.
Serialization facts (REAL transport, BOOLEAN, SELECT, external facet ordering and
explicit-to-derived markers) were checked against the editor-hosted
[Part21 mapping clauses](https://www.steptools.com/stds/step/IS_final_p21e3.html#clause-12).
This does not grant edition-3 or full Part21 support.

External facets must be complete, unique, in ascending name order and match one
registered signature. Audited mutually exclusive unit/context branches reject
as mapping conflicts. A supported sibling signature shares its ancestor instance;
it does not create three REP_CONTEXT objects or duplicate inherited RI.name.

`Snapshot` shares the original owning source. Per-record compact membership bits
and population indices reference its records. Attribute views point directly into
the original immutable values; there is no second full typed entity/value graph.
Const views remain valid while a snapshot owner lives. Reference roles preserve
one-based slot/aggregate paths and original byte spans.

`Instances(type)` returns null for an unregistered type and an actual empty vector
for a registered zero population. `IsA` and `UsedIn` reject unknown queries/roles;
missing support cannot become an invented empty-set proof. The registered catalog
covers profile ancestors and selected guards, not all N2635 declarations. General
EXPRESS TYPEOF/QUERY/USEDIN and RULE execution remain later work.

The single-part and signature restrictions are project profile guards. They are
not general AP uniqueness rules: PRODUCT.id is not treated as UNIQUE. A file with
two products is refused as outside this slice rather than merged by its labels.
APD strings/year remain separate source claims; the APD global RULE is not run in
03, so N10's wrong suffix is preserved without being replaced by FILE_SCHEMA.

The structural checker distinguishes empty string from missing `$`, explicit
values from derived `*`, BOOLEAN from LOGICAL U, and ID equality from coincident
geometry. N07 fails VERTEX reference typing even though its CP has the same position.
PATH's LIST UNIQUE checks OE identity; distinct OEs may reference one underlying EC.
The latter's connection/direction/geometry remain ticket 04 checks.

SI scale is an exact power of ten expressed as a rational string pair, never a
binary float. The pinned prefix enum has exa through atto, including hecto/deca;
modern unlisted prefixes are rejected. Required radian fields are checked even
when LINE/PLANE do not consume a physical angle. Radian and steradian retain
different nominal types despite identical dimension exponents.

Coordinates and VECTOR magnitude are lengths, direction ratios and LINE t are
dimensionless, source PLANE parameters are lengths. This stage reports those
contracts and source scales; it does not convert geometry, reparameterize UV,
normalize directions or change source coordinates/tolerances.

Outcome statuses are deliberately limited:

- `Checked`: all promised ticket-03 checks ran; it is not full schema/model acceptance.
- `Deferred`: fields and the product graph are checked, but the required conversion
  length scale is not interpreted. `lengthPower10Mm`/JSON scale is absent, with an
  explicit ticket-09 reason; no inch-name heuristic or fallback millimetres.
- `Rejected` / `Unsupported`: precise source counterexample versus profile boundary.
- `SourceFailure`: the underlying reader failed; no partial semantic snapshot.
- `ResourceLimit`: configured semantic traversal or raw-layer limits blocked completion.

Only complete type/part information can produce a semantic snapshot. A deferred
snapshot carries partial unit qualification explicitly; callers must inspect status
and optional scale. A rejected/unsupported result retains its raw source outcome
but does not publish a partial semantic graph.

Not implemented here: 47 global RULEs, other entity WHEREs, named measure predicates,
conversion factor/dimensions/cycle interpretation, uncertainty evaluation, geometric
consistency/region checks and native construction. The two legacy-context predicates
and context dimension_count positivity are explicitly checked where interpreted;
they do not imply a general evaluator. Source shape roles are claims, with
`shape_geometry_verified=false`. Closed-shell text does not prove closedness.

The carrier remains STEP_SOURCE's ASCII `2;1`/single-DATA slice. The semantic test
preset bounds dependency visits to 1,000,000; callers can override
`--max-semantic-visits`, with a hard ceiling of 10,000,000. Counts cover record/field
and dependency traversal visits, not CPU instructions, time or process memory.
All raw byte/record/value/ref/depth/string/report bounds are retained. The final
combined raw+semantic report obeys the original report-byte budget with no partial JSON.

CLI codes: 0 Checked, 7 Deferred, 1 Rejected/source rejected, 2 Unsupported, 3 limit,
4 raw I/O failure, 5 report limit, 6 infrastructure failure, 64 arguments. Native
BRep construction and complete import remain NotRun in all reports.

See the [independent verification tool](../../tools/step_semantic_validation/README.md)
for frozen-corpus, declaration-witness and diagnostic evidence.
