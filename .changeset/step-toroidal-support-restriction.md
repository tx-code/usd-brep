---
change: feature
---

Explicit general STEP support-domain restriction can import regular trimmed
regions of horn/spindle toroidal supports. Original-basis NURBS extraction
preserves native UV, source definitions, shared boundaries and orientation,
with a continuous surface-difference bound and a generator radius bound away
from the axis. Toroidal derived projection can use an identical ordinary basis
while checking its paired curve parameter against the actual surface.

## Scope

[Toroidal support restriction](../source/STEP_IMPORT/README.md#角环面自交环面支撑的原基提取).

## Integrator action

Rebuild internal STEP libraries and consumers together. Select the existing
support-domain restriction option explicitly. Retain the owning source snapshot
and normalization evidence when using a native modeling copy or archive.

## Compatibility

Default import scope, source rules, repair permissions and quality budgets remain.
Internal C++ snapshot/report layouts gain owning surface-normalization evidence.
The normalized target can be ordinary NURBS while its source analytic definition
remains retained by the import snapshot. Native archives/modeling copies encode
the target and do not inherit STEP qualification or original-definition metadata.
Kernel ABI and native archive format are unchanged. Singular or unproved regions
remain refused; no full-schema or continuous derived-UV qualification is added.
