// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportCircleVertexCorrection.h"

#include "StepImportExecution.h"
#include "StepImportInterval.h"
#include "StepImportVertexProjectionProof.h"

#include <SmAxis2Placement.h>
#include <SmBSplineCurve.h>
#include <SmCircle.h>
#include <SmLine.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace step_import::detail
{
namespace
{
Failure Refusal(const char* key, std::optional<std::size_t> record = {})
{
    return { Status::Indeterminate, key, "Scoped circle vertex correction evidence is unestablished", record, "Repair" };
}
template <class T>
Point P(const T& p)
{
    return { p.x, p.y, p.z };
}
Result<CircleProjectionGeometry> Circle(const SmCircle& circle)
{
    SmAxis2Placement frame;
    double radius = 0;
    if (circle.GetCanonical(frame, radius) != SM_SUCCESS)
    {
        return tl::unexpected(Refusal("CircleVertexCanonicalCircleUnestablished"));
    }
    return CircleProjectionGeometry{ P(frame.GetOrigin()), P(frame.GetZAxis()), radius };
}
struct ProtectedCurve
{
    enum class Kind
    {
        Circle,
        Line,
        Hull
    } kind;
    std::size_t sourceEdge;
    CircleProjectionGeometry circle;
    Point origin, direction;
    Bounds box;
};
Result<std::vector<ProtectedCurve>> Protection(std::span<const CircleVertexCurveView> curves, Execution& execution, Report& report)
{
    std::vector<ProtectedCurve> result;
    result.reserve(curves.size());
    for (const auto& view : curves)
    {
        if (auto work = execution.SpendCylindricalBandProof(); !work)
        {
            return tl::unexpected(std::move(work.error()));
        }
        if (!view.nativeCurve)
        {
            return tl::unexpected(Refusal("CircleVertexMissingDefiningCurve", view.edgeRecord));
        }
        ProtectedCurve item{};
        item.sourceEdge = view.edgeRecord;
        if (const auto circle = dynamic_cast<const SmCircle*>(view.nativeCurve))
        {
            if (auto call = execution.ChargeNative(report, view.edgeRecord); !call)
            {
                return tl::unexpected(std::move(call.error()));
            }
            auto geometry = Circle(*circle);
            if (!geometry)
            {
                return tl::unexpected(std::move(geometry.error()));
            }
            item.kind = ProtectedCurve::Kind::Circle;
            item.circle = *geometry;
        }
        else if (const auto line = dynamic_cast<const SmLine*>(view.nativeCurve))
        {
            // The infinite represented line contains the trimmed edge. Its
            // distance is a conservative lower bound; uncertain clearance is
            // refused rather than expanded from sampled points.
            item.kind = ProtectedCurve::Kind::Line;
            item.origin = P(line->GetLinePoint());
            item.direction = P(line->GetLineVector());
        }
        else if (const auto spline = dynamic_cast<const SmBSplineCurve*>(view.nativeCurve))
        {
            const auto natural = spline->GetNaturalInterval();
            if (view.firstParameter < natural.GetMin() || view.lastParameter > natural.GetMax())
            {
                return tl::unexpected(Refusal("CircleVertexProtectedParameterRange", view.edgeRecord));
            }
            SmTArray<SmPoint3d> poles;
            SmTArray<double> weights;
            if (auto call = execution.ChargeNative(report, view.edgeRecord); !call)
            {
                return tl::unexpected(std::move(call.error()));
            }
            if (spline->GetControlPolygon(poles, weights) != SM_SUCCESS || !poles.GetSize() ||
                (weights.GetSize() && weights.GetSize() != poles.GetSize()))
            {
                return tl::unexpected(Refusal("CircleVertexProtectedHullUnestablished", view.edgeRecord));
            }
            item.kind = ProtectedCurve::Kind::Hull;
            item.box = { Point{ INFINITY, INFINITY, INFINITY }, Point{ -INFINITY, -INFINITY, -INFINITY } };
            for (ULONG i = 0; i < poles.GetSize(); ++i)
            {
                if (auto work = execution.SpendCylindricalBandProof(); !work)
                {
                    return tl::unexpected(std::move(work.error()));
                }
                if (weights.GetSize() && (!(weights[i] > 0) || !std::isfinite(weights[i])))
                {
                    return tl::unexpected(Refusal("CircleVertexProtectedWeightUnestablished", view.edgeRecord));
                }
                const auto p = P(poles[i]);
                for (std::size_t a = 0; a < 3; ++a)
                {
                    if (!std::isfinite(p[a]))
                    {
                        return tl::unexpected(Refusal("CircleVertexProtectedHullUnestablished", view.edgeRecord));
                    }
                    item.box[0][a] = std::min(item.box[0][a], p[a]);
                    item.box[1][a] = std::max(item.box[1][a], p[a]);
                }
            }
        }
        else
        {
            return tl::unexpected(Refusal("CircleVertexProtectedCurveUnestablished", view.edgeRecord));
        }
        result.push_back(std::move(item));
    }
    return result;
}
} // namespace

Result<std::vector<CircleVertexCorrectionEvidence>> CorrectCircleVertices(
    const step_semantics::Snapshot& source,
    const step_semantics::Part::Shape& shape,
    const Options& options,
    std::span<const std::size_t> sourceVertexRecords,
    std::span<Point> nativePointsMm,
    std::span<const CircleVertexCurveView> curves,
    Execution& execution,
    Report& report
)
{
    if (!options.cylindricalBandRepair || !options.cylindricalBandRepair->circleVertices)
    {
        return std::vector<CircleVertexCorrectionEvidence>{};
    }
    const auto& request = *options.cylindricalBandRepair->circleVertices;
    const auto& records = source.Source().Records();
    const auto& roles = source.PartDefinition().roles;
    const auto role = std::find_if(
        roles.begin(),
        roles.end(),
        [](const auto& r)
        {
            return r.name == "definition";
        }
    );
    if (role == roles.end() || sourceVertexRecords.size() != nativePointsMm.size())
    {
        return tl::unexpected(Refusal("CircleVertexSourceScopeUnestablished"));
    }
    const auto& definitionId = records[role->recordIndex].id;
    const auto& shapeId = records[shape.shape].id;
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < request.vertices.size(); ++i)
    {
        if (request.vertices[i].definitionId == definitionId && request.vertices[i].shapeId == shapeId)
        {
            indices.push_back(i);
        }
    }
    if (indices.empty())
    {
        return std::vector<CircleVertexCorrectionEvidence>{};
    }
    if (std::none_of(
            options.cylindricalBandRepair->faces.begin(),
            options.cylindricalBandRepair->faces.end(),
            [&](const auto& f)
            {
                return f.definitionId == definitionId && f.shapeId == shapeId;
            }
        ))
    {
        return tl::unexpected(Refusal("CircleVertexBandPlanScopeRequired"));
    }
    auto protectedCurves = Protection(curves, execution, report);
    if (!protectedCurves)
    {
        return tl::unexpected(std::move(protectedCurves.error()));
    }
    std::vector<CircleVertexCorrectionEvidence> result;
    std::vector<std::size_t> ordinals;
    const auto bindings = source.Bindings();
    // The immutable reader already indexes every incoming reference. Retain
    // that complete use set, including uses outside this body, while indexing
    // only our native candidates once. Never rediscover ownership by scanning
    // the entire file separately for every requested vertex.
    std::unordered_map<std::size_t, std::size_t> vertexOrdinals;
    std::unordered_map<std::size_t, const CircleVertexCurveView*> edgeViews;
    for (std::size_t i = 0; i < sourceVertexRecords.size(); ++i)
    {
        if (auto work = execution.SpendCylindricalBandProof(); !work)
        {
            return tl::unexpected(std::move(work.error()));
        }
        vertexOrdinals.emplace(sourceVertexRecords[i], i);
    }
    for (const auto& curve : curves)
    {
        if (auto work = execution.SpendCylindricalBandProof(); !work)
        {
            return tl::unexpected(std::move(work.error()));
        }
        edgeViews.emplace(curve.edgeRecord, &curve);
    }
    for (const auto index : indices)
    {
        const auto& edit = request.vertices[index];
        if (auto work = execution.SpendCylindricalBandProof(); !work)
        {
            return tl::unexpected(std::move(work.error()));
        }
        const auto vertex = source.Source().Find(edit.vertexId), circleRecord = source.Source().Find(edit.circleId);
        if (!vertex || !circleRecord)
        {
            return tl::unexpected(Refusal("CircleVertexSourceIdentity"));
        }
        const auto vertexIndex = std::size_t(vertex - records.data()), geometryIndex = std::size_t(circleRecord - records.data());
        const auto position = vertexOrdinals.find(vertexIndex);
        if (position == vertexOrdinals.end() || !source.IsA(vertexIndex, "VERTEX_POINT") || !source.IsA(geometryIndex, "CIRCLE"))
        {
            return tl::unexpected(Refusal("CircleVertexSourceScopeUnestablished", vertexIndex));
        }
        const auto ordinal = position->second;
        const CircleVertexCurveView* incident = nullptr;
        for (const auto referenceIndex : source.Source().Incoming(edit.vertexId))
        {
            if (auto work = execution.SpendCylindricalBandProof(); !work)
            {
                return tl::unexpected(std::move(work.error()));
            }
            const auto& reference = source.Source().References()[referenceIndex];
            const auto found = edgeViews.find(reference.recordIndex);
            if (found == edgeViews.end() || found->second->curveRecord != geometryIndex || found->second->firstVertexRecord != vertexIndex ||
                found->second->lastVertexRecord != vertexIndex)
            {
                return tl::unexpected(Refusal("CircleVertexAllIncidentUsesRequired", vertexIndex));
            }
            if (incident && incident != found->second)
            {
                return tl::unexpected(Refusal("CircleVertexSingleSelfLoopRequired", vertexIndex));
            }
            incident = found->second;
        }
        const auto nativeCircle = incident ? dynamic_cast<const SmCircle*>(incident->nativeCurve) : nullptr;
        if (!incident || !nativeCircle)
        {
            return tl::unexpected(Refusal("CircleVertexSingleSelfLoopRequired", vertexIndex));
        }
        if (auto call = execution.ChargeNative(report, vertexIndex); !call)
        {
            return tl::unexpected(std::move(call.error()));
        }
        auto geometry = Circle(*nativeCircle);
        if (!geometry)
        {
            return tl::unexpected(std::move(geometry.error()));
        }
        auto proof = ProveCircleProjection(
            nativePointsMm[ordinal],
            *geometry,
            incident->firstMm,
            request.maximumDisplacementMm,
            options.quality.definitionMm
        );
        if (!proof)
        {
            return tl::unexpected(std::move(proof.error()));
        }
        try
        {
            if (NormUpper(Subtract(Exact(incident->firstMm), Exact(incident->lastMm))) > Enclose(options.quality.definitionMm).low)
            {
                return tl::unexpected(Refusal("CircleVertexClosedEndpointPairUnestablished", vertexIndex));
            }
        }
        catch (const std::overflow_error&)
        {
            return tl::unexpected(Refusal("CircleVertexClosureNumericUnestablished", vertexIndex));
        }
        catch (const std::domain_error&)
        {
            return tl::unexpected(Refusal("CircleVertexClosureNumericUnestablished", vertexIndex));
        }
        double local = geometry->radiusMm;
        for (std::size_t j = 0; j < nativePointsMm.size(); ++j)
        {
            if (j == ordinal)
            {
                continue;
            }
            if (auto work = execution.SpendCylindricalBandProof(); !work)
            {
                return tl::unexpected(std::move(work.error()));
            }
            auto distance = SegmentDistanceLower(nativePointsMm[ordinal], nativePointsMm[j], nativePointsMm[j]);
            if (!distance)
            {
                return tl::unexpected(std::move(distance.error()));
            }
            local = std::min(local, *distance);
        }
        for (const auto& other : *protectedCurves)
        {
            if (other.sourceEdge == incident->edgeRecord)
            {
                continue;
            }
            if (auto work = execution.SpendCylindricalBandProof(); !work)
            {
                return tl::unexpected(std::move(work.error()));
            }
            auto distance = other.kind == ProtectedCurve::Kind::Circle ? CircleDistanceLower(nativePointsMm[ordinal], other.circle) :
                            other.kind == ProtectedCurve::Kind::Line   ? LineDistanceLower(nativePointsMm[ordinal], other.origin, other.direction) :
                                                                         BoxDistanceLower(nativePointsMm[ordinal], other.box);
            if (!distance)
            {
                return tl::unexpected(std::move(distance.error()));
            }
            local = std::min(local, *distance);
        }
        try
        {
            const double limit = Down(local / 100.);
            if (!(local > 0) || proof->displacementUpperMm > limit || proof->originalGapUpperMm > limit ||
                Enclose(options.quality.maximumWorkToleranceMm).high > limit)
            {
                return tl::unexpected(Refusal("CircleVertexLocalProtectionUnestablished", vertexIndex));
            }
        }
        catch (const std::overflow_error&)
        {
            return tl::unexpected(Refusal("CircleVertexProtectionNumericUnestablished", vertexIndex));
        }
        const auto sourcePoint = bindings.Field(vertexIndex, "vertex_geometry").ReferenceTarget();
        result.push_back(
            { edit,
              records[sourcePoint].id,
              nativePointsMm[ordinal],
              incident->firstMm,
              proof->displacementUpperMm,
              proof->projectionErrorUpperMm,
              local,
              proof->originalGapUpperMm }
        );
        ordinals.push_back(ordinal);
    }
    if (auto claimed = execution.ClaimCircleVertexCorrections(indices, result); !claimed)
    {
        return tl::unexpected(std::move(claimed.error()));
    }
    if (auto boundary = execution.Check(Phase::BeforeRepair); !boundary)
    {
        return tl::unexpected(std::move(boundary.error()));
    }
    // No visible/partial model exists. Apply only after the complete body-local
    // proof and request-cumulative budgets pass; raw source is never changed.
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        nativePointsMm[ordinals[i]] = result[i].correctedMm;
    }
    return result;
}
} // namespace step_import::detail
