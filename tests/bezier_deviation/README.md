# Quadratic curve / Bézier deviation certificate

`bezier_deviation` exercises the private numerical certificate against independent
homogeneous de Casteljau construction and actual native point evaluators. Build
with `SMLIB_BUILD_VALIDATION=ON` and matching kernel configuration/CRT; see the
[SDK build guide](../../README.md). Run the focused CTest in Release and Debug,
and a separate `/GR-` consumer when changing runtime type selection.

## Scope and lifetime

The cache keeps its polygon, chord, angle, step, continuity and null-curve checks.
Only its final curve-to-Bézier deviation probe can be replaced by a certificate.
Unsupported or inconclusive evidence retains the original 20-step/local-solver
path; refusal is not a successful query or a new import rejection.

`Prepare` accepts concrete, correctly registered `SmBSplineCurve`, `SmCircle` and
`SmEllipse` evaluators with existing, clamped, already separated quadratic NLib
spans. It owns copied actual coefficients and native knots, never analytic radius
or angle metadata. The closed `QuadraticReference` cannot be default constructed
by callers. Its bounded storage lives only within one `Tessellate` invocation,
after Bézier extraction, while the defining geometry is unchanged. An edit needs
a fresh reference; this adds no persistent result cache or thread-safety claim.

At most 16 spans/33 poles, matching 2D/3D conventions, consistent polynomial
sentinels or positive weights, raw weights in `[2^-8,2^8]` and an outward weight
ratio at most 4 form the proof envelope. Coordinate magnitude is at most `2^100`,
knot magnitude at most `2^60`, and nonzero spans lie in `[2^-30,2^60]`. These are
refusal limits, not new modeling tolerances or import-quality defaults. Derived
evaluators, `SmCrvOnSurf`/Hermite approximations, other bases and conditioning keep
the numerical path. UV coordinate errors retain their original parameter units;
they are not relabeled as millimetres.

## Continuous bound

Let the restricted reference be `R=N/W`, the leaf `L=NL/WL`, and `lambda` any
finite positive double. The identity

`R-L = (N-lambda*NL - L*(W-lambda*WL))/W`

gives a sufficient bound without forming a degree-4 polynomial product. For
quadratic Bernstein coefficients, convex hulls bound each numerator residual by
its largest coefficient magnitude, the denominator below by its smallest positive
coefficient, and every leaf coordinate by the largest Euclidean leaf pole.
The coordinate maximum times dimension bounds the Euclidean distance by L1.

The reference is restricted using actual native leaf endpoints and knot spans;
native parameters are never interpreted as angles. A tiny fixed `constexpr`
blossom stencil selects `(a,a)`, `(a,b)`, `(b,b)`. Clamped parameter conversion has
absolute error below `8*epsilon`; each blossom parameter derivative is bounded by
`2*M`. Two convex-lerp levels have fewer than 16 rounded operations on a path.
Normalization, conversion, interpolation and underflow therefore fit within
`512*epsilon*M`. The chosen leaf scale has scaled weights below 8. Its rounded
product/subtraction residual fits within another `32*epsilon*M`.

The implemented coefficient envelope is the conservative `4096*epsilon*M`, with
`M=max(1, reference coordinate scale, 8*leaf coordinate scale)`; the weight envelope
uses `4096*epsilon`. The denominator lower bound is rounded outward. All remaining
bound arithmetic is positive and normal; a factor `1+64*epsilon` followed by
`nextafter(+infinity)` covers fewer than 32 operations. `consteval` gamma checks
keep these small operation envelopes checked at compile time.

## Native point-evaluation rounding

This is not an assumption that algebraically equivalent floating evaluations
return identical bits. The certificate adds, for each curve, the L1 allowance
`3*4096*epsilon*weight_ratio^2*coordinate_scale`. The audited native position path
is [`SmBSplineCurve::EvaluatePoint`](../../source/SMLib/src/SmBSplineCurve.cpp),
[`N_CrvEval`](../../source/SMLib/src/NL_CrvNurb.cpp),
[`N_BasisEval` / `N_BasisFindSpan`](../../source/SMLib/src/NL_BasisBasic.cpp) and
[`N_CPtToPtEuclid`](../../source/SMLib/src/NL_Math.cpp).

For a separated quadratic, the three inner basis iterations and homogeneous
accumulation/projection have fewer than 64 rounded operations on a path. Positive
weights bound the conditioning by the guarded ratio; the 4096 envelope dominates
the resulting gamma error and gradual-underflow absolute terms. The span floor
also stays outside NLib's `1e-12` degeneracy handling and `1e-24` denominator guard.
The magnitude/weight envelope precludes overflow and zero denominators. Endpoint
shortcuts require only homogeneous projection. Large coordinates or tight budgets
that cannot absorb this allowance are refused.

The backend requires IEC559 binary64, supported SSE2 arithmetic, nearest rounding,
masked exceptions and gradual underflow; it is disabled for fast-math builds.
Unsupported environments fall back without changing rounding controls. A bound
is accepted only within the **unchanged incoming tolerance**. Other geometry
quality checks, STEP validation, surface-interior extrema, precision requests,
complete reports, native-call accounting, cancellation and publication remain
independent.

## Regression evidence

Controls include rational/polynomial 2D/3D spans; clipped, reversed and edited
native parameters; stale analytic metadata; nonzero allowed perturbations and
out-of-budget leaf changes; correctly registered source/leaf subclasses; extreme
weights, layouts, coordinate scales and short spans; reference ownership after
edits/destruction; FTZ and directed rounding. Dense native observations check the
implementation but do not establish the continuous bound. Existing STEP conic
and bulged-surface controls separately guard complete reports, cancellation and
surface-interior bounds. None of these controls grants full import conformance
or a global continuous error bound for arbitrary geometry.
