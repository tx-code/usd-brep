// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "SmBezierDeviation.h"

#include <SmBSplineCurve.h>
#include <SmCircle.h>
#include <SmContext.h>
#include <SmEllipse.h>
#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <nurbs.h>
#include <stdexcept>
#include <type_traits>
#if defined(_M_X64) || defined(__SSE2__)
#    include <xmmintrin.h>
#endif
namespace
{
static_assert(std::is_trivially_copyable_v<sm_bezier_deviation::QuadraticReference>);
static_assert(!std::is_default_constructible_v<sm_bezier_deviation::QuadraticReference>);
static_assert(sizeof(sm_bezier_deviation::QuadraticReference) <= 2048);
bool SupportedEnvironment()
{
#if (defined(_M_X64) || defined(__SSE2__)) && !defined(__FAST_MATH__) && !defined(_M_FP_FAST) &&                                                     \
    (!defined(__FINITE_MATH_ONLY__) || __FINITE_MATH_ONLY__ == 0)
    const auto state = _mm_getcsr();
    return std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53 && std::fegetround() == FE_TONEAREST &&
           !(state & ((1U << 15) | (1U << 6) | 0x6000U)) && (state & 0x1F80U) == 0x1F80U;
#else
    return false;
#endif
}
void Require(bool b, const char* m)
{
    if (!b)
    {
        throw std::runtime_error(m);
    }
}
std::unique_ptr<SmBSplineCurve> Make(
    SmContext& context,
    int dim,
    const std::array<SmPoint3d, 3>& points,
    const std::array<double, 3>& weights,
    double a,
    double b
)
{
    SmTArray<SmPoint3d> cp;
    SmTArray<double> w, knots;
    SmTArray<ULONG> mult;
    for (int i = 0; i < 3; ++i)
    {
        cp.Add(points[i]);
        w.Add(weights[i]);
    }
    knots.Add(a);
    knots.Add(b);
    mult.Add(3);
    mult.Add(3);
    SmBSplineCurve* raw = nullptr;
    const auto status = SmBSplineCurve::CreateCanonical(context, dim, 2, cp, SM_CF_UNSPECIFIED, mult, knots, SM_KT_UNSPECIFIED, &w, nullptr, raw);
    if (status != SM_SUCCESS)
    {
        std::printf(
            "canonical status=%d dim=%d weights=[%.17g,%.17g,%.17g] domain=[%.17g,%.17g]\n",
            status,
            dim,
            weights[0],
            weights[1],
            weights[2],
            a,
            b
        );
    }
    Require(status == SM_SUCCESS, "canonical quadratic");
    return std::unique_ptr<SmBSplineCurve>(raw);
}
std::unique_ptr<SmBSplineCurve> Leaf(SmContext& context, const SmBSplineCurve& source, int start, double from, double to)
{
    const auto* raw = source.GetGwNurbPointer();
    const long double a = raw->knt->U[start + 2], b = raw->knt->U[start + 3];
    const long double t0 = (from - a) / (b - a), t1 = (to - a) / (b - a);
    std::array<std::array<long double, 4>, 3> p{};
    for (int i = 0; i < 3; ++i)
    {
        const auto& q = raw->pol->Pw[start + i];
        p[i] = { q.x, q.y, source.GetDim() == 2 ? 0.L : static_cast<long double>(q.z), q.w == NL_NOW ? 1.L : static_cast<long double>(q.w) };
    }
    auto blossom = [&](long double u, long double v)
    {
        std::array<long double, 4> out{};
        for (int k = 0; k < 4; ++k)
        {
            out[k] = (1 - v) * ((1 - u) * p[0][k] + u * p[1][k]) + v * ((1 - u) * p[1][k] + u * p[2][k]);
        }
        return out;
    };
    const std::array q{ blossom(t0, t0), blossom(t0, t1), blossom(t1, t1) };
    std::array<SmPoint3d, 3> points{};
    std::array<double, 3> weights{};
    for (int i = 0; i < 3; ++i)
    {
        weights[i] = double(q[i][3]);
        points[i] = SmPoint3d(double(q[i][0] / q[i][3]), double(q[i][1] / q[i][3]), double(q[i][2] / q[i][3]));
    }
    return Make(context, int(source.GetDim()), points, weights, from, to);
}
void Check(const SmBSplineCurve& source, const SmBSplineCurve& leaf, double tolerance, bool expected)
{
    const auto interval = leaf.GetNaturalInterval();
    const auto bound = sm_bezier_deviation::Bound(source, leaf, interval, tolerance);
    Require(bool(bound) == expected, expected ? "exact quadratic needs certificate" : "unestablished input must retain fallback");
    if (bound)
    {
        Require(std::isfinite(*bound) && *bound >= 0 && *bound <= tolerance, "certificate within original tolerance");
        for (int i = 0; i <= 128; ++i)
        {
            const double u = interval.Evaluate(double(i) / 128);
            SmPoint3d a, b;
            Require(source.EvaluatePoint(u, a) == SM_SUCCESS && leaf.EvaluatePoint(u, b) == SM_SUCCESS, "independent native evaluators");
            Require(a.DistanceBetween(b) <= *bound, "native numerical difference exceeds certificate");
        }
    }
}
constexpr SM_TYPE forwardingType = 19995;
class ForwardingCurve : public SmBSplineCurve
{
public:

