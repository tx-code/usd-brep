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

Schema declaration parsing and version recognition live in `StepSchemaSelection.cpp`.
`Outcome::schemaSelection` owns every decoded HEADER identifier, name spelling,
numeric OID arc string, source span and recognition result, including after failed
binding. Null means selection did not run (for example, raw source failure).
Recognition alone never grants an implementation: `qualified` selects an exact,
single implemented source profile, with its validation scope explicit. Existing AP203 diagnostics,
phase order and dependency-visit counts retain their contracts.

The registry recognizes the AP203e1 `CONFIG_CONTROL_DESIGN`, AP203e2 long MIM_LF
name, AP214 `AUTOMOTIVE_DESIGN`, legacy `AUTOMOTIVE_DESIGN_CC2`, and AP242 long
MIM_LF name. AP214 edition 1/2/3 numeric OIDs have stable identity entries;
edition 3 now selects the source-only profile below. Other AP214 versions/aliases
have identity observations but no implemented source profile. A bare name identifies a family only.
Other OIDs remain unregistered; AP242 edition is not inferred from an unchecked
OID convention. Multiple schemas remain unsupported while all declarations,
including unknown additional schemas, are preserved in source order.

Names are not case-folded for admission, OID arcs are never converted to machine
integers or stripped of leading zeroes, and trailing text/malformed braces do not
become a registry match. No identity is inferred from APD text/year or DATA entity
names. Recognized versions have not thereby passed EXPRESS, geometry or native
validation. Additional profiles need audited declarations, checks and their own
dispatch before they can be admitted to the existing pipeline.

