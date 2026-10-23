---
change: fix
---

`SmTorus::AssertValid` checks the spindle exclusion interval against the generating
circle's analytic V domain. A change to the major U sweep no longer rejects a
regular profile or hides a profile in the excluded band. V extent validation also
uses V, and the U/V extent reports use their declared test identities 3 and 4.

## Scope

[Native torus validation](../tests/torus_validation/README.md).

## Integrator action

Rebuild SMLib and distribute the matching runtime to native consumers. Code that
interprets torus extent diagnostics should use the declared U/V identities.

## Compatibility

No C++ layout, ownership, native-file format, geometry, parameter, or tolerance
change. The existing spindle exclusion policy and degree units remain. Validation
can change for previously misclassified domains, and incorrect extent diagnostic
identities are corrected. This fix does not expand STEP import scope or change
the release/debug report-filtering policy.
