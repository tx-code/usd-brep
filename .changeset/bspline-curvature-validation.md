---
change: fix
---

`SmBSplineCurve::AssertValid` consumes the minimum-radius search results before
applying its existing threshold. Curves below that threshold produce diagnostic
13 (2D) or 14 (3D) instead of silently passing. Selected-test calls only apply
this radius check when the corresponding diagnostic is requested.

## Scope

Maintenance

## Integrator action

Review newly reported small-radius quality findings in callers that require
`AssertValid` to pass, including native STEP publication.

## Compatibility

C++ signatures and object layout are unchanged. Validation acceptance can change
for curves with an extremely small radius. The historical threshold remains
parameter dependent and uses the curve's own coordinates; it does not establish
a physical UV tolerance, a parameter-independent quality policy, or full STEP
import correctness.
