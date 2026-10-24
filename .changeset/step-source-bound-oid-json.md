---
change: fix
---

Source-bound AP214 interpretation emits valid JSON when a source OID contains
names, signs or leading zeroes. Its typed source snapshot records `oid: null`,
the original `oid_tokens` strings and `header_identity_verified: false`.
Unverified tokens are never rewritten or emitted as raw JSON numbers.

## Scope

[Source interpretation contract](../source/STEP_SEMANTICS/README.md).

## Integrator action

Read `oid_tokens` for the original source-bound declaration; `oid` is not an
authenticated numeric identifier in this explicit mode.

## Compatibility

Only the source-bound typed snapshot's JSON schema identity fields change.
Existing registered and narrow interpretation report payloads, C++ layouts,
ownership, quality budgets and native archive formats are unchanged.
