# Native STEP import design

This document carries the durable design constraints for the independent STEP
importer. Implemented profiles and qualification limits belong to
[STEP_IMPORT](../../source/STEP_IMPORT/README.md) and the owning layer's README.
Design requirements do not grant unimplemented capabilities or authorize a new
implementation phase.

## Purpose and architecture

The target is the supported exact BRep subset of AP203, AP214, and AP242, with
part definitions, basic occurrences/transforms, open surfaces, material solids,
and cavities preserved. Product reading/conversion is independent of OCCT and
Truck/ruststep; their sources and independent test comparisons may inform it.
Public reproductions use synthetic inputs; private model libraries stay local.

| Layer | Responsibility |
|---|---|
| STEP_SOURCE | Immutable captured bytes, Part21 records/references, exact source text and locations |
| STEP_SEMANTICS | Explicit schema/profile selection, typed fields, dependency closure, product/unit interpretation |
| STEP_PLANAR | The qualified planar source-rule, exact-arithmetic, and region checks |
| STEP_IMPORT | Source-to-native mapping, private construction, postchecks, owning result/report publication |

Preserve source IDs and directed uses across layers. Shared typed field bindings
and reference queries retain the source snapshot owner; `$`, `*`, empty values,
nominal types, and coincident-but-distinct entities keep their own meanings.
Schema recognition is distinct from checked source scope and geometric acceptance.
An application may explicitly select a fixed developer interpretation for a
specifically supported unregistered identifier. This introduces a recorded source
assumption, not an authenticated binding or proven nongeometric exception. Such
complete models have a separate result qualification and owning assumption
evidence; ordinary admission and unsupported required-content guards remain
unchanged. They do not silently enter ordinary source-qualified acceptance counts.

A carrier declaration may separately require an explicit, unverified reading
assumption. It is independent of schema identity: neither assumption verifies
the other. Complete native results retain the applied evidence in their owning
source and use the interpreted qualification whenever either assumption applies.
Failed parsing may retain captured bytes and a carrier receipt without publishing
a partial source graph. Requested but unused interpretations grant no additional
qualification. Native defaults, supported grammar and complete-result publication
remain unchanged; current options and limits belong to the component contracts.

## Completeness and geometry

- Select an explicit root and account for its complete required dependencies.
  An unknown influence or unsupported required entity blocks the selected request;
  partial models, skipped faces, or choosing the largest surviving solid do not
  satisfy it. Syntax that leaves record boundaries or dependency influence
  ambiguous, and ambiguous entity IDs, remain global failures.
- Preserve defining curves/surfaces, knots, weights, source parameter domains,
  placements, and every orientation layer. Normalize physical coordinates to
  canonical mm/rad through verified unit chains; dimensionless ratios and source
  parameters follow their own contracts. Record invertible parameter mappings.
- Keep 3D curves, source pcurves/master semantics, and derived target UV distinct.
  Equivalent reparameterization or an exact target seam needs a recorded
  source-to-native correspondence. Derived approximations need their own error
  budget, coverage, region/orientation, and branch checks.
- Preserve shared-edge identity, periodic branches, degenerate edges, poles,
  holes, shell roles, and cavities. Surface closure or native manifold checks do
  not establish a material interior. Sampling is not a continuous error bound.
- Keep part definitions separate from occurrences. Resolve transform direction
  and composition, apply unit scaling once, and preserve repeated and nested
  instances, mirrors, and supported scales. Assembly import is not a Boolean union.

## Error influence and isolation

Long-term robustness depends on the influence of an error, not its apparent size.
Keep the original error fact (stage, stable code, byte span, entity/field and
source authority) separate from the admission decision and its evidence.
Preserving an error never turns its source declaration into a valid one.

| Error scope | Admission requirement |
|---|---|
| Carrier, uncertain quote/comment boundary, ambiguous or duplicate ID | Reject the selected request when a trustworthy complete index cannot be established |
| Required geometry, topology, unit, transform or product dependency | Reject the affected selected request; no guessed values or missing faces |
| Known administrative/presentation metadata | Explicit interoperability may isolate it only with a field/record-specific nonphysical ownership proof |
| Unselected independent root | Explicit scoped admission requires proof that shared and inverse dependencies do not affect selected roots; the current pilot is limited to the pinned AP214 FACE.WR2 clause |
| Unknown influence, exhausted proof budget | Remain unsupported/indeterminate/resource-limited; never infer independence from missing evidence |

Forward reachability alone is insufficient. Inverse assignments, representation
relationships, shared units and contexts, and profile/global obligations can
affect a selected root without a direct outgoing reference from that root.
Isolation requires both a trustworthy reference footprint and a known semantic
role. An unknown or malformed record must not be declared harmless merely because
the current parser found no references in it.

Local syntax recovery is a future capability, not current reader qualification.
It must use lexically established boundaries outside strings/comments, preserve
the exact failed record span and any unestablished reference footprint, and bound
attempts/bytes/diagnostics with monotonic progress. Searching for a semicolon or
`#id=` inside uncertain text is not a recovery proof. Never renumber duplicates,
guess missing delimiters/values, or silently repair physical definitions.

The implemented `InspectIndependentDefinitions` diagnostic mode now retains
separate definition-local part/region and unit-context observations after an
earlier definition fails. It first requires complete raw/type/global dependency
prerequisites and the existing independent-definition planner. Its owning facts
include unapproved metadata diagnostics; it never publishes qualified parts or
a document, even for all-green observations. Whole-document ownership and final
interoperability approval remain unrun. This provides facts for later influence
proofs; it does not itself establish error isolation or selected-root admission.

