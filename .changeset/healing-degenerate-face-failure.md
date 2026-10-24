---
change: fix
---

Healing returns a failed degenerate-face squeeze or boundary-cleanup status instead
of reporting success and continuing with further edits or property refresh.
It preserves the status delivered by the repair; earlier in-place changes remain.

## Scope

[BRep healing](../.agents/operations/heal_import.md) through the native sequence,
`SmBrep::HealBrep`, and `SmApiHealBrep`.

## Integrator action

Update the matching SMLib runtime and handle the returned failure. Heal a copy when
the original must be preserved. Discard cached properties from failed retained
sessions before using them again. `ReSet` or a fresh `HealBrep` call resets the session.

## Compatibility

Public signatures, layouts, tolerances and parameter conventions are unchanged.
`SqueezeFace` can already have deleted a face before returning `SM_ERR`; failure
does not imply rollback. Other known healing limitations remain.
