---
change: fix
---

`SmApiStitch(brep, face)` and Python `stitch_brep_with_face` handle a valid seed
face owned by the target Brep without crashing during default tolerance setup.
Single-Brep stitching derives that tolerance from the target's own geometry and
working tolerance. The two-Brep Boolean calculation remains unchanged.

## Scope

[Stitching operations](../.agents/operations/stitching.md).

## Integrator action

Update the SMLib runtime using the matching SDK configuration. No source migration.

## Compatibility

C++ signatures, native layouts, Python signatures and ownership are unchanged.
The seed face must already belong to the target Brep. STEP admission, precision,
repair permissions and native file formats are unchanged.
