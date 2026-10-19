// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmContext.h>
#include <SmSolutionArray.h>
#include <cmath>
#include <cstdio>
#include <nurbs.h>

namespace
{
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
    return (curve.AssertValid(&fullReports, SM_LEVEL_2, SM_NO_WALK) != FALSE) == expectedValid;
}
} // namespace

int main()
{
    SmContext context;
    bool passed = true;
    for (ULONG dimension : { 2UL, 3UL })
    {
        passed &= checkEllipse(context, dimension, 1.0, 1.0, true);
        passed &= checkEllipse(context, dimension, 0.5, 1.0, true);
        // Endpoint radius b*b/a = 1e-6, below the historical ~1e-4 threshold.
        // Its tangent is nonzero, so this isolates the unconsumed curvature result.
        passed &= checkEllipse(context, dimension, 0.001, 1.0, false);
        passed &= checkEllipse(context, dimension, 0.001, 1.0, true, false);
        // Retain the validator's existing parameter-dependent threshold, rather
        // than silently introducing a new quality policy during optimization.
        passed &= checkEllipse(context, dimension, 0.001, 1000.0, true);
    }
    return passed ? 0 : 1;
}
