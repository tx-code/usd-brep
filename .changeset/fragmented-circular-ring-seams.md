---
change: fix
---

Native STEP general construction supports cylindrical bands whose circular
boundary rings contain multiple direct CIRCLE edges. It retains the original
vertices and curve definitions, shares necessary arc subdivisions across adjacent
faces, and records the derived meridian and complete source-use correspondence.
Geometric quality, cancellation and complete-publication checks still apply.

A closed circular ring can also bound a cylindrical face whose opposite boundary
contains lines and B-splines. A bounded private survey identifies one meridian
connection to an original vertex or circular-arc interior before any real face is
constructed. It uses retained pre-snap encounter parameters, preserves complete
definitions, and shares all necessary arc children across their original uses.
Ambiguous classifications and unsupported intersection families remain refused.

## Scope

[Circular ring construction](../source/STEP_IMPORT/README.md#完整圆环的目标分段与圆柱圆锥图切口).

## Integrator action

Rebuild the native STEP library and its consumers together.

## Compatibility

No public C++ API, kernel ABI or native-file format change. This expands the
existing general-development construction profile without introducing repair,
changing source representations or increasing physical acceptance budgets.
