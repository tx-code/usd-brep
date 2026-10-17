// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportTrimMeasurement.h"

#include <SmCurve.h>
#include <SmSurface.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace step_import::detail
{
TrimMeasurement MeasureFinalTrim(const SmCurve& edge, const SmCurve& uv, const SmSurface& surface, double begin, double end)
{
    const auto domain = uv.GetNaturalInterval();
    // SMLib stores trim and edge curves in the same parameter direction/range,
    // independently of edgeuse orientation. Do not silently compare unrelated
    // parameters if the projection/fix stage broke that kernel contract.
    if (!(end > begin) || domain.GetMin() != begin || domain.GetMax() != end)
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
