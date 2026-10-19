---
change: fix
---

Native `SmBrepData` part reads preserve each saved positive BRep work tolerance.
They no longer apply external-data coordinate-size clamping that collapses an
origin point's tolerance to zero or enlarges a distant point's tolerance.

## Scope

Maintenance: native part archive reconstruction through `MakeTopologyFromData`.

## Integrator action

Rebuild the kernel and use matching binaries. External translator calls retain
their existing explicit/default tolerance-policy choices.

## Compatibility

No API or ABI change. Native reloads retain recorded tolerances instead of silently
rescaling them. Serialized geometry, recognition and healing options are unchanged.
