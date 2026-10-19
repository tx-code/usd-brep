// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "SmConicBounds.h"

#include "StdAfx.h"

#include <SmBSplineCurve.h>
#include <SmCurveTypes.h>
#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <limits>
#include <numeric>
#include <nurbs.h>
#if defined(_M_X64) || defined(__SSE2__)
#    include <xmmintrin.h>
#endif
#if (defined(_M_X64) || defined(__SSE2__)) && !defined(__FAST_MATH__) && !defined(_M_FP_FAST) &&                                                     \
    (!defined(__FINITE_MATH_ONLY__) || __FINITE_MATH_ONLY__ == 0)
#    define SM_CONIC_BOUNDS_ARITHMETIC 1
#else
#    define SM_CONIC_BOUNDS_ARITHMETIC 0
#endif
#if defined(_MSC_VER) && SM_CONIC_BOUNDS_ARITHMETIC
#    pragma float_control(precise, on, push)
#endif
namespace sm_conic_bounds
{
namespace
{
#if SM_CONIC_BOUNDS_ARITHMETIC
constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr double epsilon = std::numeric_limits<double>::epsilon();
struct Interval
{
    double lo = 0, hi = 0;
    bool finite() const
    {
        return std::isfinite(lo) && std::isfinite(hi) && lo <= hi;
    }
    bool zero() const
    {
        return lo == 0 && hi == 0;
    }
};
Interval point(double a)
{
    return { a, a };
}
Interval invalid()
{
    return { 1, -1 };
}
double down(double x)
{
    return std::nextafter(x, -infinity);
}
double up(double x)
{
    return std::nextafter(x, infinity);
}
Interval operator+(Interval a, Interval b)
{
    if (!a.finite() || !b.finite())
    {
        return invalid();
    }
    if (a.zero())
    {
        return b;
    }
    if (b.zero())
    {
        return a;
    }
    return { down(a.lo + b.lo), up(a.hi + b.hi) };
}
Interval operator-(Interval a, Interval b)
{
    if (!a.finite() || !b.finite())
    {
        return invalid();
    }
    if (b.zero())
    {
        return a;
    }
    if (a.lo == a.hi && a.lo == b.lo && b.lo == b.hi)
    {
        return {};
    }
    return { down(a.lo - b.hi), up(a.hi - b.lo) };
}
Interval operator*(Interval a, Interval b)
{
    if (!a.finite() || !b.finite())
    {
        return invalid();
    }
    if (a.zero() || b.zero())
    {
        return {};
    }
    if (a.lo == 1 && a.hi == 1)
    {
        return b;
    }
    if (b.lo == 1 && b.hi == 1)
    {
        return a;
    }
    const std::array v{ a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi };
    return { down(*std::min_element(v.begin(), v.end())), up(*std::max_element(v.begin(), v.end())) };
}
Interval operator/(Interval a, Interval b)
{
    if (!a.finite() || !b.finite() || b.lo <= 0)
    {
        return invalid();
    }
    if (a.zero())
    {
        return {};
    }
    if (b.lo == 1 && b.hi == 1)
    {
        return a;
    }
    return a * Interval{ down(1 / b.hi), up(1 / b.lo) };
}
Interval lerp(Interval a, Interval b, double t)
{
    if (t == 0)
    {
        return a;
    }
    if (t == 1)
    {
        return b;
    }
    return (point(1) - point(t)) * a + point(t) * b;
}
using Quadratic = std::array<Interval, 3>;
Interval blossom(const Quadratic& p, double a, double b)
{
    return lerp(lerp(p[0], p[1], a), lerp(p[1], p[2], a), b);
}
Quadratic restrictTo(const Quadratic& p, double a, double b)
{
    return { blossom(p, a, a), blossom(p, a, b), blossom(p, b, b) };
}
bool signedPolynomial(const Quadratic& p)
{
    return std::all_of(
               p.begin(),
               p.end(),
               [](Interval x)
               {
                   return x.finite() && x.lo >= 0;
               }
           ) ||
           std::all_of(
               p.begin(),
               p.end(),
               [](Interval x)
               {
                   return x.finite() && x.hi <= 0;
               }
           );
}
Interval hull(const Quadratic& x, const Quadratic& w, double a, double b, double origin)
{
    const auto xx = restrictTo(x, a, b), ww = restrictTo(w, a, b);
    Interval result{ infinity, -infinity };
    for (int i = 0; i < 3; ++i)
    {
        const Interval value = point(origin) + xx[i] / ww[i];
        if (!value.finite())
        {
            return invalid();
        }
        result.lo = std::min(result.lo, value.lo);
        result.hi = std::max(result.hi, value.hi);
    }
    return result;
}
void include(Interval value, Interval& result)
{
    result.lo = std::min(result.lo, value.lo);
    result.hi = std::max(result.hi, value.hi);
}
struct Window
{
    double lo, hi;
};

// For C=N/W, with quadratic homogeneous Bernstein coefficients (Xi,wi),
// C' has positive denominator W^2. Half its numerator has Bernstein data
// [X1*w0-X0*w1, (X2*w0-X0*w2)/2, X2*w1-X1*w2]. Its degree is at most two.
// Roots are analytic candidates only: sign certificates on every remaining
// interval and positive-weight hulls around uncertain roots establish coverage.
// No sample, root residual or fixed angular/native parameter assumption is used.
bool axisBounds(const Quadratic& x, const Quadratic& w, double lo, double hi, double origin, double budget, Interval& result)
{
    result = hull(x, w, lo, lo, origin);
    const Interval end = hull(x, w, hi, hi, origin);
    if (!result.finite() || !end.finite() || up(result.hi - result.lo) > budget || up(end.hi - end.lo) > budget)
    {
        return false;
    }
    include(end, result);
    const Interval whole = hull(x, w, lo, hi, origin);
    if (!whole.finite())
    {
        return false;
    }
    if (up(whole.hi - whole.lo) <= budget)
    {
        include(whole, result);
        return true;
    }
    const Quadratic d{ x[1] * w[0] - x[0] * w[1], point(.5) * (x[2] * w[0] - x[0] * w[2]), x[2] * w[1] - x[1] * w[2] };
    if (signedPolynomial(restrictTo(d, lo, hi)))
    {
        return true;
    }
    if (!std::all_of(
            d.begin(),
            d.end(),
            [](Interval v)
            {
                return v.finite();
            }
        ))
    {
        return false;
    }
    std::array<Window, 4> windows{};
    int count = 0;
    constexpr double radius = 0x1p-23; // 8*sqrt(binary64 epsilon), exactly representable.
    auto add = [&](double root)
    {
        if (!std::isfinite(root) || root < lo - radius || root > hi + radius)
        {
            return;
        }
        const Window window{ std::max(lo, down(root - radius)), std::min(hi, up(root + radius)) };
        if (window.lo <= window.hi)
        {
            windows[count++] = window;
        }
    };
    auto uncertain = [&](double t)
    {
        const Interval value = blossom(d, t, t);
        return value.lo <= 0 && value.hi >= 0;
    };
    if (uncertain(lo))
    {
        add(lo);
    }
    if (uncertain(hi))
    {
        add(hi);
    }
    const double v0 = std::midpoint(d[0].lo, d[0].hi), v1 = std::midpoint(d[1].lo, d[1].hi), v2 = std::midpoint(d[2].lo, d[2].hi);
    double a = v0 - 2 * v1 + v2, b = 2 * (v1 - v0), c = v0;
    const double scale = std::max({ std::abs(a), std::abs(b), std::abs(c) });
    if (!std::isfinite(scale))
    {
        return false;
    }
    if (scale > 0)
    {
        a /= scale;
        b /= scale;
        c /= scale;
        if (a == 0)
        {
            if (b != 0)
            {
                add(-c / b);
            }
        }
        else
        {
            const double discriminant = std::fma(b, b, -4 * a * c);
            if (discriminant >= 0)
            {
                const double q = -.5 * (b + std::copysign(std::sqrt(discriminant), b));
                if (q != 0)
                {
                    add(q / a);
                    add(c / q);
                }
                else
                {
                    add(-b / (2 * a));
                }
            }
        }
    }
    std::sort(
        windows.begin(),
        windows.begin() + count,
        [](Window a, Window b)
        {
            return a.lo < b.lo;
        }
    );
    int merged = 0;
    for (int i = 0; i < count; ++i)
    {
        if (merged && windows[i].lo <= windows[merged - 1].hi)
        {
            windows[merged - 1].hi = std::max(windows[merged - 1].hi, windows[i].hi);
        }
        else
        {
            windows[merged++] = windows[i];
        }
    }
    double cursor = lo;
    for (int i = 0; i < merged; ++i)
    {
        const auto window = windows[i];
        if (cursor < window.lo && !signedPolynomial(restrictTo(d, cursor, window.lo)))
        {
            return false;
        }
        const Interval enclosure = hull(x, w, window.lo, window.hi, origin);
        if (!enclosure.finite() || up(enclosure.hi - enclosure.lo) > budget)
        {
            return false;
        }
        include(enclosure, result);
        cursor = window.hi;
    }
    return cursor >= hi || signedPolynomial(restrictTo(d, cursor, hi));
}
Interval translated(double coordinate, double weight, double origin)
{
    if (origin == 0)
    {
        return point(coordinate);
    }
    if (weight == 1 && coordinate == origin)
    {
        return {};
    }
    // fma encloses X-origin*w without a large intermediate product rounding.
    // The origin is arbitrary; it changes neither curve nor native parameter.
    const double residual = std::fma(-origin, weight, coordinate);
    return { down(residual), up(residual) };
}
#endif
} // namespace
Attempt Try(const SmCurve& curve, const SmExtent1d& interval, SmExtent3d& output)
{
    // GetClassType is static. Only the registered virtual identity excludes
    // correctly registered subclasses with a different evaluator definition.
    const auto type = curve.GetType();
    if (type != SmCircle_TYPE && type != SmEllipse_TYPE)
    {
        return {};
    }
#if SM_CONIC_BOUNDS_ARITHMETIC
    const unsigned arithmetic = _mm_getcsr();
    if (!std::numeric_limits<double>::is_iec559 || std::numeric_limits<double>::digits != 53 || std::fegetround() != FE_TONEAREST ||
        (arithmetic & ((1U << 15) | (1U << 6) | 0x6000U)) != 0 || (arithmetic & 0x1F80U) != 0x1F80U)
    {
        return {};
    }
    const double first = interval.GetMin(), last = interval.GetMax();
    if (!std::isfinite(first) || !std::isfinite(last) || first >= last)
    {
        return {};
    }
    const auto& spline = static_cast<const SmBSplineCurve&>(curve);
    const auto* raw = spline.GetGwNurbPointer();
    if (!raw)
    {
        const SmStatus status = const_cast<SmBSplineCurve&>(spline).MakeNurb();
        if (status != SM_SUCCESS)
        {
            return { true, status };
        }
        raw = spline.GetGwNurbPointer();
        if (!raw)
        {
            return { true, SM_ERR_INVALID_INPUT };
        }
    }
    const auto dimension = curve.GetDim();
    // Bounded, already-separated quadratic Beziers. Read the actual NLib order,
    // homogeneous poles and full knots, not SmEllipse's analytic degree/tag.
    // Other bases (including degree elevation and non-Bezier knot layouts) keep
    // the original solver; no knot insertion or representation replacement occurs.
    if (dimension < 2 || dimension > 3 || raw->p != 2 || !raw->pol || !raw->knt || !raw->pol->Pw || !raw->knt->U || raw->pol->n < 2 ||
        raw->pol->n > 32 || raw->pol->n % 2 || raw->knt->m != raw->pol->n + 3)
    {
        return {};
    }
    const auto* knots = raw->knt->U;
    const int end = static_cast<int>(raw->knt->m), n = static_cast<int>(raw->pol->n);
    for (int i = 0; i <= 2; ++i)
    {
        if (!std::isfinite(knots[i]) || !std::isfinite(knots[end - i]) || knots[i] != knots[0] || knots[end - i] != knots[end])
        {
            return {};
        }
    }
    const auto natural = curve.GetNaturalInterval();
    if (first < knots[0] || last > knots[end] || first < natural.GetMin() || last > natural.GetMax())
    {
        return {};
    }
    const bool polynomial = raw->pol->Pw[0].w == NL_NOW;
    SmPoint3d minimum(infinity, infinity, infinity), maximum(-infinity, -infinity, -infinity);
    bool covered = false;
    for (int start = 0; start < n; start += 2)
    {
        const double a = knots[start + 2], b = knots[start + 3];
        if (!std::isfinite(a) || !std::isfinite(b) || a >= b || knots[start + 4] != b)
        {
            return {};
        }
        const double from = std::max(first, a), to = std::min(last, b);
        if (from > to)
        {
            continue;
        }
        // Outward conversion of the ACTUAL knot span to local Bezier t. It is
        // unrelated to the analytic angle and honors arbitrary parameter edits.
        Interval ta = from == a ? point(0) : (point(from) - point(a)) / (point(b) - point(a));
        Interval tb = to == b ? point(1) : (point(to) - point(a)) / (point(b) - point(a));
        if (!ta.finite() || !tb.finite())
        {
            return {};
        }
        const double lo = std::max(0., ta.lo), hi = std::min(1., tb.hi);
        if (lo > hi)
        {
            return {};
        }
        double weights[3]{};
        double weightScale = 0;
        for (int i = 0; i < 3; ++i)
        {
            const auto& pole = raw->pol->Pw[start + i];
            if ((pole.w == NL_NOW) != polynomial || (dimension == 2 && pole.z != 0 && pole.z != NL_NOZ) || (dimension == 3 && pole.z == NL_NOZ))
            {
                return {};
            }
            weights[i] = polynomial ? 1 : pole.w;
            if (!std::isfinite(weights[i]) || weights[i] <= 0)
            {
                return {};
            }
            weightScale = std::max(weightScale, weights[i]);
        }
        Quadratic w{};
        for (int i = 0; i < 3; ++i)
        {
            w[i] = point(weights[i]) / point(weightScale);
            if (!w[i].finite() || w[i].lo <= 0)
            {
                return {};
            }
        }
        double lowerBound[3]{}, upperBound[3]{};
        for (int axis = 0; axis < 3; ++axis)
        {
            Quadratic x{};
            const auto& p0 = raw->pol->Pw[start];
            const double zero = axis == 0 ? p0.x : axis == 1 ? p0.y : dimension == 2 ? 0 : p0.z;
            const double origin = zero / weights[0];
            if (!std::isfinite(origin))
            {
                return {};
            }
            double scale = std::max(1., std::abs(origin));
            for (int i = 0; i < 3; ++i)
            {
                const auto& p = raw->pol->Pw[start + i];
                const double value = axis == 0 ? p.x : axis == 1 ? p.y : dimension == 2 ? 0 : p.z;
                if (!std::isfinite(value))
                {
                    return {};
                }
                x[i] = translated(value, weights[i], origin) / point(weightScale);
                scale = std::max(scale, std::abs(value / weights[i]));
            }
            // This is a refusal threshold, never a model tolerance or padding.
            // Returned values come from outward coefficient enclosures. Reject
            // a wide root/constant-coordinate enclosure rather than loosening
            // the general query's existing 1e-6 numerical scale.
            const double budget = std::min(double(SM_EFF_ZERO_SQRT), 512 * epsilon * scale);
            Interval bound;
            if (!std::isfinite(scale) || !axisBounds(x, w, lo, hi, origin, budget, bound))
            {
                return {};
            }
            lowerBound[axis] = bound.lo;
            upperBound[axis] = bound.hi;
        }
        minimum.x = std::min(minimum.x, lowerBound[0]);
        minimum.y = std::min(minimum.y, lowerBound[1]);
        minimum.z = std::min(minimum.z, lowerBound[2]);
        maximum.x = std::max(maximum.x, upperBound[0]);
        maximum.y = std::max(maximum.y, upperBound[1]);
        maximum.z = std::max(maximum.z, upperBound[2]);
        covered = true;
    }
    if (!covered)
    {
        return {};
    }
    // Keep actual endpoint evaluation and its status. Nothing is published if
    // it fails; no partial or failed numerical result is converted to success.
    SmPoint3d beginPoint, endPoint;
    SmStatus status = curve.EvaluatePoint(first, beginPoint);
    if (status != SM_SUCCESS)
    {
        return { true, status };
    }
    status = curve.EvaluatePoint(last, endPoint);
    if (status != SM_SUCCESS)
    {
        return { true, status };
    }
    if (!SM_IS_VALID_DOUBLE(beginPoint.x) || !SM_IS_VALID_DOUBLE(beginPoint.y) || !SM_IS_VALID_DOUBLE(beginPoint.z) ||
        !SM_IS_VALID_DOUBLE(endPoint.x) || !SM_IS_VALID_DOUBLE(endPoint.y) || !SM_IS_VALID_DOUBLE(endPoint.z))
    {
        return {};
    }
    SmExtent3d result(minimum.x, minimum.y, minimum.z, maximum.x, maximum.y, maximum.z);
    result.AddPoint3d(beginPoint);
    result.AddPoint3d(endPoint);
    output = result;
    return { true, SM_SUCCESS };
#else
    SM_REF2(interval, output);
    return {};
#endif
}
} // namespace sm_conic_bounds

#if defined(_MSC_VER) && SM_CONIC_BOUNDS_ARITHMETIC
#    pragma float_control(pop)
#endif
