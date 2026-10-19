# STEP source semantics: ticket 03

The two fixed AP214 catalogs admit ordinary `PRODUCT_CATEGORY` and
`PRODUCT_CATEGORY_RELATIONSHIP` with the pinned AP214E3_2010.exp
(`71ab140f...`) transport: category name/optional description, and relationship
name/optional description/category/sub_category. Computed category.id has no
P21 slot; it is not synthesized. The closed catalogs still reject ID_ATTRIBUTE,
so its incoming count for PRODUCT_CATEGORY.WR1 is zero in this subset; no general
derived-id or ID_ATTRIBUTE evaluation is claimed. Relationship WR1's absence of
category cycles is checked by a budgeted iterative traversal, including self
links and shared descendants. Ordinary dictionary categories do not create
PRODUCT membership, a selected-part category or a new geometry root.

Unattached category dictionaries and their checked relations may be retained
only under explicit `PreserveNonGeometric`, through the existing orphan
preservation contract: preserve the original `UnownedOrOrphanProfileContent`
diagnostic and complete records, with all outgoing relations restricted to
typed categories. Strict requests keep the orphan refusal. The existing
PRODUCT_RELATED_PRODUCT_CATEGORY membership/name guard is unchanged. N2635's
existing standalone dictionary behavior is unchanged; N916, N2635 and P0 do
not acquire the new hierarchy declarations.

AP214 also admits ordinary `INVISIBILITY(invisible_items)` and its required
nonempty SET. The fixed invisible_item SELECT includes styled items, layers,
representations and draughting callouts. This subset admits the first three
only where already supported: the seven-arc catalog still excludes layers,
while the separate MBx catalog supports them. Callouts and context-dependent
invisibility remain unsupported. Every target must already belong to the
proved selected part/presentation closure before INV acquires its own source
role; an invisible reference cannot establish a new owner or hide required
geometry, units or diagnostics. Its raw targets and instruction remain in the
owning source snapshot/report. Native construction and queries retain the
complete model; applying visibility in a renderer is not implemented or claimed.

An application may separately request `SchemaInterpretation::UnregisteredAp214As2010`
with general-BRep scope and `SourcePolicy::PreserveNonGeometric`. Only the exact
unregistered identifier `AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1 1}` may select the
existing fixed `214-MBx2010-Source-0.1` implementation under this assumption.
Its catalog and pinned EXPRESS bytes are reused; this does not register an OID,
authenticate an edition, or equate it with the seven-arc identifier. Registered
bindings still select their ordinary implementation; there is no retry after a
body check fails and no arbitrary schema override.

The HEADER retains `UnregisteredVersion`, all eight original arcs and its empty
registry ID. `schema_interpretation` identifies the request/application, reports
`source_binding_verified=false` when applied, and retains the exact original
`UnsupportedSchemaBinding` in `original_binding_diagnostics`. Binding progress is
`InterpretedUnverified`; the owning snapshot exposes `AppliedInterpretation()`
and `InterpretationDiagnostics()`. These assumptions are distinct from proven
nongeometric exceptions, while sharing their configured diagnostic-storage quota.
All required types, dependencies, units and remaining checks still run. The
default policy remains `DeclaredOnly`; P0/strict requests cannot enable this mode.
The semantic CLI flag is `--interpret-unregistered-ap214-2010`, together with
`--general-brep-source --interop-nongeometric`. This source-only result does not
grant native geometry or ordinary source-binding qualification.

