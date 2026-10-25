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

Speed retains the threshold `boundingBoxDiagonal / parameterLength / 10000`.
Radius uses the separate threshold `boundingBoxDiagonal / 10000`, preserving the
old radius threshold at a unit parameter span. The former shared threshold
compared a geometric radius with a speed, so merely scaling the knots could
reject a regular curve or hide an existing small-radius finding. This is an
intentional behavior correction: the `.001` minor-radius ellipse now produces
the same radius diagnostic with parameter spans 1 and 1000. Infinite curves
retain the existing absolute threshold `1e-3` for each property.

These are coordinate-space validator quality criteria, not a universal definition
of valid geometry or import correctness. In particular, a UV radius is measured
in chart coordinates and is not a physical mm radius. The existing GPA search
and solution-separation policy remain unchanged; this correction does not claim
that every extreme parameter range has an established numerical result.

Public polynomial controls independently use `C(t)=(a*x,x*x,0)`, where
`x=2*t/T-1`. Their minimum radius is exactly `a*a/2`, independent of the knot
span `T`. For `a=1`, spans 1 and `2^-12` both pass; for `a=2^-8`, spans 1 and 256
both retain the small-radius diagnostic. Each case runs in 2D and 3D through
the concrete B-spline and a registered forwarding evaluator, with LEVEL2,
selected-radius, unrelated and empty test requests. The tests compare native
pole/knot/domain bits and owner/context identities before and after validation.

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


## Property scalar/parameter identity

Local min/max property results must pair the scalar with the returned parameter,
which can differ from Brent's cached current iterate. The property seam now
re-evaluates that parameter before returning a witness; an unevaluable or
nonfinite candidate cannot supply a scalar. That scalar/parameter correction does
not prove global minimality; the separate radius/speed threshold correction is
described above.

Public deterministic monotone-X cubic splines compare each returned radius with
`|C′|^3/|C′ x C″|` evaluated independently at that parameter. A five-pole case
fails against the pre-fix runtime and passes after the fix. Additional straight,
inflection and explicitly refused evaluator cases retain ordinary no-result/error
behavior, while the existing small-radius ellipse controls still reject.
