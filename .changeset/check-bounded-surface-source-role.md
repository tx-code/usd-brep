---
change: fix
---

The fixed AP214 source catalogs retain ordinary `GEOMETRIC_SET`, bounded surface shape representation and spherical surface declarations. A bounded surface representation with no direct surface-bearing set is rejected by its necessary WR7 before native construction. A surface elsewhere in the file does not satisfy this rule.

## Scope

[Source semantics contract](../source/STEP_SEMANTICS/README.md).

## Integrator action

Rebuild STEP_SEMANTICS and its consumers. Handle the new source-rule rejection for inputs previously stopped at unsupported declarations.

## Compatibility

Existing type identities are preserved; two declarations append only to the fixed AP214 catalogs. Ordinary sets do not acquire curve-set or material-solid semantics. Spherical radii retain the positive-length requirement. Passing WR7 does not qualify the other representation rules or enable native surface-role construction. Other protocol catalogs, quality budgets and repair policies are unchanged.
