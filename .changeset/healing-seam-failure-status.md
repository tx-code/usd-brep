---
change: fix
---

`SmBrep::HealBrep` and `SmApiHealBrep` report failed periodic seam movement/insertion
and failed recursive property refresh instead of returning success. Direct `SmFace::SplitAtSeam`
preserves U/V insertion errors. Python `heal_brep` exposes these failures through its existing
`RuntimeError` status check.

## Scope

[BRep healing](../.agents/operations/heal_import.md).

## Integrator action

Handle failed healing calls. Healing modifies its input in place and does not roll back earlier
edits on error; use a copy when the original must be retained.

## Compatibility

C++ signatures, ABI and Python return ownership are unchanged. Previously hidden failures can
now produce a non-success status or Python exception. Success still does not establish complete
repair or geometric validity. Native STEP import retains `Repair=None`.