Explicit `GeometryScope::GeneralBrep` also selects `203-N916-Source-0.1` for a
single bare `CONFIG_CONTROL_DESIGN` HEADER. The implementation uses
[STEPcode's fixed modified N916/AP203 amendment 1 artifact](https://raw.githubusercontent.com/stepcode/stepcode/87e1f9948c60837c2a1664cd86e00e3962a02ff5/data/ap203/ap203.exp),
SHA `020b4d25dbd0b6ee7d15099b978e3448f6699a72cb862d381e416e32187562f1`.
This is a separately identified source subset, not a N2635 alias or proof of an
ISO edition. The HEADER retains `NameOnly`, its original literal/span and empty
OID/registry; the selected implementation reports
`binding_basis=NameOnlyGeneralSourceSubset` and `header_edition_verified=false`.
Default/P0 selection, neighboring OIDs, multiple schemas and other names do not
gain this binding. APD/year values cannot select or certify the implementation.

The new catalog reuses compared transport descriptions at constant evaluation,
with N916's required descriptions, direct PDR property reference, actual
administrative SELECT branches, UTC type aliases and two-value offset enum.
Its 137 registered transport descriptors comprise 117 declarations present in
the fixed artifact and 20 borrowed N2635 diagnostic descriptions. Registration
and `IsA` do not prove source conformance: every explicitly instantiated foreign
type is refused. Only `PRESENTATION_LAYER_ASSIGNMENT` may survive explicit
nongeometric interoperability, with its original `UnsupportedEntityType`, after
all fields, selected-part attachment and physical-dependency exclusion pass.
It cannot create a geometry root, unit or inferred part owner.
Malformed foreign-layer fields fail the borrowed N2635 transport prerequisite
as `ProfileUnsupported`; they are not labelled N916 source-rule violations.
The same explicit
policy may retain the failed single-APD guard for zero APDs after the remaining
unique product chain and units complete; no APD, role, year or claim is fabricated.
Strict requests and multiple APDs keep their original failure behavior.

N916 conversion dimensions are explicit. Its CBU has no WHERE rule, and
`derive_dimensional_exponents` reads the named unit's transmitted dimensions.
This subset separately requires a finite conversion graph, a coherent factor
dimension and a positive scalar before establishing a physical conversion.
Failure of those project prerequisites is `ProfileUnsupported`, not an invented
`CONVERSION_BASED_UNIT.WR1`; original dimensions/scalars remain unchanged.
The common geometry prerequisites and nominal/value-unit rules run with this
artifact's own responsibility labels. Complete management/date TYPE predicates,
WHERE/global-rule conformance and native angular conversion remain unqualified.
Native construction keeps its separate geometry, tolerance and publication gates.

Explicit general interoperability may preserve unavailable DATA text in exactly
`PRODUCT.id`, `PRODUCT.name` and
`PRESENTATION_LAYER_ASSIGNMENT.name`. Source capture must separately select its
raw DATA-string policy. Typing retains `OpaqueDataTextUnestablished` as
`ProfileUnsupported`, never a decoded/empty label or a proven malformed value.
Final approval requires the actual selected product, or a layer
whose complete attachment was already proved. `REPRESENTATION.name` remains
unsupported when opaque: the current profile consults that name for special
representation-purpose exclusions, which unknown text cannot silently satisfy.
Other fields, protocol
claims, property-rule names, units and unapproved owners remain unsupported.
`CheckedWithDiagnostics` and owning `OpaqueString` values expose this limitation;
the source bytes, field roles, scope and all other checks remain intact. Strict
semantics and P0 do not gain this exception. Both source policy flags are exposed
by the semantic CLI, independently of `--interop-nongeometric`.

The three explicit general profiles also retain `TRIMMED_CURVE`, its original
`basis_curve`, both `SET[1:2]` cuts, sense and master preference. The pinned
N2635 / AP214E3_2010 declarations admit only `CARTESIAN_POINT` references or
typed `PARAMETER_VALUE` REALs in each cut. Exact SET duplicates and the distinct
WR1/WR2 same-SELECT-type restrictions are checked separately. Iterative cycle
rejection establishes a finite trimmed-basis dependency chain as a project
prerequisite, not an additional EXPRESS WHERE rule. Source checking alone does
not establish that a point lies on the basis or that dual representations agree;
those require the selected numerical/native checks. P0 remains closed.

General source scope also admits `GEOMETRIC_CURVE_SET` and
`GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION` from the pinned AP214,
N2635 and modified N916 artifacts. `GEOMETRIC_SET` is a guarded ancestor;
its ordinary leaf is not admitted. The inherited `elements` field is a nonempty
SET of POINT/CURVE/SURFACE references; GCS.WR1 excludes surfaces but permits
points. Source order does not create connectivity, and coincident distinct
source points retain their identities. Computed GRI.dim has no transmitted slot.

For an admitted wireframe representation, WR1/WR2 check the permitted item
categories and at least one curve set. WR3/WR4 use the fixed-source curve/point
predicates restricted to registered branches; WR5 checks direct conic placement.
Bare LINE is not a bounded wireframe member; a TRIMMED_CURVE with LINE basis
is. Trimmed dependency cycles fail before evaluating the wireframe predicates.
MAPPED_ITEM, POLYLINE, replicas and composite curves remain unsupported at typed
binding, so their WR6/WR7 branches are not executed or claimed as checked.
These checks establish source prerequisites, not geometric trim consistency,
continuous numerical bounds or complete AP/AIC conformance.

The closed source graph may retain one BRep and one **distinct** wireframe,
joined by ordinary `SHAPE_REPRESENTATION_RELATIONSHIP` instances to the selected
root. The root is either the BRep itself with one wireframe link, or a retained
placement-only SHAPE_REPRESENTATION with two links. Relation endpoints may be
reversed; their source order is preserved. Every representation must use the
identical source context. These are project selection restrictions: no transform,
unit scaling, equivalence, composition, Boolean union or display priority is
inferred from a simple relationship. Separate roles
`wireframe_representation` and `wireframe_shape_relationship` retain its source
identities alongside the existing BRep/root roles. `source_shape_role` still
describes the selected BRep branch, not the relationship as a material whole.
Physical-dependency exclusion and selected-geometry presentation attachment
include the wireframe; it is never demoted to nongeometric metadata. Additional
or ambiguous geometry blocks the request. Native construction has a separate
[linked-wireframe gate](../STEP_IMPORT/README.md).

The three explicit general source profiles admit `TOROIDAL_SURFACE` with its
inherited elementary-surface placement and two original positive length radii.
Pinned N2635 and AP214E3_2010.exp impose no entity WHERE ordering those radii;
`TORUS.WR1` belongs to the separate CSG entity. Horn/spindle source values are
retained, while native ring construction has its own narrower capability gate.
The default planar P0 profile is unchanged.

The exact `AUTOMOTIVE_DESIGN {1 0 10303 214 1 1 1 1}` identifier selects
`214-MBx2010-Source-0.1`, with registry identity `ap214-mbx-2010`. The
[MBx-IF EXPRESS download table](https://www.mbx-if.org/home/mbx/resources/express-schemas/)
lists this spelling for its AP214E3_2010 archive. Its pinned EXPRESS header instead
states version(3), so this binding deliberately retains a separate identity and
the original eight arcs, rather than inferring an ISO edition or removing an arc.
It uses the same audited source declarations/hash as the existing seven-arc
AP214e3 subset. Neighboring OIDs, name-only declarations and other editions do not
inherit this binding; global rules and native qualification remain separate.

Only this separate MBx subset has compatibility transport for legacy
`MECHANICAL_CONTEXT` / `DESIGN_CONTEXT` fields from pinned N2635. Strict checking
rejects those foreign types. Explicit general-BRep interoperability may preserve
the original type errors after validating their fields and proving that each is
the selected product/definition context, outside physical dependencies. They are
not relabelled as current AP214 types. The existing seven-arc subset stays closed.

The same MBx subset has separately pinned N2635 transport for the foreign
`REAL_REPRESENTATION_ITEM` leaf, which is absent from fixed AP214. Only explicit
general-BRep nongeometric interoperability may retain it: the original strict
`UnsupportedEntityType` remains, and final approval requires actual item
membership in a proven selected-part property representation plus complete
physical-dependency exclusion. Detached items, geometry roots, expression
siblings and additional complex mappings remain unsupported. Malformed borrowed
fields are `ProfileUnsupported` with N2635 transport provenance, not invented
AP214 source-rule failures. Existing RI missing-label retention still needs the
same final property proof and does not manufacture a name.

The ten inherited types preserve two source slots: `REPRESENTATION_ITEM.name`
and `LITERAL_NUMBER.the_value`. A shared compile-time explicit redeclaration
keeps the original `NUMBER` declaration while `REAL_LITERAL` narrows the
effective field to `REAL`; `redeclaredBy` identifies that subtype while
`derivedSlot` stays false. This is not an explicit-to-DERIVE marker and adds no
slot. The closed compiler currently supports exactly this NUMBER-to-REAL scalar
narrowing and rejects missing owners/origins/fields, unsupported narrowings and
ambiguous or derived-slot conflicts. No old type identities move, and the other
four catalogs retain their facts.

The original numeric token and property context remain unchanged. No unit,
scale, positivity or measurement meaning is inferred from names or contexts.
The mathematical ancestry does not enable variables, general expressions, SQL
or derived-value evaluation; N2635's computed `is_int` and `sql_mappable`
attributes have no transmitted slots. This diagnostic transport does not grant
N2635/AP214 full conformance or authenticate an OID. Explicit unverified schema
interpretation keeps its separate qualification and original binding diagnostic.

The MBx subset also registers general-property associations, overriding styled
items and RI presentation layers from the pinned AP214 artifact. Associations
check their actual WR1/WR2 and attach only to an already proven selected-part
property; inverse links never grant property ownership. Every overridden style
must target the body's existing RI closure; cycles are explicitly unsupported.
An invalid override reference to the selected `CLOSED_SHELL` may be retained only
under explicit general-BRep interoperability, after proving both the style's
presentation attachment and its exclusion from physical inputs. The original
`ReferenceTypeMismatch`, raw reference and shell remain unchanged; the shell is
never interpreted as an ancestor style. Strict mode still rejects it.
The same explicit MBx interoperability scope may retain the original failed
single-APD guard when no APD record exists. The remaining unique product chain
and physical-unit checks must still complete. No APD role is created; status,
schema claim and year are null/unavailable in JSON. The AP214 APD-required global
rule is not thereby satisfied or qualified, and multiple APDs remain unsupported.

The explicit general N2635/AP214 subsets admit rational B-spline curves and
surfaces with explicit knot facets. `weights_data` retains source REAL tokens,
U-row/V-entry order and dimensionless values. Cardinality, rectangular shape and
strict positivity are checked before construction; exact source predicates do
not round tiny positive values to zero. The derived `weights` value has no P21
transport slot. The general catalogs contain 137/124 declarations respectively;
the separate MBx general catalog contains 139 declarations;
the default P0 catalog remains at 72. This is not admission of other rational
uniform/quasi-uniform mappings or complete EXPRESS conformance.

Both AP214 bindings also admit the pinned `CURVE_STYLE` declaration and the
predefined draughting-font inheritance chain. All four style fields are required;
font names satisfy their exact five-name WR1. `SIZE_SELECT` retains a typed
positive length, typed descriptive string, or generic `MEASURE_WITH_UNIT`
reference with its original nominal unit. Referenced positive/nonnegative length
measures satisfy their own TYPE predicates. The admitted font branch excludes
custom, external and font-scaling extensions. Curve-style RGB dependencies,
same-type curve-style duplicates in `PRESENTATION_STYLE_ASSIGNMENT.WR1`, and
`styled_curve.WR1` assignment counts are checked; this is not full AP214 global
validation or a promise that a viewer renders these styles.

Both AP214 bindings also admit `DRAUGHTING_PRE_DEFINED_COLOUR` from the same
fixed `AP214E3_2010.exp`. Its `PRE_DEFINED_COLOUR` parent inherits
`PRE_DEFINED_ITEM` before `COLOUR`; the leaf transmits exactly one required
`PRE_DEFINED_ITEM.name: LABEL` and no derived slots or RGB channels. WR1 checks
the decoded STRING against the exact case-sensitive names `red`, `green`,
`blue`, `yellow`, `magenta`, `cyan`, `black`, `white`. Missing, opaque or invalid
names stay failures even under interoperability; no colour is synthesized.
Existing fill, rendering and curve-style `COLOUR` references retain their
declared types and source identities. Bare parents and additional complex
mappings stay unsupported. Existing type IDs are preserved by appending the
two declarations after prior entries; the other three catalogs are unchanged.

This does not establish full global-rule conformance or certify an unregistered
OID. Strict orphan colours keep the existing ownership failure; explicit AP214
interoperability may retain proven nonphysical appearance residue with that
original diagnostic, without a fabricated part or geometry role. The dependent-
instantiable and subtype global RULEs are not newly advertised as executed.
RGB names and channel rules remain independent of the predefined palette.

Only explicit general-BRep nongeometric interoperability may retain an invalid
STRING width with its original `AttributeTypeMismatch`. It remains a STRING;
its content is not parsed into a measure or replaced by a default. An internal
typed pending proof requires selected-part presentation attachment and complete
physical-dependency exclusion before publication. Detached/foreign styles or
ambiguous reference errors cannot use that exception. Strict mode keeps its
original rejection. Neither admission nor isolation creates geometry ownership.

The AP214e3 source subset also retains applied date/time assignments and the
rendering/transparency style chain from fixed `AP214E3_2010.exp` (71ab140f...).
Date transport declarations are shared with the compared N2635 fields, while
each protocol keeps its own assignment subtype and admitted SELECT branches.
Calendar values, clock fields and zone markers stay as written: calendar/time
WHERE predicates, date range TYPE predicates and UTC/epoch conversion are not
implemented or claimed. Date assignments must target already owned part content;
an inverse link cannot give an unattached property or shape a geometry root.

Rendering enums/references/cardinalities are checked, together with
`SURFACE_STYLE_TRANSPARENT.WR1` and the distinct-property-TYPEOF requirement in
`SURFACE_STYLE_RENDERING_WITH_PROPERTIES.WR1`. The rendering method is required.
Strict mode rejects `$`; explicit interoperability may retain that original
error only on a proven selected-body presentation, outside physical dependencies.
It does not choose a display method or change geometry. Invalid other enum
values, missing colour references and unattached missing methods remain failures.

Two additional property-only unit spellings can be retained diagnostically under
the same explicit policy: a well-typed explicit dimensions reference where an
AP214 mass conversion unit requires `*`, and `SI_UNIT.WR1` for non-KILO GRAM used
in a derived unit. Original references/prefixes and strict errors remain intact.
Complete property-item dependency and physical-closure checks precede approval:
sharing such a unit/factor with the body or its unit context rejects the import.
These are nonconforming source metadata, not clean units or repaired quantities.
Unit cycles, wrong reference types, nonmass marker errors and dimensional
contradictions remain fatal. Existing MRI value/unit errors keep their own
original diagnostics and independent ownership checks.
The retained explicit mass dimensions must equal the factor-derived vector;
`MetadataMassUnitDimensionsConflict` is a project compatibility guard, not an
invented AP214 WHERE clause.

Both AP214 source bindings also admit `PLANE_ANGLE_MEASURE_WITH_UNIT`, the
`PLANE_ANGLE_MEASURE` value branch and the conversion-based plane-angle complex
mapping from the same fixed `AP214E3_2010.exp`. Generic MWU `valid_units` checks
that this value branch has a zero dimensional vector; the angle MWU subtype's
own WR1 additionally requires a nominal `PLANE_ANGLE_UNIT`, independently of
the value tag. A dimensionless solid-angle or plain named unit therefore cannot
satisfy that subtype. Original values, conversion-unit names, references and
derived-slot markers remain unchanged. Source scalar conversion evaluation stays
`NotRun`; no radian scale or positivity requirement is invented by this source
declaration. Native construction keeps its separate angular-conversion gates.

An explicit `GeometryScope::GeneralBrep` request also selects the separate
`203e2-G0-Source-0.1` subset for the exact N2635 name/OID. The default P0 catalog,
rule admission and six-signature contract stay unchanged. G0 shares the audited
conic/knot and presentation declarations with the AP214 source subset, retains
AP203 legacy contexts and its explicit conversion-unit dimensions, and adds
selected approval/security/person/date, layer and value-item declarations from
the same fixed N2635 artifact. It is a source subset for general development
construction, not complete N2635 management WHERE/global-rule qualification.

Administrative attachments require a fixed-point proof that every target is
already owned by the selected part; metadata cycles cannot create geometry roots.
`VALUE_REPRESENTATION_ITEM` retains NUMBER/REAL lexical forms and its owning
representation's assigned unit context. No implicit unit is fabricated. Explicit
conversion dimensions must agree with the factor unit and conversion scales must
be positive; angle conversion scale evaluation remains outside this source step.

Only explicit `PreserveNonGeometric` may retain legacy CC approval SELECT
violations for the selected PD/formation, after attachment and nonphysical-use
proofs. It keeps the original diagnostic and source bytes. Unused, reference-free
category dictionaries may survive as diagnosed unattached metadata. Foreign
owners, unsupported branches, physical unit errors and geometry remain failures.
The general development path and its report retain their separate qualification;
this does not grant the old planar rules to newly admitted content.

Internal C++20 checks on the immutable [STEP_SOURCE](../STEP_SOURCE/README.md)
snapshot. No OCCT, SMLib, USD, STEPcode runtime or EXPRESS compiler is linked.
The existing raw reader and its public behavior remain unchanged.

The internal type catalog owns the fixed complex signatures, exclusive subtype groups, reference SELECT branches,
enumeration and measure choices, SI-prefix powers, and declaration-qualified derived-slot descriptions. All five
profiles are validated and generated by a private consteval builder in the ordinary C++20 library build. Final arrays
and their read-only views are constant-initialized; acquisition does not construct hash tables or inheritance closures.
Invalid fixed registrations fail compilation with a `STEP_CATALOG_INVALID_...` category. This validates program metadata,
not external STEP data or complete EXPRESS semantics. The checker retains traversal order,
source diagnostics, budgets and product/unit interpretation; field queries use the same derived-slot descriptions.
Catalog ownership does not itself grant admission; the implemented subsets are listed below.
Existing indexed Snapshot queries retain their contracts.

The checker tracks binding, types/fields, part/closure and units with private typed progress in that order.
AP214 additionally checks selected source unit dimensions, then geometry dependencies, between fields and part/closure;
AP203 retains its existing phases, diagnostics, report bytes and traversal charges.
The existing Outcome strings are written when Check returns; failure preserves completed phases and leaves later
phases NotRun. Conversion-based units still yield Deferred only after the preceding semantic checks complete.

The two-argument `Check` remains strict. Its explicit policy overload and CLI
`--interop-nongeometric` can preserve a closed set of nondefining issues for AP214:
missing RI/REP/style labels, arbitrary labels on the uniquely linked definition
context/product category, unattached declarations wholly within the admitted
nongeometric style chain, and the selected MRI ratio/value-unit predicates.
An orphan STYLED_ITEM is preservable only when its explicit target is already in
the selected body's RI closure; it remains without a fabricated presentation owner.
Original values, unset markers, diagnostic keys/classification/locations and raw
bytes stay intact. Style residue is exposed as `UnattachedNonGeometricDeclarations`
and is never assigned a fake part root.

MRI errors remain provisional until complete property attachment and a separate
outbound closure from the selected body and its physical unit context prove that
the item is metadata only. A shared physical conversion factor, unowned MRI,
geometric data error, unknown branch or unit dependency cycle remains fatal.
`Limits::preservedDiagnostics`, visits and report bytes bound this work; excess
issues are not discarded. `PreservedDiagnostics` belongs to the owning snapshot
and is also reported separately from fatal `diagnostics`. Successful preservation
returns `CheckedWithDiagnostics`, with corresponding phase markers; it does not
claim strict source conformance or qualify a native model. Failed requests expose
no approved partial snapshot or preserved-issue list.

The separate `GeometryScope::GeneralBrep` / `--general-brep-source` admits legal
ordinary FACE_BOUND regions without choosing an outer bound. Such source faces
are retained in `Part::deferredRegionFaces`, and the source result remains
`Deferred` with `GeneralBrepRegionClassificationNotRun`. Native construction must
prove their regions before publishing a model. Multiple explicit outer bounds
remain rejected. The default `ExplicitOuterBounds` scope and all existing strict
AP203 behavior remain unchanged; neither source option bypasses geometric checks
or the existing downstream AP203 validator/native qualification gates.
The independent `interoperability_approval` phase records final proof/budget
failures; a typed source snapshot is not evidence of native construction.

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
metadata views and named field queries use that snapshot's immutable catalog;
there is no process-wide current-protocol switch. AP203 retains its original 72
registered declarations and indexes. AP214 has 70 common declarations, excluding
`MECHANICAL_CONTEXT` and `DESIGN_CONTEXT`, which are absent in the pinned source,
plus 13 presentation/relationship, 11 geometry, one mass-measure and two metadata-item declarations (97 total).
The AP214 unit increment also replaces its existing derived-unit guard entries with exact source fields.
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

Generic material/property representations use the attachment subset below; other
presentation branches remain unsupported. Unsupported content is retained in the
raw outcome with diagnostics and no partial typed publication.

The AP214 geometry source subset adds `CONIC` / `CIRCLE` / `ELLIPSE`,
`CYLINDRICAL_SURFACE` / `CONICAL_SURFACE`, and nonrational
`B_SPLINE_CURVE_WITH_KNOTS` / `B_SPLINE_SURFACE_WITH_KNOTS` with their bounded/B-spline
ancestors. Conic `AXIS2_PLACEMENT` admits the existing 3D branch; 2D placements,
rational/other spline forms and unsupported complex signatures fail explicitly.
No weights or defining geometry are discarded to fit this subset.

`geometry_dependencies` records the separate AP214 source phase. It checks the
positive-length predicate for radii/semi-axes, cone radius >= 0, rectangular control
grids required by the surface DERIVE arrays, knot/multiplicity sizes, and the
pinned `constraints_param_b_spline` function: degree >= 1 and below control count,
positive bounded multiplicities, their required sum and strict distinct-knot order.
Endpoints may have degree+1 multiplicity; interior knots may have at most degree.
Original knots are compared exactly from decimal source text, including huge
exponents; no binary-float conversion or power expansion occurs.

Original REAL/INTEGER lexemes, ellipse axis order, cone angle-unit values,
control-point row/column order and repetitions, multiplicities and distinct
knots remain immutable. `LOGICAL` preserves `T`/`F`/`U`; no unknown flag becomes
false. `AttributeView::nestedAggregates` and JSON `nested_aggregates` describe
inner list bounds only when a field is nested. A grid remains nested in `Value`;
`FieldView::ReferenceTargets()` accepts flat lists only, while individual grid
references can be resolved through `Snapshot::ReferenceTarget()`.

Source dependency progress/counts are separate from full AP214 qualification.
`other_entity_where` and `named_measure_predicates` report `Partial` only if the
corresponding selected source predicates were attempted; all remaining clauses,
uniformity claims in `knot_spec`, actual closedness/self-intersection, geometric
consistency, parameter conversion/reparameterization and full AP214 RULEs are
still unverified. B-spline parameters are retained `PARAMETER_VALUE`s: no native
domain, period or physical UV scale is invented. Coordinate/radius values remain
in original length units and cone `semi_angle` in original angle units; canonical
unit metadata never rewrites these fields. A source failure, budget stop or failed
dependency publishes no typed snapshot and leaves later phases unrun.

AP214 `CONVERSION_BASED_UNIT` redeclares `NAMED_UNIT.dimensions` as derived, so
the original transport value must be `*`. `FieldView` retains the marker, original
slot and declaring/redeclaring types; `ReferenceTarget()` rejects it as a
non-reference. AP203 conversion dimensions retain their explicit reference.
Neither binding rewrites source data or defining 3D curves. Source dimensions are
computed by the AP214 unit pass below; physical conversion
interpretation remains deferred in this layer, with AP214 reason
`Ap214ConversionUnitEvaluationNotRun` and no invented canonical length scale.

AP214 `DERIVED_UNIT` retains `elements : SET[1:?] OF DERIVED_UNIT_ELEMENT`;
its derived `name` is not a serialized slot. Each element retains its original
named-unit reference and REAL exponent. The profile admits SI mass and conversion
mass signatures, `MASS_MEASURE_WITH_UNIT` and its typed
`MASS_MEASURE` branch. AP203 retains its original six signatures and length-only
measure branches. A plain `NAMED_UNIT` can retain explicit seven-component REAL
dimensions without acquiring an inferred nominal type or physical scale.

The separate AP214 `unit_dependencies` phase computes dimensions in
length/mass/time/current/temperature/amount/luminous order. It uses explicit
dependency frames and memoization, including conversion-factor unit references;
cycles fail before recursive dimensional evaluation. It checks
`DERIVED_UNIT.WR1`, selected nominal length/mass/angle predicates,
`SI_UNIT.WR1` and selected `MEASURE_WITH_UNIT.WR1` / subtype unit predicates.
The SI mass prefix requirement uses actual element references, including zero
exponents; a transitive reference through a conversion factor is not the same
`USEDIN` condition. The SI interpretation subset is metre/gram/radian/steradian;
other nominal families and SI names remain explicit unsupported cases.

Dimension exponents use exact bounded fractions, never double, tolerance-based
comparison or unbounded exponent expansion. Checked arithmetic capacity failure
is `UnitDimensionCapacityExceeded/ResourceLimit`, not a source-invalid verdict.
Raw exponent tokens, element IDs/order, equivalent or cancelling basis units,
factor values, prefixes and derived markers stay unchanged. Zero contributions
do not remove elements, cycles or SI prefix obligations. Dimensions alone never
promote a derived unit to a named mass/length/angle unit.

Only a complete source result exposes `Units::dimensionDefinitions` (and JSON
`units.dimension_definitions`) as separate derived facts with owning rational
strings. Source field views still return original values/references. SI mass
metadata uses canonical kg, alongside mm/rad/sr; no derived scale or conversion
factor is evaluated, and geometry values are not rescaled. Later part/context
failure withholds the typed snapshot even if unit prerequisites passed.

AP214 `MEASURE_REPRESENTATION_ITEM` retains both parent roles: simple mapping
transmits RI `name`, then MWU `value_component` and `unit_component`. Its complete
external mapping has the empty MRI facet plus the two parent facets (nine admitted
signatures in total). Fields retain original source values, REAL lexemes, typed
measure branches and unit references; names never infer or replace a quantity.
The added `RATIO_MEASURE` and `POSITIVE_RATIO_MEASURE` SELECT branches remain REAL
transport. The positive branch checks its exact `> 0` predicate, including signed
zero and extreme exponents, and inherits ratio's dimensionless `valid_units` check.
A density-named positive ratio paired with M/L^3 fails `MEASURE_WITH_UNIT.WR1`
without changing its source branch, value or unit. Unknown branches stay unsupported.
`unit_type_predicate_checks` and `named_measure_predicates` report actual attempts;
they do not grant complete type or entity validation.

Admitting this source entity alone does not make an orphan owned or allow non-shape
items in the selected body's representation. MRI-as-conversion-factor tests isolate
the existing MWU reference/field contract; they do not claim full RI.WR1 or AP214
global-rule validation, physical conversion evaluation or native construction.

The AP214 property subset admits simple `PROPERTY_DEFINITION_REPRESENTATION`,
`PROPERTY_DEFINITION`, ordinary `REPRESENTATION` and `REPRESENTATION_CONTEXT`,
plus `DESCRIPTIVE_REPRESENTATION_ITEM` with inherited RI name and required description.
Their derived names, IDs and descriptions do not become extra transport slots.
`CHARACTERIZED_DEFINITION` remains limited to the registered PD/PDS branches, and
`REPRESENTED_DEFINITION` to property definitions; this is not the full schema SELECT.
The existing nine complete complex signatures are unchanged; the new DRI leaf uses
simple mapping only. Unsupported external mappings fail explicitly.

Part/closure checks each non-SDR property link before granting it a root: the
property definition's original characterized reference must be the selected PD or
selected PDS, and its used representation must be ordinary REP containing only
MRI/DRI metadata items. An inherited PDS property can supply the same selected PD
identity. Geometry, placements and styles cannot be attached as property items;
shape/presentation representations cannot become property representations. These
are project ownership guards, not new EXPRESS WHERE clauses or name-based inference.

`Part::propertyAttachments` and JSON `part.property_attachments` retain each source
link, property definition, characterized definition, selected product definition,
representation and context identity. Repeated links/names and shared representations
remain distinct source facts. Field views still borrow exact source values; no
material data, number, unit reference, factor or prefix is rewritten. Metadata
contexts never replace the body's selected context/units or source shape role.
Only proven links become `property_link_N` roles. Complete closure still rejects
detached properties, representations and contexts. The owning snapshot is withheld
if any link, later closure/unit check or resource budget fails.

`property_attachment_checks` counts actual links attempted in the existing part
phase. Unit/geometry prerequisites still precede it: an invalid typed measure/unit
pair can fail before attachment (counter zero). Successful field/attachment checks
do not evaluate the remaining ID/name/description-attribute WHERE clauses, full
RI/global rules, material meaning, physical conversion or native construction.

The shared single-part, mechanical/design role, complete closure and nominal-unit
restrictions are project guards. Apart from the selected dependencies above,
entity WHERE/UNIQUE and AP214's 272 global RULEs have not been evaluated.
`STEP_PLANAR::Check` rejects AP214 at `UnsupportedRuleProfile/D3`
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

`Snapshot::RegisteredTypes()` returns `std::span<const std::string_view>` over the selected static catalog, in the
original declaration order. `AttributeView::nestedAggregates` is a `std::span<const AggregateDimension>` over static
dimension descriptors; it no longer owns a vector copy per field. C++ callers must rebuild for these API/ABI changes.
Use the spans for read-only iteration and copy explicitly when editable data is required. `TypesOf()` still returns an
owning vector of strings. Static metadata does not extend the lifetime of `AttributeView::value`: keep its source owner
or the owning `FieldView` alive. Profile-local type and field indexes are not portable source entity identifiers.

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
subdirectory and builds both source and semantic contract tests. The root SDK CMake build also
registers the static library and CLI independently of the geometry kernel. This is not a kernel build.

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
| Fields | Existing AP203 simple leaves and six complete complex signatures; AP214 omits the two retired contexts, uses nine signatures and adds the source leaves above; local/inherited slots, optional/required values, transport types, closed SELECT branches, BOOLEAN/LOGICAL, enums, aggregates and protocol-specific derived markers |
| Types | All admitted entity ancestors, including diamonds; explicit constituents and original source identity are retained |
| Part | Unique AC/APD/PC/PDC/product/formation/PD/PDS/SDR/representation/context and consistent reference identities |
| Profile guards | Roles, purposes, category, one shape, explicit outer bound, complete ownership; unknown content is not skipped |
| SI | Context dimension 3, nominal length/plane-angle/optional-solid-angle assignment, the fixed SI prefixes, exact scale to mm/rad/sr |
| Uses | Complete source inverse index, filtered by owner type and declared role; RI use paths do not pass through arbitrary management/unit edges |

AP203 schema identity is the previously pinned MBx N2635 implementation artifact SHA256
`c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088`.
It is not represented as a certified ISO publication. See the
[context](../../tools/step_import_validation/contracts/design/v1/ap203e2-planar-context-closure.md) and
[geometry](../../tools/step_import_validation/contracts/design/v1/ap203e2-planar-geometry-closure.md) frozen audit tables.
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

Not implemented here: AP203's 47 / AP214's 272 global RULEs and the remaining entity WHEREs / named measure predicates,
conversion scale/positivity, remaining unit families, uncertainty evaluation, geometric
consistency/region checks and native construction. The two legacy-context predicates,
context dimension_count positivity and the selected AP214 unit/geometry prerequisites
listed above are explicitly checked where interpreted;
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
