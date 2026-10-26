---
change: feature
---

General STEP construction resolves positive conversion-based plane-angle chains
ending at nominal SI RADIAN. Conical semi-angles, trimmed conic parameters and
cylindrical/revolution source pcurves use the declared decimal factors. Labels
such as degree never substitute another conversion constant. Length and angle
normalization share bounded exact arithmetic without mixing their quantities.

The existing explicit `VerifiedExplicitUnitDimensions` policy also admits AP214
conversion plane-angle units with redundant dimensional references after exact
seven-exponent and ownership checks. It retains original marker diagnostics and
reports conditional acceptance; strict source admission remains unchanged.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md) and
[source unit interpretation](../source/STEP_SEMANTICS/README.md).

## Integrator action

Relink matching native STEP consumers. Select the existing unit interpretation
explicitly for nonconforming dimensional slots; conforming markers need no unit
interpretation.

## Compatibility

No public C++ layout, Python API or native archive change. Native angle factors
must fit the existing final `Fraction` capacity after exact chain cancellation;
unestablished scales remain separate from source rejection. Source bytes,
parameter conventions, physical error budgets, repair and publication contracts
are unchanged. This does not establish full STEP or downstream modeling support.
