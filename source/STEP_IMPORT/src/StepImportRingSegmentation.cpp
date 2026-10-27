// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportRingSegmentation.h"

#include "StepImportCylindricalBandRepair.h"
#include "StepImportExecution.h"
#include "StepImportNativeStatus.h"

#include <SmBSplineSurface.h>
#include <SmCircle.h>
#include <SmCrvOnSurf.h>
#include <SmEdge.h>
#include <SmSTEPSurface.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace step_import::detail
{
namespace
{
[[nodiscard]] Result<void> Need(bool okay, const char* key)
{
    if (!okay)
    {
        return tl::unexpected(
            Failure{ Status::Indeterminate, key, "Original-basis circular ring subdivision is unestablished", {}, "RingSubdivision" }
        );
    }
    return {};
}
double ParameterGuard(std::array<double, 2> interval)
{
    // Endpoints which downstream native parameter comparisons cannot reliably
    // distinguish are outside this normalization profile, even for a large
    // physical circle. This is a parameter guard, never a millimetre allowance.
    return SM_EFF_ZERO_PARAM * (1 + std::max(std::abs(interval[0]), std::abs(interval[1])));
}
[[nodiscard]] Result<std::unique_ptr<SmCurve>> CopyBasis(const SmCurve& source, const SmContext& context, const ExecutionCheckpoint& checkpoint)
{
    SmCurve* raw = nullptr;
    if (auto operation = checkpoint(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    const auto status = source.Copy(context, raw);
    if (auto businessCheck = Need(raw != &source, "RingSubdivisionBorrowedBasis"); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    std::unique_ptr<SmCurve> result(raw);
    if (auto businessCheck = Need(status == SM_SUCCESS && result && result->GetContext() == &context, "RingSubdivisionBasisCopy"); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    result->SetOwner(nullptr);
    if (const auto* original = dynamic_cast<const SmCrvOnSurf*>(&source))
    {
        const auto* copied = dynamic_cast<const SmCrvOnSurf*>(result.get());
        const auto* originalCarrier = dynamic_cast<const SmSTEPSurface*>(original->GetBaseSurface());
        const auto* copiedCarrier = copied ? dynamic_cast<const SmSTEPSurface*>(copied->GetBaseSurface()) : nullptr;
        // Each child and the retained reference need independent owned defining
        // UV/carrier trees. Equal values alone must not qualify borrowed aliases
        // which would dangle or double-delete when an unpublished candidate dies.
        if (auto checked = Need(
                copied && original->OwnsBaseSurface() && copied->OwnsBaseSurface() && copied->GetUVCurve() &&
                    copied->GetUVCurve() != original->GetUVCurve() && copied->GetUVCurve()->GetContext() == &context &&
                    copied->GetUVCurve()->GetOwner() == copied && copiedCarrier && originalCarrier && copiedCarrier != originalCarrier &&
                    copiedCarrier->GetContext() == &context && copiedCarrier->GetOwner() == copied && copiedCarrier->OwnsBaseSurface() &&
                    copiedCarrier->GetBaseSurface() && copiedCarrier->GetBaseSurface() != originalCarrier->GetBaseSurface() &&
                    copiedCarrier->GetBaseSurface()->GetContext() == &context,
                "RingSubdivisionComposedOwnership"
            );
            !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
    }
    if (auto businessCheck = RequireSameNativeCurveDefinition(source, *result); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    return result;
}
} // namespace
Result<ClosedSplineCycle> PlanClosedSplineCycle(const SmCurve& curve, double phase, double definitionMm, const ExecutionCheckpoint& checkpoint)
{
    const auto checkCycle = [](bool okay, const char* key) -> Result<void>
    {
        if (!okay)
        {
            return tl::unexpected(Failure{ Status::Indeterminate, key, "Closed spline cycle is unestablished", {}, "ClosedSplineCycle" });
        }
        return {};
    };
    const auto domain = curve.GetNaturalInterval();
    const std::array<double, 2> interval{ domain.GetMin(), domain.GetMax() };
    const auto guard = ParameterGuard(interval);
    if (auto checked = checkCycle(
            std::isfinite(interval[0]) && std::isfinite(interval[1]) && std::isfinite(phase) && std::isfinite(definitionMm) && definitionMm > 0 &&
                phase > interval[0] + guard && phase < interval[1] - guard,
            "ClosedSplinePhaseUnestablished"
        );
        !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    SmPoint3d first, last;
    for (const auto& [parameter, point] : { std::pair{ interval[0], &first }, std::pair{ interval[1], &last } })
    {
        if (auto checked = checkpoint(); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (auto checked = CheckNativeStatus(curve.EvaluatePoint(parameter, *point), NativeStatusOrigin::GeneralConstruction); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
    }
    const double residual = first.DistanceBetween(last);
    if (auto checked = checkCycle(std::isfinite(residual) && residual <= definitionMm, "ClosedSplineClosureUnestablished"); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    return ClosedSplineCycle{ phase, interval, { first.x, first.y, first.z }, residual };
}
[[nodiscard]] Result<std::optional<ConicalRingCut>> PlanConicalRingCut(
    const ConicalRingSupport& support,
    const std::array<ConicalRingInput, 2>& rings,
    double oldAngleRadians,
    double definitionMm,
    double consistencyMm,
    double workMm,
    const ExecutionCheckpoint& checkpoint
)
{
    const auto add = [](Point a, Point b)
    {
        return Point{ a[0] + b[0], a[1] + b[1], a[2] + b[2] };
    };
    const auto sub = [](Point a, Point b)
    {
        return Point{ a[0] - b[0], a[1] - b[1], a[2] - b[2] };
    };
    const auto mul = [](Point a, double b)
    {
        return Point{ a[0] * b, a[1] * b, a[2] * b };
    };
    const auto dot = [](Point a, Point b)
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    const auto length = [](Point a)
    {
        return std::hypot(a[0], a[1], a[2]);
    };
    const auto point = [](const SmPoint3d& a)
    {
        return Point{ a.x, a.y, a.z };
    };
    if (auto checked = Need(
            bool(checkpoint) && std::isfinite(definitionMm) && definitionMm > 0 && std::isfinite(consistencyMm) && consistencyMm > 0 &&
                std::isfinite(workMm) && workMm > 0,
            "ConicalRingCutInput"
        );
        !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    for (const auto& ring : rings)
    {
        if (!ring.circle || ring.circle->GetInsideOut() || ring.circle->GetXRadius() != ring.circle->GetYRadius())
        {
            return std::optional<ConicalRingCut>{};
        }
        const auto& frame = ring.circle->GetPosition();
        const auto axis = frame.GetZAxis();
        const auto delta = sub(point(frame.GetOriginRef()), support.origin);
        const double z = dot(delta, support.zAxis);
        const double alignment = axis.x * support.zAxis[0] + axis.y * support.zAxis[1] + axis.z * support.zAxis[2];
        if (std::abs(std::abs(alignment) - 1) > 1e-12 || length(sub(delta, mul(support.zAxis, z))) > definitionMm)
        {
            return std::optional<ConicalRingCut>{};
        }
    }
    for (const double shift : { std::numbers::pi / 2, std::numbers::pi, -std::numbers::pi / 2 })
    {
        ConicalRingCut cut;
        cut.angleRadians = std::remainder(oldAngleRadians + shift, 2 * std::numbers::pi);
        if (cut.angleRadians > 0)
        {
            cut.angleRadians -= 2 * std::numbers::pi;
        }
        const auto radial = add(mul(support.xAxis, std::cos(cut.angleRadians)), mul(support.yAxis, std::sin(cut.angleRadians)));
        std::array<Point, 2> onCircles;
        bool eligible = true;
        for (std::size_t i = 0; i < rings.size(); ++i)
        {
            const auto& ring = rings[i];
            const auto center = point(ring.circle->GetPosition().GetOriginRef());
            const double z = dot(sub(center, support.origin), support.zAxis), radius = support.radiusMm + support.slope * z;
            if (!std::isfinite(radius) || radius <= SM_EFF_ZERO)
            {
                eligible = false;
                break;
            }
            cut.pointsMm[i] = add(add(support.origin, mul(support.zAxis, z)), mul(radial, radius));
            onCircles[i] = add(center, mul(radial, ring.circle->GetXRadius()));
        }
        if (!eligible)
        {
            continue;
        }
        const auto direction = sub(cut.pointsMm[1], cut.pointsMm[0]);
        const double span = length(direction);
        if (!std::isfinite(span) || span <= workMm)
        {
            continue;
        }
        const auto unit = mul(direction, 1 / span);
        // Keep old source vertices out of the classifier's snap zone. This is
        // a chart-selection restriction, never a larger physical tolerance.
        for (const auto& ring : rings)
        {
            const auto delta = sub(ring.sourceVertex, cut.pointsMm[0]);
            if (length(sub(delta, mul(unit, dot(delta, unit)))) <= 4 * workMm)
            {
                eligible = false;
            }
        }
        if (!eligible)
        {
            continue;
        }
        for (std::size_t i = 0; i < rings.size(); ++i)
        {
            auto located = LocateRingCut(*rings[i].circle, rings[i].interval, onCircles[i], definitionMm, checkpoint);
            if (!located)
            {
                if (located.error().status != Status::Indeterminate)
                {
                    return tl::unexpected(std::move(located.error()));
                }
                eligible = false;
                break;
            }
            cut.parameters[i] = *located;
            SmPoint3d actual;
            if (auto operation = checkpoint(); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (rings[i].circle->EvaluatePoint(*located, actual) != SM_SUCCESS)
            {
                eligible = false;
                break;
            }
            cut.residualsMm[i] = actual.DistanceBetween(SmPoint3d(cut.pointsMm[i][0], cut.pointsMm[i][1], cut.pointsMm[i][2]));
            if (!std::isfinite(cut.residualsMm[i]) || cut.residualsMm[i] > consistencyMm)
            {
                eligible = false;
                break;
            }
        }
        if (eligible)
        {
            return std::optional<ConicalRingCut>(cut);
        }
    }
    return std::optional<ConicalRingCut>{};
}
[[nodiscard]] Result<double> LocateRingCut(
    const SmCurve& circle,
    std::array<double, 2> interval,
    Point point,
    double maximumMm,
    const ExecutionCheckpoint& checkpoint
)
{
    if (auto businessCheck = Need(
            bool(checkpoint) && circle.GetType() == SmCircle_TYPE && std::isfinite(maximumMm) && maximumMm > 0,
            "RingSubdivisionCircleScope"
        );
        !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    const SmExtent1d domain(interval[0], interval[1]);
    const auto natural = circle.GetNaturalInterval();
    if (auto businessCheck = Need(
            domain.GetMin() == natural.GetMin() && domain.GetMax() == natural.GetMax() && domain.GetLength() > 0,
            "RingSubdivisionWholeBasisRequired"
        );
        !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    SmBoolean found = FALSE;
    double parameter = 0, distance = 0;
    if (auto operation = checkpoint(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    const auto status = circle.DropPoint(domain, SmPoint3d(point[0], point[1], point[2]), nullptr, maximumMm, nullptr, found, parameter, distance);
    if (auto businessCheck = Need(
            status == SM_SUCCESS && found && std::isfinite(parameter) && std::isfinite(distance) && distance <= maximumMm,
            "RingSubdivisionParameterUnestablished"
        );
        !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    const auto guard = ParameterGuard(interval);
    if (auto businessCheck = Need(parameter > interval[0] + guard && parameter < interval[1] - guard, "RingSubdivisionNearSourceEndpoint");
        !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    SmPoint3d evaluated;
    if (auto operation = checkpoint(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    if (auto businessCheck = Need(
            circle.EvaluatePoint(parameter, evaluated) == SM_SUCCESS &&
                evaluated.DistanceBetween(SmPoint3d(point[0], point[1], point[2])) <= maximumMm,
            "RingSubdivisionCutPointGap"
        );
        !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    return parameter;
}
[[nodiscard]] Result<void> PrepareRingSegments(
    RingSubdivision& plan,
    const SmCurve& circle,
    const SmContext& context,
    const ExecutionCheckpoint& checkpoint
)
{
    if (auto businessCheck = Need(!plan.cuts.empty() && plan.segments.empty() && !plan.reference, "RingSubdivisionPlanState"); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
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
    auto nativeValue24 = CopyBasis(circle, context, checkpoint);
    if (!nativeValue24)
    {
        return tl::unexpected(std::move(nativeValue24.error()));
    }
    plan.reference = std::move(*nativeValue24);
    for (const auto& cut : plan.cuts)
    {
        if (auto businessCheck = Need(
                std::isfinite(cut.parameter) && cut.parameter > previous + guard && cut.parameter < plan.interval[1] - guard,
                "RingSubdivisionCoincidentCutsUnestablished"
            );
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        auto basis = CopyBasis(circle, context, checkpoint);
        if (!basis)
        {
            return tl::unexpected(std::move(basis.error()));
        }
        plan.segments.push_back({ vertex, cut.vertex, { previous, cut.parameter }, std::move(*basis), nullptr });
        previous = cut.parameter;
        vertex = cut.vertex;
    }
    auto basis = CopyBasis(circle, context, checkpoint);
    if (!basis)
    {
        return tl::unexpected(std::move(basis.error()));
    }
    plan.segments.push_back(
        { vertex, plan.originalEndVertex.value_or(plan.originalVertex), { previous, plan.interval[1] }, std::move(*basis), nullptr }
    );
    return {};
}
[[nodiscard]] Result<void> VerifyRingSegments(const RingSubdivision& plan, const ExecutionCheckpoint& checkpoint)
{
    if (auto businessCheck = Need(plan.reference && plan.segments.size() == plan.cuts.size() + 1, "RingSubdivisionIncompletePlan"); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    double parameter = plan.interval[0];
    std::size_t vertex = plan.originalVertex;
    for (const auto& segment : plan.segments)
    {
        if (auto operation = checkpoint(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto businessCheck = Need(segment.edge && segment.edge->GetCurve() && !segment.curve, "RingSubdivisionUnpublishedSegment");
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        const auto interval = segment.edge->GetInterval();
        if (auto businessCheck = Need(
                segment.interval[0] == parameter && segment.interval[1] > parameter && segment.firstVertex == vertex &&
                    interval.GetMin() == segment.interval[0] && interval.GetMax() == segment.interval[1],
                "RingSubdivisionSourceIntervalCoverage"
            );
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = RequireSameNativeCurveDefinition(*plan.reference, *segment.edge->GetCurve()); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        parameter = segment.interval[1];
        vertex = segment.lastVertex;
    }
    if (auto businessCheck = Need(
            parameter == plan.interval[1] && vertex == plan.originalEndVertex.value_or(plan.originalVertex),
            "RingSubdivisionSourceIntervalCoverage"
        );
        !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    return {};
}
} // namespace step_import::detail
