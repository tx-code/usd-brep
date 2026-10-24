---
change: fix
---

Native `SmCrvOnSurf::Copy` propagates owned-child copy failures, releases unpublished candidates,
and leaves source ownership intact. `SmSTEPSurface::Copy` creates an independent wrapper and
underlying surface while preserving the exact definition and parameter convention.

## Scope

[Native geometry ownership and copy contracts](../.agents/skills/smlib-kernel-operations/references/lifetime-context-cache.md#native-composed-curve-copies).

## Integrator action

Rebuild matching native SDK libraries and consumers. Check the returned copy status before using
the output. Borrowed children of a copied `SmCrvOnSurf` must still outlive that curve.

## Compatibility

Existing public constructor signatures, object layout, and inherited virtual slots are unchanged.
`SmSTEPSurface` now overrides the existing virtual copy operation. Failed virtual copies return
no output; derived curve caches may be rebuilt lazily. Direct C++ copy constructors, `SetAll`,
and UV-endpoints constructor behavior are outside this fix. Native archive and parameter formats
are unchanged; this does not establish STEP pcurve import acceptance.
