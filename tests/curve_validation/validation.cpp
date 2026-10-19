// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmContext.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <nurbs.h>
#include <vector>

#if defined(_M_X64) || defined(__SSE2__)
#    include <xmmintrin.h>
#endif

namespace
{
constexpr SM_TYPE forwardingCurveType = 19999;

// Use the kernel's normal runtime type registration. Forward the evaluator
// unchanged so the fixture remains a consistent NURBS curve; its call count
// observes the real AssertValid fallback without inventing mismatched geometry.
class ForwardingCurve final : public SmBSplineCurve
{
public:

    using SmBSplineCurve::SmBSplineCurve;
    mutable ULONG higherDerivativeCalls = 0;

    SmStatus Evaluate(double parameter, ULONG derivatives, SmBoolean fromLeft, SmVector3d output[], SmBoolean nonZeroTangents = FALSE) const override
    {
        if (derivatives >= 2)
        {
            ++higherDerivativeCalls;
        }
        return SmBSplineCurve::Evaluate(parameter, derivatives, fromLeft, output, nonZeroTangents);
    }

    SM_COMMON(ForwardingCurve, SmBSplineCurve, forwardingCurveType);
};

SmBoolean ForwardingCurve::IsKindOf(SM_TYPE type) const
{
    return type == forwardingCurveType || SmBSplineCurve::IsKindOf(type);
}

void ForwardingCurve::Dump() const
{
    SmBSplineCurve::Dump(FALSE);
}

bool checkDerivedEvaluator(SmContext& context, ULONG dimension)
{
    gw_CPOINT points[] = { { 0, 0, 0, NL_NOW }, { .5, .25, 0, NL_NOW }, { 1, 0, 0, NL_NOW } };
    double knots[] = { 0, 0, 0, 1, 1, 1 };
    ForwardingCurve curve(dimension, 2, 5, 2, points, knots, &context);
    const SmCurve& dispatched = curve;
    SmAssertArray reports;
    const bool valid = dispatched.AssertValid(&reports, SM_LEVEL_2, SM_NO_WALK) != FALSE;
    if (!valid || reports.GetSize() != 0 || curve.higherDerivativeCalls == 0)
    {
        std::printf(
            "derived dim=%lu valid=%d reports=%lu higher_derivative_calls=%lu\n",
            dimension,
            valid,
            reports.GetSize(),
            curve.higherDerivativeCalls
        );
        return false;
    }
    return true;
}

bool checkEllipse(SmContext& context, ULONG dimension, double minorRadius, double parameterLength, bool expectedValid, bool selectRadius = true)
{
    const double weight = std::sqrt(0.5);
    gw_CPOINT points[] = { { 1, 0, 0, 1 }, { weight, weight * minorRadius, 0, weight }, { 0, minorRadius, 0, 1 } };
    double knots[] = { 0, 0, 0, parameterLength, parameterLength, parameterLength };
    SmBSplineCurve curve(dimension, 2, 5, 2, points, knots, &context);
    SmAssertArray reports;
    SmTArray<ULONG> requested;
    requested.Add(selectRadius ? (dimension == 2 ? 13UL : 14UL) : 7UL);
    const bool valid = curve.AssertValid(&reports, SM_LEVEL_GIVEN, SM_NO_WALK, &requested) != FALSE;
    bool radiusReported = false;
    for (ULONG index = 0; index < reports.GetSize(); ++index)
    {
        radiusReported |= reports[index]->GetTestIndex() == (dimension == 2 ? 13UL : 14UL);
        if (radiusReported && (reports[index]->GetOwner() != &curve || std::abs(reports[index]->GetValue() - minorRadius * minorRadius) > 1e-10))
        {
            return false;
        }
    }
    if (valid != expectedValid || radiusReported != !expectedValid)
    {
        std::printf(
            "ellipse dim=%lu minor=%g domain=%g selected=%d valid=%d radius_report=%d expected=%d\n",
            dimension,
            minorRadius,
            parameterLength,
            selectRadius,
            valid,
            radiusReported,
            expectedValid
        );
        return false;
    }
    if (!selectRadius)
    {
        return true;
    }

    // Also exercise the normal caller seam, including base and all lower-level checks.
    SmAssertArray fullReports;
    const bool fullValid = (curve.AssertValid(&fullReports, SM_LEVEL_2, SM_NO_WALK) != FALSE) == expectedValid;
    SmAssertArray emptyReports;
    SmTArray<ULONG> empty;
    const bool emptyValid = curve.AssertValid(&emptyReports, SM_LEVEL_GIVEN, SM_NO_WALK, &empty) != FALSE;
    return fullValid && emptyValid && emptyReports.GetSize() == 0;
}

bool checkCurve(
    SmContext& context,
    ULONG dimension,
    int degree,
    std::vector<gw_CPOINT> points,
    std::vector<double> knots,
    bool expectedValid,
    ULONG expectedTest = 0
)
{
    SmBSplineCurve curve(
        dimension,
        static_cast<ULONG>(points.size() - 1),
        static_cast<ULONG>(knots.size() - 1),
        static_cast<short>(degree),
        points.data(),
        knots.data(),
        &context
    );
    SmAssertArray reports;
    const bool valid = curve.AssertValid(&reports, SM_LEVEL_2, SM_NO_WALK) != FALSE;
    bool found = expectedValid;
    for (ULONG i = 0; i < reports.GetSize(); ++i)
    {
        found |= reports[i]->GetTestIndex() == expectedTest;
    }
    if (valid != expectedValid || !found || (expectedValid && reports.GetSize() != 0))
    {
        std::printf(
            "curve dim=%lu degree=%d poles=%zu valid=%d expected=%d report=%lu found=%d\n",
            dimension,
            degree,
            points.size(),
            valid,
            expectedValid,
            expectedTest,
            found
        );
        return false;
    }
    return true;
}

bool checkSpans(SmContext& context, ULONG dimension)
{
    bool passed = true;
    const double w = std::sqrt(0.5);
    const std::vector<gw_CPOINT> circle{ { 1, 0, 0, 1 },   { w, w, 0, w },  { 0, 1, 0, 1 },  { -w, w, 0, w }, { -1, 0, 0, 1 },
                                         { -w, -w, 0, w }, { 0, -1, 0, 1 }, { w, -w, 0, w }, { 1, 0, 0, 1 } };
    for (double weightScale : { 0.25, 1.0, 4.0 })
    {
        auto poles = circle;
        for (auto& p : poles)
        {
            p.x *= weightScale;
            p.y *= weightScale;
            p.w *= weightScale;
        }
        passed &= checkCurve(context, dimension, 2, poles, { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4 }, true);
    }
    // Interior stationary point: C(t)=((t-.5)^3,0,0), not a repeated-pole defect.
    passed &= checkCurve(
        context,
        dimension,
        3,
        { { -.125, 0, 0, NL_NOW }, { .125, 0, 0, NL_NOW }, { -.125, 0, 0, NL_NOW }, { .125, 0, 0, NL_NOW } },
        { 0, 0, 0, 0, 1, 1, 1, 1 },
        false,
        dimension == 2 ? 11UL : 12UL
    );
    // A fast proof of each linear span must not bypass the existing join checks.
    passed &= checkCurve(
        context,
        dimension,
        1,
        { { 0, 0, 0, NL_NOW }, { 1, 0, 0, NL_NOW }, { 1, 1, 0, NL_NOW } },
        { 0, 0, 1, 2, 2 },
        false,
        dimension == 2 ? 7UL : 8UL
    );
    // These representations deliberately require the original GPA fallback.
    passed &= checkCurve(
        context,
        dimension,
        4,
        { { 0, 0, 0, NL_NOW }, { 1, 0, 0, NL_NOW }, { 2, 0, 0, NL_NOW }, { 3, 0, 0, NL_NOW }, { 4, 0, 0, NL_NOW } },
        { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 },
        true
    );
    passed &= checkCurve(
        context,
        dimension,
        2,
        { { 0, 0, 0, NL_NOW }, { 1, .2, 0, NL_NOW }, { 2, .1, 0, NL_NOW }, { 3, 0, 0, NL_NOW } },
        { 0, 0, 0, .5, 1, 1, 1 },
        true
    );
    if (dimension == 2)
    {
        // A 2D tag does not project stored Z in the NLib evaluator. In XYZ this
        // is (t,0,10000*(t-.5)^2), whose middle radius is 1/20000. An XY-only
        // certificate would incorrectly accept it as a constant-speed line.
        passed &=
            checkCurve(context, 2, 2, { { 0, 0, 2500, NL_NOW }, { .5, 0, -2500, NL_NOW }, { 1, 0, 2500, NL_NOW } }, { 0, 0, 0, 1, 1, 1 }, false, 13);
    }
    return passed;
}

bool checkVariedPoles(SmContext& context, ULONG dimension)
{
    bool passed = true;
    std::uint32_t state = 0xCAFE1234U;
    auto next = [&]()
    {
        state = 1664525U * state + 1013904223U;
        return (state >> 8) / 16777216.0;
    };
    for (int degree = 1; degree <= 3; ++degree)
    {
        for (int trial = 0; trial < 12; ++trial)
        {
            const double scale = trial % 2 ? 1e-3 : 1e3;
            const double offset = trial % 3 ? 0 : 1e6;
            const double domain = trial % 2 ? .25 : 4.0;
            std::vector<gw_CPOINT> poles;
            for (int i = 0; i <= degree; ++i)
            {
                const double weight = .75 + .5 * next();
                const double x = offset + scale * i / degree;
                const double y = scale * .2 * next();
                const double z = dimension == 2 ? NL_NOZ : scale * .2 * next();
                poles.push_back({ x * weight, y * weight, dimension == 2 ? z : z * weight, weight });
            }
            std::vector<double> knots(degree + 1, 0);
            knots.insert(knots.end(), degree + 1, domain);
            passed &= checkCurve(context, dimension, degree, poles, knots, true);
        }
    }
    return passed;
}
} // namespace

