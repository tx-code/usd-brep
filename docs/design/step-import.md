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

## Completeness and geometry

- Select an explicit root and account for its complete required dependencies.
  An unknown influence or unsupported required entity blocks the selected request;
  partial models, skipped faces, or choosing the largest surviving solid do not
  satisfy it. Global syntax and entity-ID ambiguity remain global failures.
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
