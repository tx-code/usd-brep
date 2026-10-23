---
change: feature
---

Integrate the native kernel, API and Python changes from upstream Release 1.0.1,
including seam/pole classification and healing tracking, primitive and
tessellation fixes, and geometry-query improvements. Retain the fork's native
SDK build and STEP source, quality, ownership and publication contracts.

## Scope

[Native SDK maintenance](../FORK_MAINTENANCE.md) and
[geometry queries](../.agents/operations/queries.md).

## Integrator action

Rebuild the kernel, SM_API, native STEP consumers and Python/developer extensions
together: upstream changes native layouts and interfaces. Keep delivered
runtimes separate from a newly built SDK.

## Compatibility

Property-query relative accuracy is forwarded to the kernel instead of silently
clamping it to the previous wrapper envelope. Callers should choose a positive
accuracy explicitly and handle numerical failures; values tighter than 1e-8
are not recommended. STEP quality settings and acceptance budgets are unchanged.

The fork retains coefficient-certified curve-cache shortcuts, safe owning
classification copies, centered normalized-span integration and failure cleanup.
Upstream's zero-step integration guard is applied to that integration path.
Inner integrals can refine their initial step without changing the requested
accuracy or component scales; each trial initializes its state and propagates
final failure.
Low-level face-property queries require a finite positive area estimate.
Split B-spline spans are evaluated directly in local parameter coordinates,
avoiding loss of quadrature nodes in large-offset, narrow native intervals.
Curve copies preserve the defining knot vector instead of silently tuning
positive knot spans ([upstream report](https://github.com/NVIDIA-Omniverse/usd-brep/issues/26)).
These changes preserve source definitions and UV parameters.
Component-scaled stopping controls can choose different adaptive nodes for
different property selections or reference frames; numerical results are
compared at their requested controls rather than assumed bit-identical.
The retired USD/schema/converter pipelines are not part of this integration.