The implemented native-only selection `SourceCompleteNativeSelection-0.1`
first requires a completely checked independent-product source document. It
binds explicit PRODUCT_DEFINITION IDs to original SHA256 and constructs only
those definitions, atomically in source order. Its owning receipt distinguishes
the complete source inventory from selected and natively NotRequested roots.
It does not isolate source errors or prove a malformed/unknown record harmless;
the root-exclusion admission requirement above remains for that future work.

Recovered/opaque records must have an explicit state and cannot masquerade as
ordinary parsed records in the existing complete `SourceSnapshot` contract.
Required typed consumers must reject unresolved dependencies. Native publication
still commits all selected roots together, including required diagnostics and
isolation evidence. A caller may explicitly select independent roots; a failed
whole-assembly request must not automatically turn into a successful partial one.
Source preservation/isolation, equivalent target normalization and geometric
repair remain distinct operations with distinct qualifications.

The separate `SourceFaceClauseSelection-0.1` path now admits explicitly selected
AP214 definitions after retaining unselected `FACE.WR2` failures and proving their
complete selected influence/incoming boundary. All other existing source checks,
whole-source ownership and metadata approval run with every candidate present;
only afterwards are unselected candidate views removed. The selected owning source
keeps the full original record graph, declaration inventory, failed clauses and
request/proof identity. Native construction still commits all selected definitions
together and reports `AcceptedWithSourceIsolation` when failures were isolated.
This is a complete-parse/typed-source pilot, not arbitrary local-rule isolation,
syntax recovery or automatic surviving-root selection. The ordinary native-only
selection above retains its all-source-checked contract.

Implement this incrementally: first unify error facts and ownership proofs for
existing metadata policies; then add bounded diagnostic-only record recovery;
only after boundary/reference and inverse-dependency controls pass may an explicit
recovery mode feed selected-root import. Controls must include malformed geometry,
metadata-only faults, shared-unit faults, inverse links, duplicate IDs, unterminated
text, budget/cancellation and mixed-root atomic publication. Current supported
options remain documented in the component READMEs; this design does not enable
general syntax recovery or multi-root selection by itself.

## Quality and bounded repair

An explicit versioned configuration distinguishes definition error, consistency,
derived UV, repair displacement, working tolerance, feature protection, query
qualification, and resource limits. Source uncertainty is retained as source
information and may produce a warning; it does not select or enlarge these budgets.
Fixture/development values are not production defaults.

Track initial, intermediate, propagated, and final effective native tolerances.
Keep search/convergence thresholds separate from permission to change geometry.
Working tolerance must cover verified residuals while remaining within the
configured limits; resetting its baseline or truncating its stored value cannot
make an unqualified candidate acceptable.

Strict import uses `Repair=None`. A separately requested repair operates on a
candidate/copy and enables only independently validated actions: unique
connection/orientation corrections, bounded endpoint/trim corrections on existing
geometry, proven duplicate merges, matching existing boundaries, tolerance
coordination, or derived-parameter corrections respecting the source master.
These actions need local/cumulative displacement and tolerance budgets and
postchecks. The original gap, maximum displacement, and final effective tolerance
must each satisfy its explicit limit and 1% of a reliable local protection scale.
Intermediate peaks and growth have separate budgets. The protection scale must
reflect nearby thin walls, holes/slots, and nonparticipating boundaries; overall
model size or the edited edge's length alone is insufficient. An unknown
protection scale blocks a repair that requires it.

Automatic deletion of small edges/faces/holes/thin walls, filling missing faces,
fitting defining geometry, resolving contradictory source definitions by guess,
or enabling the entire healer are outside this whitelist. Clean inputs must not
drift; repeated repair and intermediate peaks need explicit verification.

## Result, ownership, and publication

Publish the complete selected model and required report only after the applicable
checks and one final execution decision succeed. Prepare all owners and required
report allocations before success; after commitment, transfer ownership without
callbacks, allocation, or native operations. Failures release unpublished models.
Source rejection, unsupported scope, insufficient numerical evidence, resource
limits, cancellation, and native/internal failures retain their distinct causes.

The accepted snapshot owns its native roots/context and source correspondence.
Read-only views retain their owner; reports can survive independently without
keeping the entire native model alive. Modeling uses an independently owned copy,
whose edits do not inherit the original snapshot's acceptance evidence. Neither
owning views nor separate contexts imply arbitrary kernel thread safety.

Reports identify the input hash, selected scope, request/configuration, source and
binary/checker versions, and distinguish diagnostics, representation normalization,
and repair. Native pointers are not persistent IDs; unknown measurements are not
reported as zero. Each downstream query/section/modeling qualification is scoped.

## Execution and verification

The synchronous core cooperatively checks cancellation and limits at safe
boundaries. An uninterruptible native call returns safely before candidate
disposal. Cancellation and success have one terminal decision; a late cancel
does not destroy an already delivered model.

Applications requiring hard cancellation, hard resource limits, or crash
isolation need a separately verified worker/owner protocol. A worker proxy is
not a local `SmBrep*`. Owner acquisition precedes success, job teardown must not
destroy an accepted result, and application insertion separately checks the
expected document revision/request generation. Unsupported isolation or bridge
requirements are rejected explicitly.

Qualify each geometry/operation family with synthetic independent truth,
counterexamples, budgets, lifecycle/failure controls, and source correspondence.
Use native controls to distinguish importer defects from kernel limitations.
Independent readers are comparisons, not sole correctness authorities.
Real-model promotion uses deduplicated fixed, held-out blind, and large-resource
cohorts (the accepted initial targets are 60/20/8), with captured input/configuration
and execution identities. Freeze production defaults only after scoped calibration.

The original detailed design is retained byte for byte as a
[versioned fixture input](../../tools/step_import_validation/contracts/design/v1/step-import-repair-spec.md).
Session decisions and per-run evidence stay local; reusable validation lives in
the component tests and `tools/step_*_validation`.
