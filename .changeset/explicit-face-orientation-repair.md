---
change: feature
---

Native STEP import accepts an explicit, source-bound face-orientation repair
plan and returns `AcceptedWithRepairs` with owning original-source and repair
evidence. The standalone viewer can transport the same single-file plan, show
repair progress and label repaired results separately from ordinary acceptance.

## Scope

[Explicit native repair](../source/STEP_IMPORT/README.md) and
[standalone viewer](../tools/smlib_viewer/README.md).

## Integrator action

Rebuild native consumers with matching C++20 headers/static libraries and deliver
the updated worker/viewer modules. Handle `AcceptedWithRepairs` (native CLI 12)
explicitly and retain original source, source assumptions and repair history.

## Compatibility

Internal C++ Options/Report/ModelData layouts, execution phases and result enum
are extended. `ModelSnapshot::Repairs()` and `FaceView::OrientationRepair()` own
their evidence after outcome disposal. Default `Repair=None`, original bytes,
strict refusals, existing quality budgets and kernel ABI remain unchanged.
The bounded pilot preserves defining geometry/tolerances, qualifies only its
documented spline/curve subset and never automatically heals or drops content.
Native archives/modeling copies do not inherit STEP source or repair qualification.