    explicit ForwardingCurve(const SmBSplineCurve& c) : SmBSplineCurve(c)
    {
    }
    SmStatus EvaluatePoint(double u, SmPoint3d& p) const override
    {
        const auto s = SmBSplineCurve::EvaluatePoint(u, p);
        p.z += .1;
        return s;
    }
    SM_COMMON(ForwardingCurve, SmBSplineCurve, forwardingType);
};
SmBoolean ForwardingCurve::IsKindOf(SM_TYPE type) const
{
    return type == forwardingType || SmBSplineCurve::IsKindOf(type);
}
void ForwardingCurve::Dump() const
{
    SmBSplineCurve::Dump(FALSE);
}
void Run()
{
    SmContext context;
    auto source = Make(context, 3, { SmPoint3d(0, 0, 0), SmPoint3d(1, 2, 1), SmPoint3d(3, 0, 2) }, { 1, .75, 1.25 }, -4, 8);
    auto leaf = Leaf(context, *source, 0, -1, 4);
    if (!SupportedEnvironment())
    {
        Require(!sm_bezier_deviation::Bound(*source, *leaf, leaf->GetNaturalInterval(), 1e-3), "unsupported backend fallback");
        return;
    }
    Check(*source, *leaf, 1e-6, true);
    // A prepared reference owns its old definition. A live edit requires a new
    // reference; it must not silently alter an existing certificate's subject.
    const auto snapshot = sm_bezier_deviation::Prepare(*source);
    Require(bool(snapshot), "prepare reference");
    auto* original = source->GetGwNurbPointer();
    const auto oldX = original->pol->Pw[1].x;
    original->pol->Pw[1].x += .25;
    Require(bool(sm_bezier_deviation::Bound(*snapshot, *leaf, leaf->GetNaturalInterval(), 1e-6)), "reference owns pre-edit coefficients");
    Require(!sm_bezier_deviation::Bound(*source, *leaf, leaf->GetNaturalInterval(), 1e-6), "fresh reference observes live edit");
    original->pol->Pw[1].x = oldX;
    Check(*source, *leaf, 1e-18, false);
    auto* raw = leaf->GetGwNurbPointer();
    const auto x = raw->pol->Pw[1].x;
    raw->pol->Pw[1].x += .1;
    Check(*source, *leaf, 1e-6, false);
    raw->pol->Pw[1].x = x;
    ForwardingCurve derived(*source);
    Require(!sm_bezier_deviation::Bound(derived, *leaf, leaf->GetNaturalInterval(), 1e-3), "registered source subclass fallback");
    ForwardingCurve derivedLeaf(*leaf);
    Require(!sm_bezier_deviation::Bound(*source, derivedLeaf, leaf->GetNaturalInterval(), 1e-3), "registered leaf subclass fallback");
    Require(!sm_bezier_deviation::Bound(*source, *leaf, SmExtent1d(-1, 3), 1e-3), "requested interval must match actual leaf domain");
    const SmPoint3d c(2, -3, 5);
    const SmVector3d ax(.6, .8, 0), ay(-.48, .36, .8);
    SmCircle circle(c, ax, ay, SmExtent1d(0, 360), 3, 3, &context);
    for (int start = 0; start < 8; start += 2)
    {
        const auto* n = circle.GetGwNurbPointer();
        const auto a = n->knt->U[start + 2], b = n->knt->U[start + 3];
        auto part = Leaf(context, circle, start, a + (b - a) * .25, a + (b - a) * .625);
        Check(circle, *part, 1e-6, true);
    }
    SmEllipse ellipse(c, ax, ay, SmExtent1d(-90, 270), 7, .25, 3, &context);
    Require(ellipse.EditParameterization(SmExtent1d(-17, 29)) == SM_SUCCESS, "native parameter edit");
    auto part = Leaf(context, ellipse, 2, -2, 1);
    Check(ellipse, *part, 1e-6, true);
    SmExtent1d reversed;
    Require(ellipse.ReverseParameterization(ellipse.GetNaturalInterval(), reversed) == SM_SUCCESS, "reversal");
    const auto* en = ellipse.GetGwNurbPointer();
    const double ea = en->knt->U[4], eb = en->knt->U[5];
    part = Leaf(context, ellipse, 2, ea + .25 * (eb - ea), ea + .75 * (eb - ea));
    Check(ellipse, *part, 1e-6, true);
    SmCircle stale(circle);
    stale.SetXRadius(1000);
    const auto* cn = stale.GetGwNurbPointer();
    auto stalePart = Leaf(context, stale, 0, cn->knt->U[2], cn->knt->U[3]);
    Check(stale, *stalePart, 1e-6, true);
    auto two = Make(context, 2, { SmPoint3d(0, 0, 0), SmPoint3d(1, 2, 0), SmPoint3d(3, 0, 0) }, { 1, 1, 1 }, 0, 1);
    auto twoPart = Leaf(context, *two, 0, .125, .875);
    Check(*two, *twoPart, 1e-6, true);
    auto* twoRaw = two->GetGwNurbPointer();
    std::array<double, 3> savedWeights{};
    for (int i = 0; i < 3; ++i)
    {
        savedWeights[i] = twoRaw->pol->Pw[i].w;
        twoRaw->pol->Pw[i].w = NL_NOW;
    }
    Check(*two, *twoPart, 1e-6, true);
    twoRaw->pol->Pw[1].w = 1;
    Require(!sm_bezier_deviation::Bound(*two, *twoPart, twoPart->GetNaturalInterval(), 1e-3), "mixed polynomial sentinel fallback");
    for (int i = 0; i < 3; ++i)
    {
        twoRaw->pol->Pw[i].w = savedWeights[i];
    }
    // Nonzero, independently known uniform translation. The certificate is a
    // bound within the existing tolerance, not a bitwise-identity shortcut.
    auto* twoLeaf = twoPart->GetGwNurbPointer();
    for (int i = 0; i < 3; ++i)
    {
        twoLeaf->pol->Pw[i].x += 1e-4 * (twoLeaf->pol->Pw[i].w == NL_NOW ? 1 : twoLeaf->pol->Pw[i].w);
    }
    Check(*two, *twoPart, 1e-3, true);
    Check(*two, *twoPart, 1e-5, false);
    for (int i = 0; i < 32; ++i)
    {
        const int dim = i % 2 ? 2 : 3;
        const double k = .125 * i;
        auto sample = Make(
            context,
            dim,
            { SmPoint3d(-2 + k, 1, dim == 2 ? 0 : 2), SmPoint3d(1, 3 + k, dim == 2 ? 0 : -1), SmPoint3d(4, -2 + k, dim == 2 ? 0 : 1) },
            { .8, 1 + .01 * i, 1.25 },
            -3,
            5
        );
        auto portion = Leaf(context, *sample, 0, -2 + .01 * i, 3 + .01 * i);
        Check(*sample, *portion, 1e-6, true);
    }
    auto dying = Make(context, 3, { SmPoint3d(0, 0, 0), SmPoint3d(1, 2, 1), SmPoint3d(3, 0, 2) }, { 1, .75, 1.25 }, -4, 8);
    auto retainedLeaf = Leaf(context, *dying, 0, -1, 4);
    const auto retained = sm_bezier_deviation::Prepare(*dying);
    dying.reset();
    Require(
        retained && sm_bezier_deviation::Bound(*retained, *retainedLeaf, retainedLeaf->GetNaturalInterval(), 1e-6),
        "reference does not borrow destroyed curve"
    );
    auto far = Make(context, 3, { SmPoint3d(1e10, 0, 0), SmPoint3d(1e10 + 1, 2, 1), SmPoint3d(1e10 + 3, 0, 2) }, { 1, .75, 1.25 }, -4, 8);
    auto farPart = Leaf(context, *far, 0, -1, 4);
    Check(*far, *farPart, 1e-7, false);
    auto* s = source->GetGwNurbPointer();
    const auto saved = s->pol->Pw[1].w;
    s->pol->Pw[1].w = 0;
    Require(!sm_bezier_deviation::Bound(*source, *leaf, leaf->GetNaturalInterval(), 1e-3), "nonpositive weights fallback");
    s->pol->Pw[1].w = saved;
    const auto degree = s->p;
    s->p = 3;
    Require(!sm_bezier_deviation::Bound(*source, *leaf, leaf->GetNaturalInterval(), 1e-3), "unsupported degree fallback");
    s->p = degree;
    const auto knot = circle.GetGwNurbPointer()->knt->U[4];
    circle.GetGwNurbPointer()->knt->U[4] += .01;
    Require(!sm_bezier_deviation::Prepare(circle), "unsupported knot layout fallback");
    circle.GetGwNurbPointer()->knt->U[4] = knot;
    auto tiny = Make(context, 3, { SmPoint3d(0, 0, 0), SmPoint3d(1, 2, 1), SmPoint3d(3, 0, 2) }, { 1, .75, 1.25 }, 0, 1);
    for (int i = 3; i < 6; ++i)
    {
        tiny->GetGwNurbPointer()->knt->U[i] = 0x1p-60;
    }
    Require(!sm_bezier_deviation::Prepare(*tiny), "short spans retain NLib degeneracy handling");
    for (int i = 3; i < 6; ++i)
    {
        tiny->GetGwNurbPointer()->knt->U[i] = 1;
    }
    // A valid canonical curve outside the certificate's weight-ratio envelope.
    auto extreme = Make(context, 3, { SmPoint3d(0, 0, 0), SmPoint3d(1, 2, 1), SmPoint3d(3, 0, 2) }, { 1, .1, 1 }, 0, 1);
    Require(!sm_bezier_deviation::Prepare(*extreme), "ill-conditioned weights fallback");
    Require(!sm_bezier_deviation::Bound(*source, *twoPart, twoPart->GetNaturalInterval(), 1e-3), "dimension mismatch fallback");
    Require(
        !sm_bezier_deviation::Bound(*source, *leaf, leaf->GetNaturalInterval(), std::numeric_limits<double>::quiet_NaN()),
        "invalid tolerance fallback"
    );
#if defined(_M_X64) || defined(__SSE2__)
    const auto state = _mm_getcsr();
    _mm_setcsr(state | (1U << 15));
    const auto ftz = sm_bezier_deviation::Bound(*source, *leaf, leaf->GetNaturalInterval(), 1e-3);
    _mm_setcsr(state);
    Require(!ftz, "FTZ fallback");
    _mm_setcsr((state & ~0x6000U) | 0x2000U);
    const auto directed = sm_bezier_deviation::Bound(*source, *leaf, leaf->GetNaturalInterval(), 1e-3);
    _mm_setcsr(state);
    Require(!directed, "directed rounding fallback");
#endif
}
} // namespace
int main()
{
    try
    {
        Run();
        std::puts("Bezier deviation certificate: Passed");
        return 0;
    }
    catch (const std::exception& e)
    {
        std::printf("Bezier deviation certificate: %s\n", e.what());
        return 1;
    }
}
