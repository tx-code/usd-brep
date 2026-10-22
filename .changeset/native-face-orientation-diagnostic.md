---
change: fix
---

Native STEP refusal reports distinguish a known face-loop orientation predicate
from the legacy `GeneralPointerValidation` entry key. The standalone viewer shows
a precise orientation-check failure caption while retaining the original
diagnostic and previous accepted scene.

## Scope

[Native STEP diagnostics](../source/STEP_IMPORT/README.md) and
[standalone viewer](../tools/smlib_viewer/README.md).

## Integrator action

Rebuild the native importer/display worker and deliver the matching viewer
Python modules. Callers can inspect the owning diagnostic classification without
parsing native message text.

## Compatibility

The known general-path failure gains an additive diagnostic `classification`
field. Status, original diagnostic key/message, strict `Repair=None`, checks,
budgets, failure cleanup and successful report bytes remain unchanged. Mixed or
unknown native findings retain their existing classification. No public C++
layout, kernel ABI, geometry, native-file or acceptance change is introduced.
The additive failure field counts against the existing mandatory report capacity;
insufficient report capacity still prevents publication.
