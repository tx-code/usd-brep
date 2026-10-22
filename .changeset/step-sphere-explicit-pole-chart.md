---
change: fix
---

General spherical STEP construction can select its target longitude cut from
the nonpolar endpoint of a repeated circular seam when the other endpoint is
a source pole. The original frame, radius, source vertices and edge senses are
preserved; final UV and native validation remain mandatory.

## Scope

[Native STEP import](../source/STEP_IMPORT/README.md). Public hemispheres with
explicit pole/seam topology pass independent area, volume, bounds and archive
checks. Implicit pole construction and two-pole cut inference remain unqualified.

## Integrator action

Rebuild STEP consumers. No new option is required for the general path.

## Compatibility

No kernel ABI, archive format or budget change. Existing SphericalLongitudeChart
evidence identifies the target chart; no source angular coordinate is invented
for a pole and no source geometry is repaired.
