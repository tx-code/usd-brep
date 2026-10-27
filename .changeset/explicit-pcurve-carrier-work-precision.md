---
change: feature
---

Applications can explicitly select a narrower numerical work zone for owned
source-pcurve STEP/revolution carriers. `Quality::GeneralPcurveCarrierPrecisionStudy`
uses the existing general geometric budgets and topology tolerances, with a
separate experimental carrier resolution. The default quality is unchanged.
The CLI accepts its distinct quality identifier.

Ordinary revolution NURBS construction retains nonzero small circles instead of
collapsing their control rows according to absolute world coordinates. Rows below
the circle factory's radius floor use an affine unit-circle net without changing
the generator, weights or angular/STEP parameter correspondence. True zero-radius
rows remain degenerate. Previously accepted, silently collapsed representations
can now report the original near-pole quality finding; work precision, acceptance
budgets and native validation requirements are unchanged.

`SmCrvOnSurf` exposes status-returning setters/getters for this local resolution.
Only a genuinely owned STEP wrapper and revolution support qualify. No global
precision, source definition, UV, axis or source-priority declaration is changed.
The effective zone cannot exceed the system default. Invalid, duplicated or
shared policy attributes fail native validation and virtual Copy.

The precision is copied independently and persists exactly through binary and
text native archives. Positive uniform scaling scales the stored length;
unsupported scales and invalid rigid frames refuse before geometry/cache edits.
The kernel's existing nontransactional failure contract still applies if a
support transformation or application notification fails after editing starts.
Reports retain constructed-carrier observations; snapshots retain only actual
defining-edge observations. Native archives do not inherit STEP qualification.
Continuous pole-visit proofs reuse endpoint enclosures and tighten single-span
rational curves with restricted positive Bezier hulls. All added work is charged;
the cumulative quota, geometric budgets, original derivative bounds and refusal
conditions remain in force.
Stored-curve proofs interpret the native 2D no-Z marker as an absent coordinate,
so independent copies and native archive readback retain the same continuous
UV/revolution evidence. Stored UV coefficients and parameter conventions remain
unchanged.
Unqualified combinations with periodic topology normalization, support-domain
restriction, repairs or definition selection/isolation refuse before construction.

Applications can independently bind a target revolution face's calculation
resolution to the original SHA256 and exact definition/shape/face/surface IDs.
`Options::targetSurfacePrecision` requires an explicit positive int64 model-mm
fraction, face budget and complete plan; the CLI transports those four fields
without inference or automatic retries. Source definitions and UV, topology
tolerances and physical acceptance budgets remain unchanged. Every native check,
including `SurfaceTest5`, remains required. This option combines with explicit
terminal-domain/source-3D choices, but refuses the same unqualified topology,
repair, support-domain and selection combinations.
Only the singularity classifier's numerical degeneracy threshold opts in to the
target-surface resolution. `SmTol` coincidence, intersection and approximation
zones stay unchanged, including those used for seam and endpoint agreement.

The request-root JSON and owning snapshot receipt retain all plans and actual
requested/effective resolutions, topology face tolerances and native-check
completion. Nested reports reference the one request root. Only a complete
request that passes all ordinary import/publication gates returns
`AcceptedUnderInterpretation` with `Repair=None`. The default remains disabled
and adds no report field. A requested calculation resolution does not certify
surface regularity, source-schema correctness or a continuous error bound.

Registered target-surface precision is validated on every surface class, so
copying its metadata onto an unsupported representation cannot pass native
validation. Conversion of a registered revolution to an ordinary NURBS surface
is currently unsupported and refuses before notification or geometry edits.
Subdivision and nonuniform BRep transforms propagate a failed NURBS conversion
instead of continuing to mutate the model after that error.
Owned seam copies retain the same surface resolution only after reciprocal
ownership/context checks. Missing copied classification geometry now returns a
failure instead of being dereferenced. Legacy inline COS archives explicitly
refuse nested target-surface precision they cannot retain; native curve and part
writers propagate that refusal instead of returning success with lost metadata.
Complete target-face archives use the existing surface attribute channel.
The temporary ordinary B-spline adapter used for same-basis projection removes
only its verified independent copy of the SOR-only precision attribute. The
actual target retains its policy, ownership, coefficients and parameter domains.
The adapter receives no target precision; projection accuracy and physical
acceptance budgets remain unchanged.

## Scope

[STEP import contract](../source/STEP_IMPORT/README.md) and
[native composed-curve contracts](../tests/curve_copy/README.md).

## Integrator action

Rebuild SMLib, STEP_IMPORT and matching native consumers. Select the quality
configuration explicitly before importing. Target-face work resolution requires
the separate original-bound request, not just the carrier quality configuration.
The study value is not a calibrated production default or a floating-point error
certificate.

## Compatibility

Adds nonvirtual C++ kernel methods and additive importer quality/report/snapshot
fields. The importer C++ layout changes; matching consumers must rebuild.
The native file format is unchanged, using a reserved COPY attribute with integer
binary64 words. The supported SDK tolerance configuration is the default old
tolerance model; alternate tolerance models are not qualified by this change.
Older kernels do not interpret the new attribute's resolution semantics.