The AP214e3 source profile `214e3-P0-Source-0.1` binds exact
`AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}` to STEPcode commit
`24783b448511b8f6bce2829286a3f00015430236`,
[`data/ap214e3/AP214E3_2010.exp`](https://github.com/stepcode/stepcode/blob/24783b448511b8f6bce2829286a3f00015430236/data/ap214e3/AP214E3_2010.exp),
SHA256 `71ab140fe7f774321beee6a31e6fee2afc3973fd60350ae2018c74c211fb4295`.
Its header describes a draft-derived implementation artifact; the filename is
not treated as proof of a certified final ISO publication.

Each snapshot owns its `SelectedProfile()` and `SchemaIdentity()`. Type indexes,
declaration pointers and named field queries use that snapshot's immutable catalog;
there is no process-wide current-protocol switch. AP203 retains its original 72
registered declarations and indexes. AP214 has 70 common declarations, excluding
`MECHANICAL_CONTEXT` and `DESIGN_CONTEXT`, which are absent in the pinned source,
plus 13 presentation/relationship declarations described below (83 total).
The admitted reference SELECT branches remain a closed project subset. This is
source field/part/unit checking, not full-schema or modeling qualification.

The AP214-only increment retains `MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION`,
`STYLED_ITEM`, the fill-area/RGB style chain and its colour ancestors, and
`REPRESENTATION_RELATIONSHIP` / `SHAPE_REPRESENTATION_RELATIONSHIP`. The admitted
SELECT branches are the registered source branches for this subset, and
`SURFACE_SIDE` preserves `POSITIVE` / `NEGATIVE` / `BOTH`. Required fields, transport
types, reference types, aggregate bounds and source identities are checked with
the selected artifact; this does not execute the entities' WHERE clauses.
For example, source field checking alone does not prove `COLOUR_RGB` range
predicates or the additional MDGPR presentation-style restrictions.

The unique source SDR selects either a direct exact-BRep representation, or a
placement-only `SHAPE_REPRESENTATION` root joined to that body by one simple shape
relationship. The linked pair must reference the identical source context in
this project subset. `part.roles` retains `definition_representation` and
`shape_representation_relationship` separately from the body's `representation`.
Both `rep_1` / `rep_2` directions remain source fields; root placement items are
retained and never applied as transforms. Extra relationships, ambiguous bodies,
different shape contexts and transformation entities are unsupported.

Presentation representations become `presentation_N` roles only after checking
that every admitted `STYLED_ITEM` targets an RI in the selected body's use closure.
Their original contexts, names, references, styles and RGB values remain in the
owning source snapshot and its field views. A presentation context does not
replace the body's physical-unit context. Complete ownership still rejects
detached styles or geometry; RI use follows actual representation items and RI
references, without following non-RI style/unit/management paths. These attachment
and ownership restrictions are project guards, not full AP214 rule evaluation.

Generic material/property representations, other presentation branches and new
curve/surface geometry remain outside this increment. Unsupported content is
retained in the raw outcome with diagnostics and no partial typed publication.

AP214 `CONVERSION_BASED_UNIT` redeclares `NAMED_UNIT.dimensions` as derived, so
the original transport value must be `*`. `FieldView` retains the marker, original
slot and declaring/redeclaring types; `ReferenceTarget()` rejects it as a
non-reference. AP203 conversion dimensions retain their explicit reference.
Neither binding rewrites source data or defining 3D curves. Physical conversion
interpretation remains deferred in this layer, with AP214 reason
`Ap214ConversionUnitEvaluationNotRun` and no invented canonical length scale.

The shared single-part, mechanical/design role, complete closure and nominal-unit
restrictions are project guards. Entity WHERE/UNIQUE and AP214's 272 global RULEs
have not been evaluated. `STEP_PLANAR::Check` rejects AP214 at `UnsupportedRuleProfile/D3`
before constructing the AP203 validator; local/global/geometry/conversion checks
stay `NotRun`, and native import makes zero native calls and publishes no model.
The legacy JSON `global_rules_47` key is retained as a compatibility field and
does not describe AP214's rule set.

JSON exposes `schema_selection` separately from the qualified snapshot. Decoded
literals use UTF-8, OID arcs stay strings, and the shared report-byte limit remains
atomic. Native import reports carry the same owning observation as `source_schema`.
The native report's existing `profile`/`schema_sha256` fields describe its requested
implementation; `source_schema.qualified_profile` identifies whether that source
declaration actually selected it.

Identity references: [AP203 naming](https://downloads.steptools.com/docs/stp_aim/notes_ap203.html),
[AP214 editions and legacy alias](https://downloads.steptools.com/docs/stp_aim/notes_ap214.html),
and [Part21 HEADER, clause 8.2.4](https://www.steptools.com/stds/step/IS_final_p21e3.html).

`Snapshot::Field(record, name)` and `Field(record, declaringType, name)` read shared bindings created during the existing
field-validation visits. The returned FieldView owns the immutable snapshot; Value/Declaration references borrow that
lifetime. Location returns independent source facts with owning strings/path. Qualified lookup uses the original
declaring type, preserving inherited and derived slots; `$` and `*` remain source values and are not evaluated.
Missing/ambiguous fields and reference-query misuse are caller errors. FieldView reference queries preserve identity
and aggregate order. Snapshot::ReferenceTarget accepts only the actual reference Value from its owning source graph,
rejecting copied values and values from a distinct source snapshot. Existing indexed Attribute queries are unchanged.

After field validation completes, product, closure and unit interpretation consume those same bindings and resolved
references. Their diagnostics derive component/parameter locations from bindings while retaining the existing choice
to include or omit attribute detail. Raw field/type validation still reads serialized slots before bindings are complete;
the checker does not construct an early Snapshot to use its queries. Public field views share the same lookup.

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
| Binding | Exact single AP203e2 N2635 or AP214e3 name/OID selects its source artifact/profile; no alias/year inference |
| Fields | Existing AP203 simple leaves and six complete complex signatures; AP214 omits the two retired contexts and adds the source leaves above; local/inherited slots, optional/required values, transport types, closed SELECT branches, BOOLEAN, enums, aggregates and protocol-specific derived markers |
| Types | All admitted entity ancestors, including diamonds; explicit constituents and original source identity are retained |
| Part | Unique AC/APD/PC/PDC/product/formation/PD/PDS/SDR/representation/context and consistent reference identities |
| Profile guards | Roles, purposes, category, one shape, explicit outer bound, complete ownership; unknown content is not skipped |
| SI | Context dimension 3, nominal length/plane-angle/optional-solid-angle assignment, the fixed SI prefixes, exact scale to mm/rad/sr |
| Uses | Complete source inverse index, filtered by owner type and declared role; RI use paths do not pass through arbitrary management/unit edges |

AP203 schema identity is the previously pinned MBx N2635 implementation artifact SHA256
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

Not implemented here: AP203's 47 / AP214's 272 global RULEs, other entity WHEREs, named measure predicates,
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
