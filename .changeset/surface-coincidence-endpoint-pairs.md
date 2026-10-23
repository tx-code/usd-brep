---
change: fix
---

`SmSurface::SimpleCoincidenceChecker` returns each sampled interval endpoint
with its own surface UV and distance. Partial searches no longer retain the
initial midpoint's distance or the previous hit's UV, which could cause BRep
validation to miss real edge/face coincidence.

## Scope

[Coincident topology contracts](../tests/coincident_topology/README.md).
Upstream reproduction: [issue #28](https://github.com/NVIDIA-Omniverse/usd-brep/issues/28).

## Integrator action

Rebuild the kernel and rerun affected geometric validation. Previously missed
coincidence may now be reported.

## Compatibility

No API/ABI or native-file change. Geometry, parameter domains, tolerances,
sampling order and stopping conditions are unchanged. The result still
describes observed endpoints; it does not certify continuous coincidence
throughout the returned interval.
