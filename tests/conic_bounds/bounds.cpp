// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmBSplineCurve.h>
#include <SmCircle.h>
#include <SmContext.h>
#include <SmEllipse.h>
#include <SmSolutionArray.h>
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <nurbs.h>
#include <stdexcept>
#if defined(_M_X64) || defined(__SSE2__)
#    include <xmmintrin.h>
#endif
#ifdef CONIC_BOUNDS_INTERNAL_TESTS
#    include "SmConicBounds.h"
#endif
namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
void Near(double a, double b, double tolerance, const char* message)
{
    Require(std::isfinite(a) && std::abs(a - b) <= tolerance, message);
}
bool FastEnvironment()
{
#if (defined(_M_X64) || defined(__SSE2__)) && !defined(__FAST_MATH__) && !defined(_M_FP_FAST) &&                                                     \
    (!defined(__FINITE_MATH_ONLY__) || __FINITE_MATH_ONLY__ == 0)
    const auto state = _mm_getcsr();
    return std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53 && std::fegetround() == FE_TONEAREST &&
           (state & ((1U << 15) | (1U << 6) | 0x6000U)) == 0 && (state & 0x1F80U) == 0x1F80U;
#else
    return false;
#endif
}
constexpr SM_TYPE forwardingType = 19996;
class ForwardingCircle : public SmCircle
{
public:

    using SmCircle::SmCircle;
    explicit ForwardingCircle(const SmCircle& curve) : SmCircle(curve)
    {
    }
    mutable ULONG calls = 0;
    bool failProperty = false;
    SmStatus Evaluate(double t, ULONG n, SmBoolean left, SmVector3d out[], SmBoolean nonzero = FALSE) const override
    {
        ++calls;
        return SmBSplineCurve::Evaluate(t, n, left, out, nonzero);
    }
    SmStatus GlobalPropertyAnalysis(
        const SmExtent1d& interval,
        SmCurvePropertyType property,
        const double* point,
        const SmVector3d* vector,
        double tolerance,
        SmSolutionArray& solutions,
        SmTArray<SmPoint3d>* parameters = nullptr,
        SmTArray<SmPoint3d>* comb = nullptr
    ) const override
    {
        if (failProperty)
        {
            return SM_ERR;
        }
        return SmCurve::GlobalPropertyAnalysis(interval, property, point, vector, tolerance, solutions, parameters, comb);
    }
    SM_COMMON(ForwardingCircle, SmCircle, forwardingType);
};
SmBoolean ForwardingCircle::IsKindOf(SM_TYPE type) const
{
    return type == forwardingType || SmCircle::IsKindOf(type);
}
void ForwardingCircle::Dump() const
{
    SmBSplineCurve::Dump(FALSE);
}

