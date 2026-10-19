---
change: feature
---

The fixed AP214 source profiles retain draughting predefined colours in fill,
rendering and curve styles. They preserve the original inherited name and
multiple inheritance, validate the exact eight-name palette, and keep missing
or invalid names as failures without a synthesized colour.

## Scope

[STEP source semantics](../source/STEP_SEMANTICS/README.md).

## Integrator action

Rebuild the affected STEP libraries and their statically linked consumers.
Explicit unverified AP214 interpretation still requires its separate opt-in
and result handling; colour support does not authenticate the HEADER identity.

## Compatibility

Two declarations are appended to each AP214 catalog, retaining prior type IDs
and facts. The other catalogs, public C++ ABI, geometry, units, tolerance,
ownership and publication contracts are unchanged. This source subset does not
grant full EXPRESS global-rule or native geometry qualification.
