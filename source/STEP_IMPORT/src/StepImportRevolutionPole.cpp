// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportRevolutionPole.h"

#include "StepImportAnalyticLinePole.h"
#include "StepImportNativeStatus.h"

#include <SmBSplineCurve.h>
#include <SmEdge.h>
#include <SmFace.h>
#include <SmLine.h>
#include <SmSurface.h>
#include <SmTol.h>
#include <SmVertex.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace step_import::detail
{
namespace
{
auto Refuse(const char* key, Status status = Status::Indeterminate)
{
    return tl::unexpected(Failure{ status, key, "Native revolution boundary-pole visits are unestablished", {}, "RevolutionPoleRegion" });
}
bool DefiningNurbsType(SM_TYPE type)
{
    // These constructors use the stored finite rational NURBS as their actual
    // ordinary-parameter definition. A generic derived spline may only expose
    // an approximation cache (offset/composed curves in particular): refuse.
    return type == SmBSplineCurve_TYPE || type == SmCircle_TYPE || type == SmEllipse_TYPE;
}
Result<terminal_spline::BinaryNet> Read(SmBSplineCurve& curve, const terminal_spline::Limits& limits)
{
    terminal_spline::BinaryNet net;
    ULONG count = 0;
    double* values = nullptr;
    if (auto good = CheckNativeStatus(curve.GetControlPointsPointer(count, values), NativeStatusOrigin::GeneralConstruction); !good)
    {
        return tl::unexpected(std::move(good.error()));
    }
    if (!values || !count)
    {
        return Refuse("GeneralRevolutionPoleDefinitionMissing");
    }
    if (count > limits.controlSlots)
    {
        return Refuse("GeneralRevolutionPoleControlCapacity", Status::ResourceLimit);
    }
    SmTArray<double> knots;
    if (auto good = CheckNativeStatus(curve.GetKnotsAll(knots), NativeStatusOrigin::GeneralConstruction); !good)
    {
        return tl::unexpected(std::move(good.error()));
    }
    net.basis.degreeU = curve.GetDegree();
    net.basis.countU = count;
    net.basis.knotsU.assign(knots.GetDataArray(), knots.GetDataArray() + knots.GetSize());
    net.homogeneous.reserve(count);
    for (ULONG i = 0; i < count; ++i)
    {
        net.homogeneous.push_back({ values[4 * i], values[4 * i + 1], values[4 * i + 2], curve.IsRational() ? values[4 * i + 3] : 1 });
    }
    return net;
}
} // namespace
Result<RevolutionPoleVisitEvidence> CheckRevolutionPoleVisits(
    SmFace& face,
    std::span<SmVertex* const> sourceVertices,
    const terminal_spline::Limits& limits,
    const terminal_spline::Charge& work,
    const std::function<Result<void>()>& nativeStep
)
{
    if (auto step = nativeStep(); !step)
    {
        return tl::unexpected(std::move(step.error()));
    }
    RevolutionPoleVisitEvidence evidence;
    SmTArray<SmPoint3d> poles;
    ULONG approximate = 0;
    evidence.mask = face.GetSurface()->GetSingularities(&poles, nullptr, &approximate);
    evidence.radiusMm = SmTol::GetZoneTol3d(&face).val;
    if (!evidence.mask || (evidence.mask & ~(SM_SS_VMIN | SM_SS_VMAX)) || approximate != evidence.mask || poles.GetSize() != 4 ||
        !(evidence.radiusMm > 0) || !std::isfinite(evidence.radiusMm))
    {
        return Refuse("GeneralRevolutionPoleChartUnestablished");
    }
    SmTArray<SmEdge*> edges;
    SmTArray<SmVertex*> vertices;
    face.GetEdges(edges);
    face.GetVertices(vertices);
    const std::set<SmVertex*> original(sourceVertices.begin(), sourceVertices.end());
    for (const auto [bit, slot] : { std::pair{ SM_SS_VMIN, 1u }, std::pair{ SM_SS_VMAX, 3u } })
    {
        if (!(evidence.mask & bit))
        {
            continue;
        }
        const auto& point = poles[slot];
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        {
            return Refuse("GeneralRevolutionPolePointUnestablished");
        }
        std::set<SmVertex*> marked;
        for (const auto vertex : vertices)
        {
            // Mirror the native face-zone marker observation, but require an
            // original source-owned vertex. A generated/unrelated marker may
            // not authorize an interior visit or a different edge endpoint.
            if (original.contains(vertex) && vertex->GetPoint().DistanceBetween(point) < evidence.radiusMm)
            {
                marked.insert(vertex);
            }
        }
        if (marked.empty())
        {
            return Refuse("GeneralRevolutionPoleOriginalMarkerRequired", Status::Deferred);
        }
        std::set<const SmEdge*> seen;
        for (const auto edge : edges)
        {
            if (!seen.insert(edge).second)
            {
                continue;
            }
            auto* curve = edge->GetCurve();
            if (!curve || curve->GetDim() != 3 || (!DefiningNurbsType(curve->GetType()) && curve->GetType() != SmLine_TYPE))
            {
                return Refuse("GeneralRevolutionPoleCurveProofUnsupported", Status::Deferred);
            }
            SmVertex* first = nullptr;
            SmVertex* last = nullptr;
            edge->GetVertices(first, last);
            const auto interval = edge->GetInterval();
            terminal_spline::PoleVisits visits;
            visits.interval = { interval.GetMin(), interval.GetMax() };
            visits.poleMm = { point.x, point.y, point.z };
            visits.radiusMm = evidence.radiusMm;
            visits.firstAllowed = marked.contains(first);
            visits.lastAllowed = marked.contains(last);
            const auto prove = [&]() -> Result<terminal_spline::PoleVisitProof>
            {
                if (curve->GetType() == SmLine_TYPE)
                {
                    const auto& line = *static_cast<const SmLine*>(curve);
                    if (!line.IsBounded())
                    {
                        return Refuse("GeneralRevolutionPoleUnboundedLineUnsupported", Status::Deferred);
                    }
                    // SmLine::EvaluatePoint evaluates its analytic fields, not
                    // the rounded endpoint NURBS made by SmLine::MakeNurb.
                    // Preserve the exact ordinary->STEP affine definition and
                    // its explicit tiny-domain identity branch, including sense.
                    terminal_spline::AffineLineDefinition definition;
                    const auto& origin = line.GetLinePoint();
                    const auto& direction = line.GetLineVector();
                    const auto natural = line.GetNaturalInterval(), step = line.GetSTEPInterval();
                    definition.originMm = { origin.x, origin.y, origin.z };
                    definition.direction = { direction.x, direction.y, direction.z };
                    definition.scale = line.GetLineScale();
                    definition.naturalInterval = { natural.GetMin(), natural.GetMax() };
                    definition.stepAtNaturalEnds = line.GetInsideOut() ? std::array{ step.GetMax(), step.GetMin() } :
                                                                         std::array{ step.GetMin(), step.GetMax() };
                    definition.directParameter = SM_IS_ZERO(natural.GetLength());
                    return terminal_spline::CheckLinePoleVisits(definition, visits, limits, work);
                }
                auto net = Read(*static_cast<SmBSplineCurve*>(curve), limits);
                if (!net)
                {
                    return tl::unexpected(std::move(net.error()));
                }
                return terminal_spline::CheckPoleVisits(*net, visits, limits, work);
            };
            auto proof = prove();
            if (!proof)
            {
                return tl::unexpected(std::move(proof.error()));
            }
            if (!proof->established)
            {
                return Refuse("GeneralRevolutionPoleCrossingUnestablished");
            }
            ++evidence.checkedEdges;
            evidence.patches += proof->patches;
        }
    }
    return evidence;
}
} // namespace step_import::detail
