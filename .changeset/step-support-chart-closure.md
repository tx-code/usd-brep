---
change: fix
---

Optional STEP support-domain restriction refuses an open UV chart on an ordinary
B-spline support before copying or installing a cropped surface. A boundary
control box alone no longer establishes coverage of the selected trim region.

## Scope

[STEP import support-domain normalization](../source/STEP_IMPORT/README.md).

## Integrator action

Rebuild STEP import consumers. Recheck requests using
`restrictUnusedSupportDomains`; charts without established closure are refused.

## Compatibility

Public C++ layouts and native archives are unchanged. Source geometry, parameters,
roles, physical error budgets and repair permissions are unchanged. The optional
normalization performs additional charged, cancellable read-only checks;
unestablished coverage does not publish a candidate.