// Independent analytic support functions for C+a*cos(theta)*X+b*sin(theta)*Y.
// Tests select an angular interval through the existing conversion API, then
// query through SmCurve using its actual native NURBS interval.
void Check(
    SmEllipse& curve,
    const SmPoint3d& center,
    const SmVector3d& x,
    const SmVector3d& y,
    double a,
    double b,
    SmExtent1d angles,
    bool requireFast = true
)
{
    SmTArray<SmExtent1d> intervals;
    Require(curve.ConvertIvlFromSTEPToNURBS(angles, intervals) == SM_SUCCESS && intervals.GetSize() > 0, "angular/native interval conversion");
    SmExtent3d box;
    const SmCurve& dispatched = curve;
    for (ULONG i = 0; i < intervals.GetSize(); ++i)
    {
        SmExtent3d part;
        Require(dispatched.CalculateTightBoundingBox(intervals[i], &part) == SM_SUCCESS, "public conic tight box");
        box.Union(part, box);
#ifdef CONIC_BOUNDS_INTERNAL_TESTS
        if (requireFast && FastEnvironment())
        {
            SmExtent3d direct;
            const auto attempt = sm_conic_bounds::Try(curve, intervals[i], direct);
            Require(attempt.handled && attempt.status == SM_SUCCESS, "supported conic must use coefficient path");
        }
#else
        (void)requireFast;
#endif
    }
    const double origin[] = { center.x, center.y, center.z }, xx[] = { x.x, x.y, x.z }, yy[] = { y.x, y.y, y.z };
    const auto low = box.GetMin(), high = box.GetMax();
    const double actualLo[] = { low.x, low.y, low.z }, actualHi[] = { high.x, high.y, high.z };
    for (int axis = 0; axis < 3; ++axis)
    {
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        auto add = [&](double theta)
        {
            const double value = origin[axis] + a * xx[axis] * std::cos(theta) + b * yy[axis] * std::sin(theta);
            lo = std::min(lo, value);
            hi = std::max(hi, value);
        };
        const double t0 = angles.GetMin() * SM_PI / 180, t1 = angles.GetMax() * SM_PI / 180;
        add(t0);
        add(t1);
        const double root = std::atan2(b * yy[axis], a * xx[axis]);
        for (int k = -4; k <= 4; ++k)
        {
            const double t = root + k * SM_PI;
            if (t >= t0 && t <= t1)
            {
                add(t);
            }
        }
        // Clipped controls include the existing angular-to-native converter's
        // error. Full-period support values have no endpoint-conversion error
        // and are checked independently at a much tighter coordinate scale.
        const double analyticTolerance = angles.GetMax() - angles.GetMin() >= 360 ? 1e-10 : 2e-6 * std::max(a, b);
        const double tolerance = std::max(analyticTolerance, 32 * std::numeric_limits<double>::epsilon() * std::max(1., std::abs(origin[axis])));
        if (std::abs(actualLo[axis] - lo) > tolerance || std::abs(actualHi[axis] - hi) > tolerance)
        {
            std::printf(
                "bounds angle=[%.17g,%.17g] radius=[%.17g,%.17g] axis=%d actual=[%.17g,%.17g] expected=[%.17g,%.17g]\n",
                angles.GetMin(),
                angles.GetMax(),
                a,
                b,
                axis,
                actualLo[axis],
                actualHi[axis],
                lo,
                hi
            );
        }
        Near(actualLo[axis], lo, tolerance, "independent analytic lower bound");
        Near(actualHi[axis], hi, tolerance, "independent analytic upper bound");
    }
    // Numerical containment is a regression oracle, not the continuous proof.
    for (ULONG segment = 0; segment < intervals.GetSize(); ++segment)
    {
        for (int i = 0; i <= 128; ++i)
        {
            const auto interval = intervals[segment];
            SmPoint3d p;
            Require(curve.EvaluatePoint(interval.Evaluate(double(i) / 128), p) == SM_SUCCESS, "native evaluation");
            const double q[] = { p.x, p.y, p.z };
            for (int axis = 0; axis < 3; ++axis)
            {
                const double roundoff = 64 * std::numeric_limits<double>::epsilon() * std::max({ 1., std::abs(q[axis]), a, b });
                Require(q[axis] >= actualLo[axis] - roundoff && q[axis] <= actualHi[axis] + roundoff, "sample escaped actual-native bound");
            }
        }
    }
}
void Run()
{
    SmContext context;
    const SmPoint3d c(0, 0, 0);
    const SmVector3d x(1, 0, 0), y(0, 1, 0);
    SmCircle circle(c, x, y, SmExtent1d(0, 360), 3, 3, &context);
    if (!FastEnvironment())
    {
        Check(circle, c, x, y, 3, 3, SmExtent1d(0, 360), false);
#ifdef CONIC_BOUNDS_INTERNAL_TESTS
        SmExtent3d skipped;
        Require(!sm_conic_bounds::Try(circle, circle.GetNaturalInterval(), skipped).handled, "unsupported backend retains general solver");
#endif
        return;
    }
    // Red on the old implementation: it adds knots outside a requested subarc.
    Check(circle, c, x, y, 3, 3, SmExtent1d(0, 45));
    for (const auto range : { SmExtent1d(0, 360), SmExtent1d(15, 273), SmExtent1d(42, 43), SmExtent1d(90, 180), SmExtent1d(270, 360) })
    {
        Check(circle, c, x, y, 3, 3, range);
    }
    SmEllipse ellipse(c, x, y, SmExtent1d(-180, 180), 7, .25, 3, &context);
    Check(ellipse, c, x, y, 7, .25, SmExtent1d(-180, 180));
    Check(ellipse, c, x, y, 7, .25, SmExtent1d(-170, 170));
    Check(ellipse, c, x, y, 7, .25, SmExtent1d(-40, 65));
    const SmVector3d rx(.6, .8, 0), ry(-.48, .36, .8);
    const SmPoint3d shifted(2, -3, 5);
    SmEllipse rotated(shifted, rx, ry, SmExtent1d(-90, 270), 7, 2, 3, &context);
    Check(rotated, shifted, rx, ry, 7, 2, SmExtent1d(-80, 260));
    Require(rotated.EditParameterization(SmExtent1d(-17, 29)) == SM_SUCCESS, "nonstandard native interval");
    Check(rotated, shifted, rx, ry, 7, 2, SmExtent1d(10, 235));
    SmEllipse arc(shifted, rx, ry, SmExtent1d(20, 110), 7, 2, 3, &context);
    Check(arc, shifted, rx, ry, 7, 2, SmExtent1d(35, 75));
    // Deterministic rotated/clipped controls with multiple interior extrema.
    for (int i = 1; i <= 12; ++i)
    {
        const double angle = .19 * i;
        const SmVector3d ax(std::cos(angle), std::sin(angle), 0), ay(-std::sin(angle), std::cos(angle), 0);
        SmEllipse sample(shifted, ax, ay, SmExtent1d(-180, 180), 1 + .37 * i, .2 + .07 * i, 3, &context);
        Check(sample, shifted, ax, ay, 1 + .37 * i, .2 + .07 * i, SmExtent1d(-170 + 3 * i, 170 - 2 * i));
    }
    SmExtent1d reverse;
    Require(rotated.ReverseParameterization(rotated.GetNaturalInterval(), reverse) == SM_SUCCESS, "native reversal");
    Check(rotated, shifted, rx, -ry, 7, 2, SmExtent1d(105, 390));
    SmCircle two(c, x, y, SmExtent1d(0, 360), 2, 2, &context);
    Check(two, c, x, y, 2, 2, SmExtent1d(5, 175));
    SmEllipse thin(c, x, y, SmExtent1d(0, 360), 7, 1e-7, 3, &context);
    Check(thin, c, x, y, 7, 1e-7, SmExtent1d(10, 260));
    SmCircle tiny(c, x, y, SmExtent1d(0, 360), 1e-7, 3, &context);
    Check(tiny, c, x, y, 1e-7, 1e-7, SmExtent1d(15, 260));
    const SmPoint3d far(1e9, -2e9, 3e9);
    SmCircle distant(far, x, y, SmExtent1d(0, 360), 3, 3, &context);
    Check(distant, far, x, y, 3, 3, SmExtent1d(0, 360), false);
    // Analytic metadata can be edited separately; the native evaluator still
    // follows its existing coefficients. A metadata-only shortcut is wrong.
    SmCircle stale(circle);
    stale.SetXRadius(1000);
    SmExtent3d actual;
    Require(stale.CalculateTightBoundingBox(stale.GetNaturalInterval(), &actual) == SM_SUCCESS, "stale metadata query");
    Near(actual.GetMin().x, -3, 1e-10, "stale metadata lower native bound");
    Near(actual.GetMax().x, 3, 1e-10, "stale metadata upper native bound");
    ForwardingCircle derived(circle);
    SmExtent3d fallback;
    Require(
        derived.CalculateTightBoundingBox(derived.GetNaturalInterval(), &fallback) == SM_SUCCESS && derived.calls > 2,
        "registered derived evaluator fallback"
    );
    derived.failProperty = true;
    Require(derived.CalculateTightBoundingBox(derived.GetNaturalInterval(), &fallback) == SM_ERR, "derived native failure propagated");
#ifdef CONIC_BOUNDS_INTERNAL_TESTS
    SmExtent3d untouched(-37, -38, -39, -36, -37, -38);
    const auto before = untouched.GetMin();
    const auto skipped = sm_conic_bounds::Try(derived, derived.GetNaturalInterval(), untouched);
    Require(!skipped.handled && untouched.GetMin() == before, "unsupported derived attempt leaves output unchanged");
    auto* raw = circle.GetGwNurbPointer();
    const auto savedWeight = raw->pol->Pw[1].w;
    raw->pol->Pw[1].w = 0;
    const auto badWeight = sm_conic_bounds::Try(circle, circle.GetNaturalInterval(), untouched);
    raw->pol->Pw[1].w = savedWeight;
    Require(!badWeight.handled && untouched.GetMin() == before, "nonpositive denominator retains fallback");
    // NLib chooses rational/polynomial storage for the whole curve. A mixed
    // sentinel layout cannot be interpreted independently per Bezier span.
    raw->pol->Pw[1].w = NL_NOW;
    const auto mixed = sm_conic_bounds::Try(circle, circle.GetNaturalInterval(), untouched);
    raw->pol->Pw[1].w = savedWeight;
    Require(!mixed.handled && untouched.GetMin() == before, "mixed weight storage retains fallback");
    const auto degree = raw->p;
    raw->p = 3;
    const auto elevated = sm_conic_bounds::Try(circle, circle.GetNaturalInterval(), untouched);
    raw->p = degree;
    Require(!elevated.handled && untouched.GetMin() == before, "actual unsupported degree retains fallback");
    const auto knot = raw->knt->U[4];
    raw->knt->U[4] = (raw->knt->U[3] + raw->knt->U[5]) / 2;
    const auto nonBezier = sm_conic_bounds::Try(circle, circle.GetNaturalInterval(), untouched);
    raw->knt->U[4] = knot;
    Require(!nonBezier.handled && untouched.GetMin() == before, "unextracted knot layout retains fallback");
    const auto outside = sm_conic_bounds::Try(circle, SmExtent1d(-2, 3), untouched);
    Require(!outside.handled && untouched.GetMin() == before, "outside native domain retains fallback");

#    if defined(_M_X64) || defined(__SSE2__)
    const auto saved = _mm_getcsr();
    _mm_setcsr(saved | (1U << 15));
    const auto ftz = sm_conic_bounds::Try(circle, circle.GetNaturalInterval(), untouched);
    _mm_setcsr(saved);
    Require(!ftz.handled && untouched.GetMin() == before, "unsupported arithmetic environment falls back without changing outputs");
    _mm_setcsr((saved & ~0x6000U) | 0x2000U);
    const auto directed = sm_conic_bounds::Try(circle, circle.GetNaturalInterval(), untouched);
    _mm_setcsr(saved);
    Require(!directed.handled && untouched.GetMin() == before, "directed rounding retains fallback");

#    endif
#endif
}
} // namespace
int main()
{
    try
    {
        Run();
        std::puts("Conic bounds: Passed");
        return 0;
    }
    catch (const std::exception& e)
    {
        std::printf("Conic bounds: %s\n", e.what());
        return 1;
    }
}
