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
    mutable ULONG deniedDerivativeCalls = 0;
    double deniedLow = 1, deniedHigh = 0; // Empty by default; test numerical refusal only.

    SmStatus Evaluate(double parameter, ULONG derivatives, SmBoolean fromLeft, SmVector3d output[], SmBoolean nonZeroTangents = FALSE) const override
    {
        if (derivatives >= 2)
        {
            ++higherDerivativeCalls;
        }
        if (derivatives >= 3 && parameter > deniedLow && parameter < deniedHigh)
        {
            ++deniedDerivativeCalls;
            return SM_ERR;
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

bool checkInflectionProperty(SmContext& context, ULONG dimension)
{
    // C(t)=(1.5*(t+1),0,0) is straight with infinite radius at every seed.
    // This tests the property seam even when curve validation can skip a line.
    gw_CPOINT linePoints[] = { { 0, 0, 0, NL_NOW }, { 1, 0, 0, NL_NOW }, { 2, 0, 0, NL_NOW }, { 3, 0, 0, NL_NOW } };
    double knots[] = { -1, -1, -1, -1, 1, 1, 1, 1 };
    ForwardingCurve line(dimension, 3, 7, 3, linePoints, knots, &context);
    SmBoolean found = FALSE;
    SmSolution solution;
    const auto
        lineStatus = line.LocalPropertyAnalysis(line.GetNaturalInterval(), SM_CP_MINIMIZE_RADIUS_OF_CURVATURE, 0, nullptr, nullptr, found, solution);
    if (found)
    {
        std::printf("straight dim=%lu status=%d found=%d value=%g\n", dimension, int(lineStatus), int(found), solution.m_vStart.m_dSolutionValue);
        return false;
    }
    // C(t)=(t,t^3,0), -1<=t<=1. Speed never vanishes. At its inflection
    // radius is infinite; a search may instead return an evaluable neighbour.
    // All coefficients and independent truth here are public analytic inputs.
    gw_CPOINT points[] = { { -1, -1, 0, NL_NOW }, { -1. / 3, 1, 0, NL_NOW }, { 1. / 3, -1, 0, NL_NOW }, { 1, 1, 0, NL_NOW } };
    ForwardingCurve curve(dimension, 3, 7, 3, points, knots, &context);
    for (const double seed : { 0., .5 })
    {
        const auto status =
            curve.LocalPropertyAnalysis(curve.GetNaturalInterval(), SM_CP_MINIMIZE_RADIUS_OF_CURVATURE, seed, nullptr, nullptr, found, solution);
        if (status != SM_SUCCESS || (seed == .5 && !found))
        {
            return false;
        }
        if (!found)
        {
            continue;
        }
        const double t = solution.m_vStart[0];
        const double radius = std::pow(1 + 9 * t * t * t * t, 1.5) / std::abs(6 * t);
        if (!std::isfinite(radius) || std::abs(solution.m_vStart.m_dSolutionValue - radius) > 1e-10 * radius)
        {
            std::printf("inflection dim=%lu radius does not match the analytic objective\n", dimension);
            return false;
        }
    }
    SmAssertArray reports;
    return curve.AssertValid(&reports, SM_LEVEL_2, SM_NO_WALK) && reports.GetSize() == 0;
}

bool checkRefusedPropertyEvaluation(SmContext& context, ULONG dimension)
{
    // C(t)=(t,t^2,0). Simulate a numerical evaluator refusal without changing
    // its defining geometry or pretending that failed derivatives are zero.
    // A returned radius must describe the returned parameter and be evaluable.
    ULONG denied = 0;
    for (const double center : { -.5, 0., .5 })
    {
        for (const double width : { .01, .1, .4 })
        {
            for (const double seed : { -.9, -.3, .2, .8 })
            {
                gw_CPOINT points[] = { { -1, 1, 0, NL_NOW }, { 0, -1, 0, NL_NOW }, { 1, 1, 0, NL_NOW } };
                double knots[] = { -1, -1, -1, 1, 1, 1 };
                ForwardingCurve curve(dimension, 2, 5, 2, points, knots, &context);
                curve.deniedLow = center - width;
                curve.deniedHigh = center + width;
                SmBoolean found = FALSE;
                SmSolution solution;
                const auto status = curve.LocalPropertyAnalysis(
                    curve.GetNaturalInterval(),
                    SM_CP_MINIMIZE_RADIUS_OF_CURVATURE,
                    seed,
                    nullptr,
                    nullptr,
                    found,
                    solution
                );
                denied += curve.deniedDerivativeCalls;
                if (status != SM_SUCCESS || !found)
                {
                    continue;
                }
                const double t = solution.m_vStart[0];
                const double radius = std::pow(1 + 4 * t * t, 1.5) / 2;
                if ((t > curve.deniedLow && t < curve.deniedHigh) || std::abs(solution.m_vStart.m_dSolutionValue - radius) > 1e-10 * radius)
                {
                    std::printf(
                        "refused objective dim=%lu center=%g width=%g seed=%g t=%g value=%g expected=%g\n",
                        dimension,
                        center,
                        width,
                        seed,
                        t,
                        solution.m_vStart.m_dSolutionValue,
                        radius
                    );
                    return false;
                }
            }
        }
    }
    return denied > 0;
}

bool checkPropertyValueIdentity(SmContext& context, ULONG dimension)
{
    // Public deterministic monotone-X splines. Property values must belong
    // to their returned parameter, even when Brent's last iterate differs.
    // This does not assert global minimality from finitely many candidates.
    std::uint32_t state = 0x7331U;
    ULONG matched = 0;
    for (unsigned trial = 0; trial < 12; ++trial)
    {
        const unsigned count = 4 + trial;
        std::vector<gw_CPOINT> poles;
        std::vector<double> knots(4, 0);
        for (unsigned i = 0; i < count; ++i)
        {
            state = 1664525U * state + 1013904223U;
            const double random = double(state >> 8) / 16777216;
            poles.push_back({ double(i), .2 * (random - .5), 0, NL_NOW });
        }
        for (unsigned i = 1; i < count - 3; ++i)
        {
            knots.push_back(double(i) / (count - 3));
        }
        knots.insert(knots.end(), 4, 1);
        SmBSplineCurve curve(dimension, count - 1, ULONG(knots.size() - 1), 3, poles.data(), knots.data(), &context);
        const auto verify = [&](const SmSolution& solution)
        {
            SmVector3d derivatives[3];
            if (curve.Evaluate(solution.m_vStart[0], 2, TRUE, derivatives) != SM_SUCCESS)
            {
                return false;
            }
            const double speed = derivatives[1].Length();
            const double radius = speed * speed * speed / (derivatives[1] * derivatives[2]).Length();
            const double given = solution.m_vStart.m_dSolutionValue;
            ++matched;
            return std::isfinite(given) && given >= 0 && std::isfinite(radius) && std::abs(given - radius) <= 1e-8 * (1 + radius);
        };
        for (const double seed : { 0., .01, .1, .3, .5, .8, .99, 1. })
        {
            SmBoolean found = FALSE;
            SmSolution solution;
            if (curve.LocalPropertyAnalysis(curve.GetNaturalInterval(), SM_CP_MINIMIZE_RADIUS_OF_CURVATURE, seed, nullptr, nullptr, found, solution) ==
                    SM_SUCCESS &&
                found && !verify(solution))
            {
                return false;
            }
        }
        SmSolutionArray solutions;
        if (curve.GlobalPropertyAnalysis(curve.GetNaturalInterval(), SM_CP_MINIMIZE_RADIUS_OF_CURVATURE, nullptr, nullptr, .001, solutions) !=
            SM_SUCCESS)
        {
            return false;
        }
        for (ULONG i = 0; i < solutions.GetSize(); ++i)
        {
            if (!verify(solutions[i]))
            {
                return false;
            }
        }
    }
    return matched > 0;
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
        passed &= checkInflectionProperty(context, dimension);
        passed &= checkRefusedPropertyEvaluation(context, dimension);
        passed &= checkPropertyValueIdentity(context, dimension);
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
