# Native conic tight bounds

The `conic_bounds` CTest target exercises the real `SmCurve::CalculateTightBoundingBox`
entry and independent analytic/actual-evaluator controls. Build with the root SDK
validation option and matching kernel layout/CRT; see the [build guide](../../README.md).

```powershell
cmake --build <build> --config Release --target conic_bounds
ctest --test-dir <build> -C Release --output-on-failure -R '^conic_bounds$'
```

Repeat the focused target in Debug. A separate `/GR-` consumer build checks that
runtime type gating uses the kernel's virtual `GetType` registration and requires
no RTTI. Other architectures/arithmetic modes retain the existing solver; tests
require coefficient-path support only when the supported environment is present.

## Contract and evidence

The internal module reads the actual NLib homogeneous coefficients and knot spans
of concrete `SmCircle`/`SmEllipse` objects. Analytic radius/angle metadata is not an
alternative geometry definition. Native parameters are mapped outward to each
Bezier span; they are never treated as degrees or radians. Supported data is a
bounded chain of at most 16 already separated quadratic spans with positive
weights (or NLib's `NL_NOW` polynomial sentinel). Different bases and inconclusive
arithmetic use the existing solver without replacing geometry or parameters.

For a coordinate `C=N/W`, half the derivative numerator has quadratic Bernstein
coefficients `X1*w0-X0*w1`, `(X2*w0-X0*w2)/2`, `X2*w1-X1*w2`. Analytic roots propose
candidate windows. Positive-weight convex hulls bound those windows, and outward
Bernstein sign checks prove monotonicity over every remaining interval. Missing
or poorly conditioned candidates therefore cause fallback, not a missed extremum.
The enclosure-width refusal threshold does not pad bounds or enlarge a working
or acceptance tolerance. Native endpoint failures propagate before any box is
assigned. No cache or retained borrowed geometry is introduced.

Controls cover clipped and full circles/ellipses, knots and periodic branches,
rotated frames, native parameter edits, reversal, short arcs, thin/small conics,
large coordinates, metadata changes, correctly registered forwarding subclasses,
native failure propagation, unsupported knots/degree/weights, and FTZ/rounding
fallback. Dense evaluations are regression observations, not the continuous proof.
The old implementation fails the clipped-arc control by adding points at knots
outside the requested interval.

This proof concerns the selected native curve's coordinate bounds. It grants no
source conformance, import quality configuration, surface-interior bound or global
geometric error qualification. STEP integration additionally keeps the independent
bulged-surface control, full reports and cooperative cancellation/resource tests.