int main()
{
    SmContext context;
    bool passed = true;
    for (ULONG dimension : { 2UL, 3UL })
    {
        passed &= checkDerivedEvaluator(context, dimension);
        passed &= checkEllipse(context, dimension, 1.0, 1.0, true);
        passed &= checkEllipse(context, dimension, 0.5, 1.0, true);
        passed &= checkEllipse(context, dimension, 0.009, 1.0, false);
        passed &= checkEllipse(context, dimension, 0.011, 1.0, true);
        // Endpoint radius b*b/a = 1e-6, below the historical ~1e-4 threshold.
        // Its tangent is nonzero, so this isolates the unconsumed curvature result.
        passed &= checkEllipse(context, dimension, 0.001, 1.0, false);
        passed &= checkEllipse(context, dimension, 0.001, 1.0, true, false);
        // Retain the validator's existing parameter-dependent threshold, rather
        // than silently introducing a new quality policy during optimization.
        passed &= checkEllipse(context, dimension, 0.001, 1000.0, true);
        passed &= checkSpans(context, dimension);
        passed &= checkVariedPoles(context, dimension);
    }
#if defined(_M_X64) || defined(__SSE2__)
    const unsigned arithmeticState = _mm_getcsr();
    _mm_setcsr(arithmeticState | (1U << 15) | (1U << 6));
    passed &= checkEllipse(context, 2, 1.0, 1.0, true);
    passed &= checkEllipse(context, 2, 0.001, 1.0, false);
    _mm_setcsr(arithmeticState);
#endif
    return passed ? 0 : 1;
}
