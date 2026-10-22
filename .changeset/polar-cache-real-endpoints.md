---
change: fix
---

SMLib polar conversion preserves real angular endpoints near a periodic seam.
Building a conversion table uses geometric inversion without point-classification
seam folding and recognizes equivalent endpoint representatives modulo one turn.
This prevents valid analytic surfaces from acquiring incompatible NURBS parameter
caches during lazy construction or copying.

## Scope

[Polar conversion endpoint contracts](../tests/polar_conversion/README.md).

## Integrator action

Rebuild and distribute the matching SMLib runtime with native STEP consumers.

## Compatibility

No API or ABI change. Defining NURBS knots, control points and weights, physical
geometry, native archives and geometric tolerance settings retain their contracts.
