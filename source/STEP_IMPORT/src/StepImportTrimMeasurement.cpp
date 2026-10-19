// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportTrimMeasurement.h"

#include <SmCurve.h>
#include <SmSurface.h>
#include <SmTol.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace step_import::detail
{
TrimMeasurement MeasureFinalTrim(const SmCurve& edge, const SmCurve& uv, const SmSurface& surface, double begin, double end)
{
    const auto domain = uv.GetNaturalInterval();
    const auto edgeDomain = edge.GetNaturalInterval();
    // SmEdgeuse::AssertValid tests 25/26 compare trim/edge parameter endpoints
    // with 100*GetScaledZero at the defining curve's natural endpoints. Match
    // that parameter-roundoff policy, not a millimetre gap budget or an affine
    // reparameterization. Every sample below still evaluates the original same
    // t on both curves and checks the actual lifted 3D gap independently.
    // This finite development measurement does not interpret kernel infinity
    // markers as an enormous allowance for unrelated parameter domains.
    if (!std::isfinite(begin) || !std::isfinite(end) || !(end > begin) || !std::isfinite(domain.GetMin()) || !std::isfinite(domain.GetMax()) ||
        !std::isfinite(edgeDomain.GetMin()) || !std::isfinite(edgeDomain.GetMax()) || !(domain.GetMax() > domain.GetMin()) ||
        !(edgeDomain.GetMax() > edgeDomain.GetMin()) || SM_IS_INFINITE(edgeDomain.GetMin()) || SM_IS_INFINITE(edgeDomain.GetMax()))
    {
        throw std::runtime_error("FinalTrimParameterDomain");
    }
    const double minRoundoff = 100 * SmTol::GetScaledZero(edgeDomain.GetMin());
    const double maxRoundoff = 100 * SmTol::GetScaledZero(edgeDomain.GetMax());
    if (!std::isfinite(minRoundoff) || !std::isfinite(maxRoundoff) || std::abs(domain.GetMin() - begin) > minRoundoff ||
        std::abs(domain.GetMax() - end) > maxRoundoff || domain.GetMin() < edgeDomain.GetMin() - minRoundoff ||
        domain.GetMax() > edgeDomain.GetMax() + maxRoundoff)
    {
        throw std::runtime_error("FinalTrimParameterDomain");
    }
    std::vector<double> breaks{ begin, end };
    for (const auto* curve : { &edge, &uv })
    {
        SmTArray<double> knots;
        if (curve->GetKnots(knots) != SM_SUCCESS)
        {
            throw std::runtime_error("FinalTrimKnots");
        }
        for (ULONG i = 0; i < knots.GetSize(); ++i)
        {
            if (knots[i] > begin && knots[i] < end)
            {
                breaks.push_back(knots[i]);
            }
        }
    }
    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
    TrimMeasurement result;
    for (std::size_t span = 1; span < breaks.size(); ++span)
    {
        for (unsigned i = 0; i <= 8; ++i)
        {
            const auto t = std::lerp(breaks[span - 1], breaks[span], double(i) / 8);
            SmPoint3d point, param, lifted;
            if (edge.EvaluatePoint(t, point) != SM_SUCCESS || uv.EvaluatePoint(t, param) != SM_SUCCESS ||
                surface.EvaluatePoint(SmPoint2d(param.x, param.y), lifted) != SM_SUCCESS)
            {
                throw std::runtime_error("FinalTrimEvaluation");
            }
            const auto gap = point.DistanceBetween(lifted);
            if (!std::isfinite(gap))
            {
                throw std::runtime_error("FinalTrimNonfinite");
            }
            result.maximumMm = std::max(result.maximumMm, gap);
            ++result.samples;
        }
    }
    return result;
}
} // namespace step_import::detail
