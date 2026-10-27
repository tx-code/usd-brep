// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportSphereChart.h"

#include "StepImportInterval.h"
#include "StepImportNativeStatus.h"
#include "StepImportProofEnvironment.h"

#include <SmBSplineCurve.h>
#include <gsl/assert>
#include <limits>
namespace step_import::detail
{
Result<std::optional<SphereHemisphereBounds>> BoundSphereChartBoundary(
    std::span<const SphereChartBoundary> boundaries,
    const SourceFrame& frame,
    std::size_t maximumControls,
    const ExecutionCheckpoint& native,
    const std::function<Result<void>(std::size_t)>& work
)
{
    if (boundaries.empty() || !maximumControls || !native || !work || !SurfaceProofNumericEnvironment())
    {
        return std::optional<SphereHemisphereBounds>{};
    }
    SphereHemisphereBounds bounds;
    bounds.negativeSideLowerMm.fill(std::numeric_limits<double>::infinity());
    for (const auto& boundary : boundaries)
    {
        const auto* curve = dynamic_cast<const SmBSplineCurve*>(boundary.curve);
        if (!curve || curve->GetDim() != 3 || !(boundary.interval[1] > boundary.interval[0]))
        {
            return std::optional<SphereHemisphereBounds>{};
        }
        const auto domain = curve->GetNaturalInterval();
        if (boundary.interval[0] < domain.GetMin() || boundary.interval[1] > domain.GetMax())
        {
            return std::optional<SphereHemisphereBounds>{};
        }
        if (auto checked = native(); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        ULONG count = 0;
        double* data = nullptr;
        if (auto checked = CheckNativeStatus(curve->GetControlPointsPointer(count, data), NativeStatusOrigin::GeneralConstruction); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (!count || !data || count > maximumControls - bounds.controls)
        {
            return std::optional<SphereHemisphereBounds>{};
        }
        // An analytic LINE overrides GetNaturalInterval with an infinite
        // domain. Its finite stored control hull is valid only on its B-spline
        // knot domain; it cannot enclose arbitrary line extrapolation.
        if (auto checked = native(); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        SmTArray<double> knots;
        if (auto checked = CheckNativeStatus(curve->GetKnotsAll(knots), NativeStatusOrigin::GeneralConstruction); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        const auto degree = curve->GetDegree();
        if (count <= degree || knots.GetSize() != count + degree + 1 || boundary.interval[0] < knots[degree] || boundary.interval[1] > knots[count])
        {
            return std::optional<SphereHemisphereBounds>{};
        }
        bounds.controls += count;
        for (ULONG i = 0; i < count; ++i)
        {
            if (auto checked = work(6); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            const double weight = curve->IsRational() ? data[4 * i + 3] : 1.;
            if (!(weight > 0) || !std::isfinite(weight))
            {
                return std::optional<SphereHemisphereBounds>{};
            }
            // Homogeneous numerator minus center*W avoids first rounding a
            // Euclidean control. Positive rational weights make the projected
            // scalar function a convex combination of these enclosed distances.
            // Every arithmetic operation rounds outward. Only arithmetic
            // capacity is translated to absence; callbacks/native errors above
            // retain their value/exception identity.
            try
            {
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    Interval dot = Exact(0);
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        dot = dot + (Exact(data[4 * i + k]) - Exact(frame.origin[k]) * Exact(weight)) * Exact(frame.axes[axis][k]);
                    }
                    const auto distance = dot / Exact(weight);
                    bounds.negativeSideLowerMm[2 * axis] = std::min(bounds.negativeSideLowerMm[2 * axis], -distance.high);
                    bounds.negativeSideLowerMm[2 * axis + 1] = std::min(bounds.negativeSideLowerMm[2 * axis + 1], distance.low);
                }
            }
            catch (const std::overflow_error&)
            {
                return std::optional<SphereHemisphereBounds>{};
            }
        }
    }
    return std::optional<SphereHemisphereBounds>{ bounds };
}
SourceFrame SphereChartFrame(const SourceFrame& source, std::size_t signedXAxis)
{
    Expects(signedXAxis < 6);
    const std::size_t x = signedXAxis / 2, z = x == 2 ? 0U : 2U;
    const auto y = 3 - x - z;
    const double xSign = signedXAxis % 2 ? -1. : 1.;
    const double permutationSign = (z == 2 && x == 0) || (z == 0 && x == 1) || (z == 1 && x == 2) ? 1. : -1.;
    SourceFrame result = source;
    for (std::size_t k = 0; k < 3; ++k)
    {
        result.axes[0][k] = xSign * source.axes[x][k];
        result.axes[1][k] = permutationSign * xSign * source.axes[y][k];
        result.axes[2][k] = source.axes[z][k];
    }
    return result;
}
} // namespace step_import::detail
