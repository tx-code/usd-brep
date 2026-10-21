---
change: feature
---

Explicit general STEP interoperability preserves unavailable text in attached
`PERSON.last_name`, `PERSON.first_name`, `ORGANIZATION.id` and `ORGANIZATION.name` fields, retaining source bytes and the
original diagnostic instead of inventing decoded values.

## Scope

[Source semantic contract](../source/STEP_SEMANTICS/README.md).

## Integrator action

Select both opaque DATA preservation and nongeometric interoperability explicitly.

## Compatibility

Strict behavior is unchanged. Approval still requires selected administrative
ownership and physical exclusion; other fields receive no new exception.
No public ABI, geometry tolerance or repair policy changes.
