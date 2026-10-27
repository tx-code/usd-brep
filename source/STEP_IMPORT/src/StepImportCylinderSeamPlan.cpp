// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportCylinderSeamPlan.h"

#include "StepImportCylindricalBandRepair.h"
#include "StepImportNativeStatus.h"

#include <SmBSplineCurve.h>
#include <SmBrep.h>
#include <SmBrepData.h>
#include <SmCircle.h>
#include <SmContext.h>
#include <SmCurve.h>
#include <SmCurveClass.h>
#include <SmCylinder.h>
#include <SmEdge.h>
#include <SmFace.h>
#include <SmTol.h>
#include <SmVertex.h>
#include <algorithm>
#include <cmath>
#include <gsl/assert>
#include <map>
#include <memory>
#include <numbers>

namespace step_import::detail
{
namespace
{
[[nodiscard]] Result<void> Need(bool okay, const char* key, Status status = Status::Indeterminate)
{
    if (!okay)
    {
        return tl::unexpected(Failure{ status, key, "Cylindrical meridian survey could not establish its construction contract", {}, "GeneralBrep" });
    }
    return {};
}
template <class Function>
[[nodiscard]] Result<void> Call(const ExecutionCheckpoint& checkpoint, Function&& function)
{
    if (auto operation = checkpoint(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    // Preserve the shared native return code and failure kind. A survey
    // operation failure is not evidence that the original STEP is invalid.
    return CheckNativeStatus(function(), NativeStatusOrigin::GeneralConstruction);
}
Point PointOf(const SmPoint3d& point)
{
    return { point.x, point.y, point.z };
}
bool Finite(const SmPoint3d& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
} // namespace

[[nodiscard]] Result<std::optional<CylinderSeamPlan>> PlanCylinderSeam(
    const SmCylinder& support,
    std::span<const Point> points,
    std::span<const CylinderSeamCurve> curves,
    const std::array<std::vector<CylinderSeamUse>, 2>& loops,
    const SmContext& context,
    double definitionMm,
    double workMm,
    std::size_t capacity,
    const ExecutionCheckpoint& checkpoint
)
{
    Expects(bool(checkpoint));
    Expects(std::isfinite(definitionMm) && definitionMm > 0 && std::isfinite(workMm) && workMm > 0);
    if (auto checked = Need(
            curves.size() <= capacity && loops[0].size() <= capacity && loops[1].size() <= capacity - loops[0].size(),
            "CylindricalSeamSurveyCapacity",
            Status::ResourceLimit
        );
        !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    std::map<std::size_t, const CylinderSeamCurve*> definitions;
    std::map<std::size_t, std::size_t> loopOf;
    std::size_t controlsAndKnots = 0;
    for (const auto& curve : curves)
    {
        if (auto checked = Need(
                curve.curve && curve.firstVertex < points.size() && curve.lastVertex < points.size() && definitions.emplace(curve.key, &curve).second,
                "CylindricalSeamSurveyInput"
            );
            !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (auto operation = checkpoint(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const auto spline = dynamic_cast<const SmBSplineCurve*>(curve.curve);
        if (!spline || spline->GetDim() != 3)
        {
            return std::optional<CylinderSeamPlan>{};
        }
        const std::size_t count = spline->GetNumberControlPoints();
        const std::size_t knots = count + std::size_t(spline->GetDegree()) + 1;
        if (auto checked = Need(
                count <= capacity - controlsAndKnots && knots <= capacity - controlsAndKnots - count,
                "CylindricalSeamSurveyDefinitionCapacity",
                Status::ResourceLimit
            );
            !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        controlsAndKnots += count + knots;
    }
    std::optional<std::size_t> anchorLoop;
    for (std::size_t i = 0; i < loops.size(); ++i)
    {
        for (const auto& use : loops[i])
        {
            if (auto checked = Need(definitions.contains(use.edge) && loopOf.emplace(use.edge, i).second, "CylindricalSeamSurveyRepeatedUse");
                !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
        }
        if (loops[i].size() == 1)
        {
            const auto& curve = *definitions.at(loops[i][0].edge);
            if (curve.firstVertex == curve.lastVertex && dynamic_cast<const SmCircle*>(curve.curve))
            {
                if (anchorLoop)
                {
                    return std::optional<CylinderSeamPlan>{};
                }
                anchorLoop = i;
            }
        }
    }
    if (!anchorLoop || loops[1 - *anchorLoop].size() < 2)
    {
        return std::optional<CylinderSeamPlan>{};
    }
    if (auto checked = Need(loopOf.size() == definitions.size(), "CylindricalSeamSurveyUnusedInput"); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    const auto& anchor = *definitions.at(loops[*anchorLoop][0].edge);
    const auto& ap = points[anchor.firstVertex];
    const auto& position = support.GetPosition();
    const auto& origin = position.GetOriginRef();
    const auto& x = position.GetXAxisRef();
    const auto& y = position.GetYAxisRef();
    const auto& circle = *dynamic_cast<const SmCircle*>(anchor.curve);
    const auto axis = position.GetZAxis();
    const auto normal = circle.GetPosition().GetZAxis();
    const auto offset = circle.GetPosition().GetOriginRef() - origin;
    const double axial = offset.x * axis.x + offset.y * axis.y + offset.z * axis.z;
    const double parallel = normal.x * axis.x + normal.y * axis.y + normal.z * axis.z;
    const double radialOffset = std::hypot(offset.x - axial * axis.x, offset.y - axial * axis.y, offset.z - axial * axis.z);
    // Eligibility only: an unrelated circular hole must not be reinterpreted
    // as a winding boundary. Complete source/target checks remain the caller's.
    if (circle.GetInsideOut() || std::abs(std::abs(parallel) - 1) > 1e-12 || radialOffset > definitionMm ||
        std::abs(circle.GetXRadius() - support.GetBotRadius()) > definitionMm ||
        std::abs(circle.GetYRadius() - support.GetBotRadius()) > definitionMm)
    {
        return std::optional<CylinderSeamPlan>{};
    }
    const double dx = ap[0] - origin.x, dy = ap[1] - origin.y, dz = ap[2] - origin.z;
    double angle = std::atan2(dx * y.x + dy * y.y + dz * y.z, dx * x.x + dy * x.y + dz * x.z);
    if (angle > 0)
    {
        angle -= 2 * std::numbers::pi;
    }
    const double degrees = angle * 180 / std::numbers::pi;
    if (auto operation = checkpoint(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    SmBrepConstructor builder;
    std::unique_ptr<SmBrep> survey(builder.StartBrep(context));
    if (auto checked = Need(bool(survey), "CylindricalSeamSurveyAllocation"); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    survey->SetTolerance(workMm, FALSE);
    std::map<std::size_t, SmVertex*> vertices;
    for (const auto& curve : curves)
    {
        for (const auto key : { curve.firstVertex, curve.lastVertex })
        {
            if (vertices.contains(key))
            {
                continue;
            }
            if (auto checked = Need(vertices.size() < capacity, "CylindricalSeamSurveyVertexCapacity", Status::ResourceLimit); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            if (auto operation = checkpoint(); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto& p = points[key];
            auto* vertex = builder.StartVertex(SmPoint3d(p[0], p[1], p[2]));
            if (auto checked = Need(vertex != nullptr, "CylindricalSeamSurveyVertex"); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            vertex->SetTolerance(workMm, FALSE);
            vertices.emplace(key, vertex);
            if (auto checked = Call(
                    checkpoint,
                    [&]
                    {
                        return builder.EndVertex();
                    }
                );
                !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
        }
    }
    SmShell* outside = nullptr;
    SmShell* inside = nullptr;
    if (auto checked = Call(
            checkpoint,
            [&]
            {
                return builder.StartShell(outside, inside);
            }
        );
        !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    if (auto operation = checkpoint(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    auto* face = builder.StartFace(SM_OT_SAME);
    if (auto checked = Need(face != nullptr, "CylindricalSeamSurveyFace"); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    face->SetTolerance(workMm, FALSE);
    if (auto operation = checkpoint(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    std::unique_ptr<SmSurface> surface(
        new (context) SmCylinder(origin, x, y, support.GetBotRadius(), degrees, degrees + 360, support.GetHeight(), FALSE, FALSE, FALSE, &context)
    );
    SmSurface* copiedSurface = nullptr;
    auto surfaceCopied = Call(
        checkpoint,
        [&]
        {
            return surface->Copy(context, copiedSurface);
        }
    );
    if (copiedSurface == surface.get())
    {
        return tl::unexpected(
            Failure{ Status::PostcheckFailure, "CylindricalSeamSurveyBorrowedSurface", "Surface copy aliased its source", {}, "GeneralBrep" }
        );
    }
    std::unique_ptr<SmSurface> surfaceReference(copiedSurface);
    if (surfaceReference)
    {
        surfaceReference->SetOwner(nullptr);
    }
    if (!surfaceCopied)
    {
        return tl::unexpected(std::move(surfaceCopied.error()));
    }
    if (auto checked = Need(bool(surfaceReference), "CylindricalSeamSurveySurfaceReference"); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    const auto domain = surface->GetNaturalUVDomain();
    auto adopted = Call(
        checkpoint,
        [&]
        {
            return builder.SetFaceSurface(surface.get(), domain);
        }
    );
    const bool owned = surface->GetOwner() == face;
    if (owned)
    {
        surface.release();
    }
    if (!adopted)
    {
        return tl::unexpected(std::move(adopted.error()));
    }
    if (auto checked = Need(owned, "CylindricalSeamSurveySurfaceOwner"); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    std::map<SmEdge*, const CylinderSeamCurve*> edgeSources;
    for (const auto& loop : loops)
    {
        if (auto operation = checkpoint(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto checked = Need(builder.StartLoop(SM_OT_SAME) != nullptr, "CylindricalSeamSurveyLoop"); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        for (const auto& use : loop)
        {
            const auto& input = *definitions.at(use.edge);
            if (auto operation = checkpoint(); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            auto* edge = builder.StartEdge(
                use.forward ? SM_OT_SAME : SM_OT_OPPOSITE,
                nullptr,
                vertices.at(input.firstVertex),
                vertices.at(input.lastVertex)
            );
            if (auto checked = Need(edge != nullptr, "CylindricalSeamSurveyEdge"); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            edge->SetTolerance(workMm, FALSE);
            SmCurve* raw = nullptr;
            auto copied = Call(
                checkpoint,
                [&]
                {
                    return input.curve->Copy(context, raw);
                }
            );
            if (raw == input.curve)
            {
                return tl::unexpected(
                    Failure{ Status::PostcheckFailure, "CylindricalSeamSurveyBorrowedCurve", "Curve copy aliased its source", {}, "GeneralBrep" }
                );
            }
            std::unique_ptr<SmCurve> curve(raw);
            if (curve)
            {
                curve->SetOwner(nullptr);
            }
            if (!copied)
            {
                return tl::unexpected(std::move(copied.error()));
            }
            if (auto checked = Need(bool(curve), "CylindricalSeamSurveyCurve"); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            if (auto checked = RequireSameNativeCurveDefinition(*input.curve, *curve); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            auto installed = Call(
                checkpoint,
                [&]
                {
                    return builder.SetEdgeCurve(curve.get(), FALSE);
                }
            );
            const bool curveOwned = curve->GetOwner() == edge;
            if (curveOwned)
            {
                curve.release();
            }
            if (!installed)
            {
                return tl::unexpected(std::move(installed.error()));
            }
            if (auto checked = Need(curveOwned, "CylindricalSeamSurveyCurveOwner"); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            if (auto checked = Call(
                    checkpoint,
                    [&]
                    {
                        return builder.SetLimits(SmVertexNode(input.interval[0]), SmVertexNode(input.interval[1]));
                    }
                );
                !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            edgeSources.emplace(edge, &input);
            if (auto checked = Call(
                    checkpoint,
                    [&]
                    {
                        return builder.EndEdge();
                    }
                );
                !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
        }
        if (auto checked = Call(
                checkpoint,
                [&]
                {
                    return builder.EndLoop();
                }
            );
            !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
    }
    if (auto checked = Call(
            checkpoint,
            [&]
            {
                return builder.EndFace();
            }
        );
        !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    if (auto checked = Call(
            checkpoint,
            [&]
            {
                return builder.EndShell(FALSE);
            }
        );
        !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    if (auto checked = Call(
            checkpoint,
            [&]
            {
                return builder.EndBrep();
            }
        );
        !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    SmCurveClassification u, v;
    SmSurfParamType crossed = SM_SP_UNKNOWN, missing = SM_SP_UNKNOWN, near = SM_SP_UNKNOWN;
    if (auto operation = checkpoint(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    // HasSeamProblem can build a trimmed-surface cache and lazily install UV
    // curves. Their native default may enlarge edge/vertex tolerances even when
    // SmCurveClassification disables its own tolerance edits. Use the existing
    // SmBrep validate-only policy for this private survey (SmBrep.h and
    // SmEdgeuse::SetUVTrimCurve); all post-consumer equality checks still run.
    // The scoped flag changes neither the caller's context nor its real BRep.
    SmTemporaryChangeValue<SmBoolean> tolerancePolicy(survey->m_bValidateOnly, TRUE);
    const auto problem = face->HasSeamProblem(u, v, crossed, missing, near);
    if (auto checked = RequireSameNativeSurfaceDefinition(*surfaceReference, *face->GetSurface()); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    // Native classification may materialize caches. It must not turn a copied
    // curve, source vertex or working tolerance into a different survey input.
    // Re-check after the consumer, not only immediately after Copy().
    for (const auto& [edge, input] : edgeSources)
    {
        if (auto checked = RequireSameNativeCurveDefinition(*input->curve, *edge->GetCurve()); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        const auto interval = edge->GetInterval();
        if (auto checked = Need(
                interval.GetMin() == input->interval[0] && interval.GetMax() == input->interval[1] && SmTol::GetZoneTol3d(edge).val == workMm,
                "CylindricalSeamSurveyChangedEdge",
                Status::PostcheckFailure
            );
            !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
    }
    for (const auto& [key, vertex] : vertices)
    {
        const auto p = vertex->GetPoint();
        const auto& expected = points[key];
        if (auto checked = Need(
                p.x == expected[0] && p.y == expected[1] && p.z == expected[2] && SmTol::GetZoneTol3d(vertex).val == workMm,
                "CylindricalSeamSurveyChangedVertex",
                Status::PostcheckFailure
            );
            !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
    }
    if (auto checked = Need(SmTol::GetZoneTol3d(face).val == workMm, "CylindricalSeamSurveyChangedFaceTolerance", Status::PostcheckFailure); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    if (auto checked = Need(u.GetSize() <= capacity && v.GetSize() <= capacity, "CylindricalSeamSurveyIntervalCapacity", Status::ResourceLimit);
        !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    if (!problem || missing != SM_SP_U || near != SM_SP_NEITHER || !u.GetCurve() || v.GetSize() != 0)
    {
        return std::optional<CylinderSeamPlan>{};
    }
    std::optional<CylinderSeamPlan> plan;
    for (ULONG i = 0; i < u.GetSize(); ++i)
    {
        const auto& interval = u[i];
        if (interval.m_vMid.GetPointClass() != SM_PC_FACE)
        {
            continue;
        }
        if (plan)
        {
            return std::optional<CylinderSeamPlan>{};
        }
        const bool anchorFirst = interval.m_vStart.GetVertexObject() == vertices.at(anchor.firstVertex);
        const bool anchorLast = interval.m_vEnd.GetVertexObject() == vertices.at(anchor.firstVertex);
        if (anchorFirst == anchorLast)
        {
            return std::optional<CylinderSeamPlan>{};
        }
        CylinderSeamPlan candidate;
        candidate.anchorEdge = anchor.key;
        candidate.anchorVertex = anchor.firstVertex;
        candidate.angleRadians = angle;
        // SmCurveInterval's endpoints may be snapped to the support window by
        // the classifier's working tolerance (SmPointClass.h, m_dPreSnapParam).
        // Use the original encounter parameters, then re-evaluate in mm. A
        // snapped interval endpoint cannot redefine or move a source vertex.
        const auto& anchorPoint = anchorFirst ? interval.m_vStart : interval.m_vEnd;
        const auto& cutPoint = anchorFirst ? interval.m_vEnd : interval.m_vStart;
        const double anchorParameter = anchorPoint.GetPreSnapParam(), cutParameter = cutPoint.GetPreSnapParam();
        const auto curveDomain = u.GetCurve()->GetNaturalInterval();
        if (!std::isfinite(anchorParameter) || !std::isfinite(cutParameter) || anchorParameter < curveDomain.GetMin() ||
            anchorParameter > curveDomain.GetMax() || cutParameter < curveDomain.GetMin() || cutParameter > curveDomain.GetMax())
        {
            return std::optional<CylinderSeamPlan>{};
        }
        SmPoint3d a, b;
        if (auto checked = Call(
                checkpoint,
                [&]
                {
                    return u.GetCurve()->EvaluatePoint(anchorParameter, a);
                }
            );
            !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (auto checked = Call(
                checkpoint,
                [&]
                {
                    return u.GetCurve()->EvaluatePoint(cutParameter, b);
                }
            );
            !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (!Finite(a) || !Finite(b) || a.DistanceBetween(vertices.at(anchor.firstVertex)->GetPoint()) > definitionMm)
        {
            return std::optional<CylinderSeamPlan>{};
        }
        const auto& point = anchorFirst ? interval.m_vEnd : interval.m_vStart;
        if (point.GetPointClass() == SM_PC_EDGE && edgeSources.contains(point.GetEdgeObject()))
        {
            const auto& input = *edgeSources.at(point.GetEdgeObject());
            if (loopOf.at(input.key) == *anchorLoop || !dynamic_cast<const SmCircle*>(input.curve) || !point.AreParametersSet())
            {
                return std::optional<CylinderSeamPlan>{};
            }
            SmPoint3d actual;
            const double parameter = point.GetTParam();
            const double guard = SM_EFF_ZERO_PARAM * (1 + std::max(std::abs(input.interval[0]), std::abs(input.interval[1])));
            const auto natural = input.curve->GetNaturalInterval();
            if (!std::isfinite(parameter) || parameter <= input.interval[0] + guard || parameter >= input.interval[1] - guard ||
                natural.GetMin() != input.interval[0] || natural.GetMax() != input.interval[1])
            {
                return std::optional<CylinderSeamPlan>{};
            }
            if (auto checked = Call(
                    checkpoint,
                    [&]
                    {
                        return input.curve->EvaluatePoint(point.GetTParam(), actual);
                    }
                );
                !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            if (!Finite(actual) || !std::isfinite(point.GetTParam()) || point.GetTParam() <= input.interval[0] ||
                point.GetTParam() >= input.interval[1] || actual.DistanceBetween(b) > definitionMm)
            {
                return std::optional<CylinderSeamPlan>{};
            }
            candidate.cutEdge = input.key;
            candidate.cutParameter = point.GetTParam();
        }
        else if (point.GetPointClass() == SM_PC_VERTEX)
        {
            std::optional<std::size_t> vertex;
            for (const auto& [key, native] : vertices)
            {
                if (native == point.GetVertexObject())
                {
                    vertex = key;
                }
            }
            if (!vertex || b.DistanceBetween(vertices.at(*vertex)->GetPoint()) > definitionMm)
            {
                return std::optional<CylinderSeamPlan>{};
            }
            const auto incident = std::find_if(
                curves.begin(),
                curves.end(),
                [&](const auto& curve)
                {
                    return loopOf.at(curve.key) != *anchorLoop && (curve.firstVertex == *vertex || curve.lastVertex == *vertex);
                }
            );
            if (incident == curves.end())
            {
                return std::optional<CylinderSeamPlan>{};
            }
            candidate.cutEdge = incident->key;
            candidate.cutVertex = *vertex;
        }
        else
        {
            return std::optional<CylinderSeamPlan>{};
        }
        candidate.first = PointOf(a);
        candidate.last = PointOf(b);
        plan = candidate;
    }
    return plan;
}
} // namespace step_import::detail
