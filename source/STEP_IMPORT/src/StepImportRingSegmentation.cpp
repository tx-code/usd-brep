// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportRingSegmentation.h"

#include "StepImportCylindricalBandRepair.h"
#include "StepImportExecution.h"

#include <SmCircle.h>
#include <SmEdge.h>
#include <algorithm>
#include <cmath>

namespace step_import::detail
{
namespace
{
void Need(bool okay, const char* key)
{
    if (!okay)
    {
        throw Failure{ Status::Indeterminate, key, "Original-basis circular ring subdivision is unestablished", {}, "RingSubdivision" };
    }
}
double ParameterGuard(std::array<double, 2> interval)
{
    // Endpoints which downstream native parameter comparisons cannot reliably
    // distinguish are outside this normalization profile, even for a large
    // physical circle. This is a parameter guard, never a millimetre allowance.
    return SM_EFF_ZERO_PARAM * (1 + std::max(std::abs(interval[0]), std::abs(interval[1])));
}
std::unique_ptr<SmCurve> CopyBasis(const SmCurve& source, const SmContext& context, const std::function<void()>& checkpoint)
{
    SmCurve* raw = nullptr;
    checkpoint();
    const auto status = source.Copy(context, raw);
    Need(raw != &source, "RingSubdivisionBorrowedBasis");
    std::unique_ptr<SmCurve> result(raw);
    Need(status == SM_SUCCESS && result && result->GetContext() == &context, "RingSubdivisionBasisCopy");
    result->SetOwner(nullptr);
    RequireSameNativeCurveDefinition(source, *result);
    return result;
}
} // namespace
double LocateRingCut(const SmCurve& circle, std::array<double, 2> interval, Point point, double maximumMm, const std::function<void()>& checkpoint)
{
    Need(bool(checkpoint) && circle.GetType() == SmCircle_TYPE && std::isfinite(maximumMm) && maximumMm > 0, "RingSubdivisionCircleScope");
    const SmExtent1d domain(interval[0], interval[1]);
    const auto natural = circle.GetNaturalInterval();
    Need(domain.GetMin() == natural.GetMin() && domain.GetMax() == natural.GetMax() && domain.GetLength() > 0, "RingSubdivisionWholeBasisRequired");
    SmBoolean found = FALSE;
    double parameter = 0, distance = 0;
    checkpoint();
    const auto status = circle.DropPoint(domain, SmPoint3d(point[0], point[1], point[2]), nullptr, maximumMm, nullptr, found, parameter, distance);
    Need(
        status == SM_SUCCESS && found && std::isfinite(parameter) && std::isfinite(distance) && distance <= maximumMm,
        "RingSubdivisionParameterUnestablished"
    );
    const auto guard = ParameterGuard(interval);
    Need(parameter > interval[0] + guard && parameter < interval[1] - guard, "RingSubdivisionNearSourceEndpoint");
    SmPoint3d evaluated;
    checkpoint();
    Need(
        circle.EvaluatePoint(parameter, evaluated) == SM_SUCCESS && evaluated.DistanceBetween(SmPoint3d(point[0], point[1], point[2])) <= maximumMm,
        "RingSubdivisionCutPointGap"
    );
    return parameter;
}
void PrepareRingSegments(RingSubdivision& plan, const SmCurve& circle, const SmContext& context, const std::function<void()>& checkpoint)
{
    Need(!plan.cuts.empty() && plan.segments.empty() && !plan.reference, "RingSubdivisionPlanState");
    std::sort(
        plan.cuts.begin(),
        plan.cuts.end(),
        [](const auto& a, const auto& b)
        {
            return a.parameter < b.parameter;
        }
    );
    const auto guard = ParameterGuard(plan.interval);
    double previous = plan.interval[0];
    std::size_t vertex = plan.originalVertex;
    plan.reference = CopyBasis(circle, context, checkpoint);
    for (const auto& cut : plan.cuts)
    {
        Need(
            std::isfinite(cut.parameter) && cut.parameter > previous + guard && cut.parameter < plan.interval[1] - guard,
            "RingSubdivisionCoincidentCutsUnestablished"
        );
        plan.segments.push_back({ vertex, cut.vertex, { previous, cut.parameter }, CopyBasis(circle, context, checkpoint), nullptr });
        previous = cut.parameter;
        vertex = cut.vertex;
    }
    plan.segments.push_back({ vertex, plan.originalVertex, { previous, plan.interval[1] }, CopyBasis(circle, context, checkpoint), nullptr });
}
void VerifyRingSegments(const RingSubdivision& plan, const std::function<void()>& checkpoint)
{
    Need(plan.reference && plan.segments.size() == plan.cuts.size() + 1, "RingSubdivisionIncompletePlan");
    double parameter = plan.interval[0];
    std::size_t vertex = plan.originalVertex;
    for (const auto& segment : plan.segments)
    {
        checkpoint();
        Need(segment.edge && segment.edge->GetCurve() && !segment.curve, "RingSubdivisionUnpublishedSegment");
        const auto interval = segment.edge->GetInterval();
        Need(
            segment.interval[0] == parameter && segment.interval[1] > parameter && segment.firstVertex == vertex &&
                interval.GetMin() == segment.interval[0] && interval.GetMax() == segment.interval[1],
            "RingSubdivisionSourceIntervalCoverage"
        );
        RequireSameNativeCurveDefinition(*plan.reference, *segment.edge->GetCurve());
        parameter = segment.interval[1];
        vertex = segment.lastVertex;
    }
    Need(parameter == plan.interval[1] && vertex == plan.originalVertex, "RingSubdivisionSourceIntervalCoverage");
}
} // namespace step_import::detail
