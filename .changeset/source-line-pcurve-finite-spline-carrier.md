---
change: feature
---

General STEP development import constructs source-priority and auxiliary two-dimensional
LINE pcurves on finite clamped positive-weight B-spline carriers. It retains source
decimal generators and vector parameter conventions, uses the exact full carrier-domain
intersection, and records the finite native parameter map. Whole-window UV conversion
error is bounded physically on the stored carrier under the existing definition budget.
The final owned carrier and UV net must still match their evidence before publication.

Decimal source boundaries are related to stored knot coordinates by exact whole-knot
affine correspondence or, for an exactly fixed clamped boundary, its original boundary
restriction. The latter preserves the source use without claiming an affine map of
the entire axis. It prevents a decimal boundary from being falsely rejected against
an independently rounded native domain; no snapping or physical allowance is added.

When a fixed clamped boundary's varying knots require numerical conversion,
import can certify its original exact curve against the actual stored homogeneous
boundary over the whole interval. This separate certificate preserves source
coordinates, weights, units, knot groups and endpoint parameter correspondence.
UV endpoint conversion must be exact, so the original definition allowance is
spent once. Pending plans, failed certificates and cancellation never publish
a partial model; this does not assert whole-surface source accuracy.

## Scope

[Finite-carrier LINE pcurves](../source/STEP_IMPORT/README.md#有限-b-样条载体上的-line-参数曲线).
This does not grant complete AP conformance, source-surface conversion proof, infinite
analytic-carrier support, or qualification for repair/domain-interpretation combinations.

## Integrator action

Rebuild matching native import consumers. Use `ModelSnapshot::PcurveLineWindows()`
for owning parameter correspondence after releasing the caller's report.

## Compatibility

Adds a C++ snapshot accessor and LINE-window parameter mappings. Existing B-spline
pcurve parameter identity and source-priority policies retain their contracts. The
selected quality, physical allowances, cancellation and complete-publication gates
remain unchanged; required reports include the new correspondence.
