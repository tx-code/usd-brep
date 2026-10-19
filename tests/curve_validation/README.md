# B-spline validation regression

This C++20 test exercises `SmBSplineCurve::AssertValid`, using synthetic curves.
It checks both ordinary LEVEL2
calls and selected radius diagnostics (13 in 2D, 14 in 3D), and verifies that an
unrelated selected test does not acquire a radius failure.

```text
cmake -S tests/curve_validation -B build/curve-validation -DSMLIB_LIBRARY_DIR=<library-directory> -DSMLIB_RUNTIME_DIR=<runtime-directory>
cmake --build build/curve-validation --config Release
ctest --test-dir build/curve-validation -C Release --output-on-failure
```

Use matching SDK configuration, CRT and layout macros. The standalone Windows
configuration targets Release `/MD`; the runtime directory contains the matching
SMLib DLL. The SDK build with `SMLIB_BUILD_VALIDATION=ON` also registers
`curve_validation`, using the SDK's own configuration and layout macros.

For a quarter ellipse with major radius 1 and minor radius .001, the endpoint
radius of curvature is `b*b/a = 1e-6`, with a nonzero tangent. The old validator
computed radius candidates but compared its unchanged sentinel to the threshold,
so it accepted this curve without a radius diagnostic.

The tests preserve the existing threshold `boundingBoxDiagonal / parameterLength
/ 10000`. That expression has speed units and is also historically used for the
radius comparison. It is parameter dependent; in UV it is not a physical length
criterion. These tests lock down the existing validator quality policy and its
diagnostics, not a universal definition of valid geometry or import correctness.

Additional coverage includes full rational circles with four Bezier spans;
homogeneous weight scaling; deterministic positive-weight curves of degrees
1..3 with large offsets, varying coordinate scales and parameter domains;
an interior stationary cubic; a kink between linear spans; and degree 4 / lower
knot multiplicity fallback. An anomalous curve tagged 2D with nonzero stored Z
must retain the real evaluator's radius failure. SSE FTZ/DAZ mode also exercises
fallback without changing the caller's arithmetic mode inside the validator.

A registered derived curve forwards `Evaluate` unchanged and counts requests
for second and higher derivatives. Calls through `SmCurve::AssertValid` in both
2D and 3D must consult that evaluator via the existing GPA fallback. This catches
the former static `GetClassType` gate through the real validation interface;
it does not manufacture a disagreement between control data and geometry.

## Conservative early pass

The validator can prove speed and radius thresholds from the stored homogeneous
control data of the concrete B-spline type, identified by the virtual `GetType`
from the kernel's existing `SM_COMMON` runtime identity convention. It supports positive rational weights
or consistent NLib `NL_NOW` polynomial data, degree 1..3, at most 16 clamped Bezier
spans with full internal knot multiplicity. It does not insert knots or change the
curve. Existing knot, continuity, endpoint and internal-pole checks still run.

For each span, outward-rounded interval arithmetic encloses `C'` and `C''` over
the original knot parameter. A speed lower bound and acceleration upper bound
give `radius >= speed^2 / acceleration`. At most three midpoint subdivisions
reduce overly broad bounds. Each property search is skipped only when its own
threshold is proved. A failed proof is not a rejection: it uses the original
global property analysis, retaining that solver's existing limitations.

The proof requires IEEE binary64, SSE arithmetic without FTZ/DAZ, masked SSE
exceptions and a build without fast/finite-only math assumptions. MSVC helpers
force precise arithmetic. Other architectures and unsupported environments use
the original solver. This establishes only these regularity predicates; it does
not establish an import accuracy bound or replace source/native correspondence
checks.
