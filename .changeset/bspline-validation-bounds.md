---
change: fix
---

`SmBSplineCurve::AssertValid` avoids expensive global speed and curvature searches
when conservative interval bounds on eligible Bezier spans prove the existing
thresholds. Curves or arithmetic environments without such a proof retain the
global searches. Knot, continuity and internal-pole checks still execute.

## Scope

Maintenance

## Integrator action

None.

## Compatibility

C++ signatures, object layout, curve representations and validation thresholds
are unchanged. The early pass is limited to supported concrete B-spline data and
floating-point environments; it does not establish full import correctness or
relax STEP source checks, quality configuration, or repair policy.
