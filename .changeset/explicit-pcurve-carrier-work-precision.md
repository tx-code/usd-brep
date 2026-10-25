---
change: feature
---

Applications can explicitly select a narrower numerical work zone for owned
source-pcurve STEP/revolution carriers. `Quality::GeneralPcurveCarrierPrecisionStudy`
uses the existing general geometric budgets and topology tolerances, with a
separate experimental carrier resolution. The default quality is unchanged.
The CLI accepts its distinct quality identifier.

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

## Scope

[STEP import contract](../source/STEP_IMPORT/README.md) and
[native composed-curve contracts](../tests/curve_copy/README.md).

## Integrator action

Rebuild SMLib, STEP_IMPORT and matching native consumers. Select the quality
configuration explicitly before importing. The study value is not a calibrated
production default or a floating-point error certificate.

## Compatibility

Adds nonvirtual C++ kernel methods and additive importer quality/report/snapshot
fields. The importer C++ layout changes; matching consumers must rebuild.
The native file format is unchanged, using a reserved COPY attribute with integer
binary64 words. The supported SDK tolerance configuration is the default old
tolerance model; alternate tolerance models are not qualified by this change.
Older kernels do not interpret the new attribute's resolution semantics.
