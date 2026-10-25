---
change: feature
---

General STEP construction can preserve a source-master B-spline pcurve through
an exact positive binary UV chart scaling when the original native UV curve
fails only the small-radius check. Complete stored surface XYZ/W and pcurve t
knots remain unchanged; surface knots and UV coefficients must round-trip
exactly under the same coordinate map. Physical budgets and source priority
remain in force. A failed native postcondition never publishes a model.

The original source snapshot retains its bytes and parameter definitions.
Existing parameter mappings record carrier and target-face correspondence;
`ModelSnapshot::SplineCharts()` owns those records independently of the caller's
report. Target-face trims are projected from the physical edge against the
actual target surface. Final ownership and complete stored definitions are
verified after native consumers and before publication.

The surface resource cap applies to the total stored control count, so a long,
narrow control net can qualify within the same maximum storage/work scope.

## Scope

[STEP import contract](../source/STEP_IMPORT/README.md). The bounded plain,
clamped spline scope and incompatible optional paths are documented there.

## Integrator action

Rebuild STEP_IMPORT and matching native consumers.

## Compatibility

Adds a C++ snapshot query and two parameter-mapping kinds without changing the
report schema. Existing general development imports may now accept an equivalent
native representation with unchanged error budgets. No repair or source-domain
interpretation is added; native archives do not inherit STEP qualification.
