// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPeriodicNormalization.h"

#include "StepImportExecution.h"
#include "StepImportInterval.h"
#include "StepImportNativeData.h"
#include "StepImportSurfaceProof.h"

#include <SmAttribute.h>
#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmCurveClass.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <SmLoop.h>
#include <SmVertex.h>
#include <SmVertexuse.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>

namespace step_import::detail
{
namespace
{
constexpr ULONG FaceLineage = SM_AI_USER_3 + 413, EdgeLineage = SM_AI_USER_3 + 414;
struct SavedUv
{
    SmEdgeuse* owner;
    std::unique_ptr<SmCurve> curve;
};
struct OriginalEdge
{
    SmEdge* edge;
    std::unique_ptr<SmCurve> basis;
    SmExtent1d interval;
    std::optional<std::size_t> source;
    std::size_t owner;
};
class Normalizer
{
public:

    Normalizer(
        ModelData& model,
        std::span<const std::size_t> faces,
        std::span<const std::size_t> edges,
        std::span<const std::size_t> vertices,
        std::span<const std::size_t> derived,
        const Options& options,
        Report& report,
        const std::function<void(std::size_t)>& step
    )
        : m(model), faceRecords(faces), edgeRecords(edges), vertexRecords(vertices), derivedOwners(derived), q(options), report(report), charge(step)
    {
    }
    void Run()
    {
        const auto originalFaces = m.faces;
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < faceRecords.size(); ++i)
        {
            const auto surface = Ref(faceRecords[i], "face_geometry");
            if (!Is(surface, "CYLINDRICAL_SURFACE") && !Is(surface, "CONICAL_SURFACE"))
            {
                continue;
            }
            SmCurveClassification u, v;
            SmSurfParamType crossed, missing, near;
            Step(faceRecords[i]);
            originalFaces[i].face->HasSeamProblem(u, v, crossed, missing, near);
            if (near != SM_SP_U)
            {
                continue;
            }
            const auto bounds = source().Field(faceRecords[i], "bounds").ReferenceTargets();
            Need(bounds.size() == 1 && Is(bounds[0], "FACE_OUTER_BOUND"), "PeriodicNormalizationRegionUnsupported", faceRecords[i]);
            candidates.push_back(i);
        }
        if (candidates.empty())
        {
            return; // Explicit request was unused; do not invent normalization evidence.
        }
        // Interacting cuts need composition evidence. Do not use a later source
        // face pointer that an earlier native split may have replaced.
        Need(candidates.size() == 1, "PeriodicNormalizationMultipleCandidatesUnsupported", faceRecords[candidates.front()], Status::Unsupported);
        SmTArray<SmEdge*> beforeEdges;
        SmTArray<SmVertex*> beforeVertices;
        m.brep->GetEdges(beforeEdges);
        m.brep->GetVertices(beforeVertices);
        const auto cap = q.sourceLimits.values;
        Need(
            beforeEdges.GetSize() <= cap && beforeVertices.GetSize() <= cap && originalFaces.size() <= cap,
            "PeriodicNormalizationInventoryLimit",
            faceRecords[candidates.front()],
            Status::ResourceLimit
        );
        Need(
            beforeEdges.GetSize() < static_cast<unsigned long>(std::numeric_limits<long>::max()) &&
                originalFaces.size() < static_cast<std::size_t>(std::numeric_limits<long>::max()),
            "PeriodicNormalizationLineageLimit",
            faceRecords.front(),
            Status::ResourceLimit
        );
        std::vector<Point> sourcePoints;
        for (const auto vertex : m.vertices)
        {
            sourcePoints.push_back(PointOf(vertex));
        }
        std::size_t surfaceControls = 0;
        for (std::size_t i = 0; i < originalFaces.size(); ++i)
        {
            // Capture BEFORE merging, not merely before cropping. A successful
            // native operation does not prove preservation of its input surface.
            const auto surface = dynamic_cast<const SmBSplineSurface*>(originalFaces[i].face->GetSurface());
            ULONG nu = 0, nv = 0;
            double* net = nullptr;
            Need(
                surface && surface->GetControlPointsPointer(nu, nv, net) == SM_SUCCESS && net && nu && nv,
                "PeriodicNormalizationSurfaceType",
                faceRecords[i],
                Status::Unsupported
            );
            Need(
                nu <= cap && nv <= cap / nu && std::size_t(nu) * nv <= cap - std::min(surfaceControls, cap),
                "PeriodicNormalizationControlLimit",
                faceRecords[i],
                Status::ResourceLimit
            );
            surfaceControls += std::size_t(nu) * nv;
            SmSurface* raw = nullptr;
            Step(faceRecords[i]);
            const auto copied = surface->SmBSplineSurface::Copy(*m.context, raw);
            std::unique_ptr<SmSurface> saved(raw);
            Good(copied, faceRecords[i]);
            Need(raw && raw != surface, "PeriodicNormalizationSurfaceAlias", faceRecords[i]);
            raw->SetOwner(nullptr);
            sourceSurfaces.push_back(std::move(saved));
            Need(!originalFaces[i].face->FindAttribute(FaceLineage), "PeriodicNormalizationAttributeCollision", faceRecords[i]);
            Step(faceRecords[i]);
            originalFaces[i].face->AddAttribute(new (*m.context) SmLongAttribute(FaceLineage, static_cast<long>(i + 1), SM_AB_COPY));
        }
        std::vector<OriginalEdge> bases;
        std::size_t basisControls = 0;
        std::map<SmEdge*, std::size_t> originalEdgeIndices;
        for (std::size_t i = 0; i < m.edges.size(); ++i)
        {
            originalEdgeIndices.emplace(m.edges[i], i);
        }
        for (const auto edge : beforeEdges)
        {
            const auto at = originalEdgeIndices.find(edge);
            Need(at != originalEdgeIndices.end(), "PeriodicNormalizationUnmappedEdge", faceRecords.front());
            const auto index = at->second;
            const auto owner = index < edgeRecords.size() ? edgeRecords[index] : derivedOwners[index - edgeRecords.size()];
            const auto curve = dynamic_cast<const SmBSplineCurve*>(edge->GetCurve());
            Need(curve && !edge->FindAttribute(EdgeLineage), "PeriodicNormalizationBasisUnsupported", owner, Status::Unsupported);
            ULONG count = 0;
            double* net = nullptr;
            Need(curve->GetControlPointsPointer(count, net) == SM_SUCCESS && net, "PeriodicNormalizationBasisUnsupported", owner, Status::Unsupported);
            Need(count <= cap - std::min(basisControls, cap), "PeriodicNormalizationControlLimit", owner, Status::ResourceLimit);
            basisControls += count; // All retained original bases share one request-local memory cap.
            SmCurve* raw = nullptr;
            Step(owner);
            Good(curve->Copy(*m.context, raw), owner);
            std::unique_ptr<SmCurve> owned(raw);
            Need(raw && raw != curve, "PeriodicNormalizationBasisAlias", owner);
            Need(SameDefinition(*raw, *curve), "PeriodicNormalizationBasisCopyChanged", owner, Status::Indeterminate);
            raw->SetOwner(nullptr);
            bases.push_back(
                { edge,
                  std::move(owned),
                  edge->GetInterval(),
                  index < edgeRecords.size() ? std::optional<std::size_t>(edgeRecords[index]) : std::nullopt,
                  owner }
            );
            Step(owner);
            edge->AddAttribute(new (*m.context) SmLongAttribute(EdgeLineage, static_cast<long>(bases.size()), SM_AB_COPY));
        }
        std::map<SmEdge*, std::size_t> cutOwners;
        for (const auto index : candidates)
        {
            auto face = originalFaces[index].face;
            const auto record = faceRecords[index];
            const auto domain = face->GetSurface()->GetNaturalUVDomain();
            SmBSplineCurve* cutRaw = nullptr;
            double extractionGap = 0;
            Step(record);
            Good(
                face->GetSurface()->CreateIsoParametricCurve(
                    *m.context,
                    SM_SP_U,
                    std::midpoint(domain.GetMin().x, domain.GetMax().x),
                    Control(q.quality.definitionMm),
                    cutRaw,
                    &domain,
                    &extractionGap
                ),
                record
            );
            std::unique_ptr<SmBSplineCurve> cut(cutRaw);
            Need(
                cut && std::isfinite(extractionGap) && extractionGap <= Control(q.quality.definitionMm),
                "PeriodicNormalizationCutDefinitionUnestablished",
                record,
                Status::Indeterminate
            );
            SmTArray<SmCurve*> curves;
            curves.Add(cut.get());
            SmTArray<SmFace*> createdFaces;
            SmTArray<SmEdge*> createdEdges;
            {
                // Native merge requires edit state; inputs remain caller-owned.
                // The entire candidate/context is discarded on ANY later failure.
                SmTemporaryChangeValue<SmBoolean> editing(m.brep->m_bEditingEnabled, TRUE);
                Step(record);
                Good(
                    m.brep->MergeCurvesOnSurface(
                        *face->GetSurface(),
                        Control(q.quality.initialWorkToleranceMm),
                        curves,
                        nullptr,
                        createdFaces,
                        createdEdges,
                        FALSE
                    ),
                    record
                );
            }
            Need(createdFaces.GetSize() <= cap && createdEdges.GetSize() <= cap, "PeriodicNormalizationInventoryLimit", record, Status::ResourceLimit);
            for (const auto edge : createdEdges)
            {
                if (!edge->FindAttribute(EdgeLineage))
                {
                    cutOwners.emplace(edge, record);
                }
            }
            Crop(*face, record);
            for (const auto child : createdFaces)
            {
                Crop(*child, record);
            }
        }
        SmTArray<SmFace*> allFaces;
        SmTArray<SmEdge*> allEdges;
        SmTArray<SmVertex*> allVertices;
        m.brep->GetFaces(allFaces);
        m.brep->GetEdges(allEdges);
        m.brep->GetVertices(allVertices);
        Need(
            allFaces.GetSize() <= cap && allEdges.GetSize() <= cap && allVertices.GetSize() <= cap,
            "PeriodicNormalizationInventoryLimit",
            faceRecords.front(),
            Status::ResourceLimit
        );
        std::set<SmVertex*> retainedVertices;
        for (const auto vertex : allVertices)
        {
            retainedVertices.insert(vertex);
        }
        for (std::size_t i = 0; i < m.vertices.size(); ++i)
        {
            Need(
                retainedVertices.contains(m.vertices[i]) && PointOf(m.vertices[i]) == sourcePoints[i],
                "PeriodicNormalizationSourceVertexChanged",
                vertexRecords[i]
            );
        }
        TopologyNormalizationInfo info;
        std::map<long, std::vector<std::pair<double, double>>> coverage;
        std::map<SmEdge*, std::size_t> targetEdgeIndex;
        std::size_t targetControls = 0;
        for (ULONG i = 0; i < allEdges.GetSize(); ++i)
        {
            auto edge = allEdges[i];
            targetEdgeIndex.emplace(edge, i);
            const auto attribute = dynamic_cast<SmLongAttribute*>(edge->FindAttribute(EdgeLineage));
            TargetEdgeOrigin origin;
            origin.target = i;
            const auto interval = edge->GetInterval();
            origin.basisParameterInterval = { interval.GetMin(), interval.GetMax() };
            if (attribute)
            {
                const auto tag = attribute->GetValue();
                Need(tag > 0 && static_cast<std::size_t>(tag) <= bases.size(), "PeriodicNormalizationLineageInvalid", faceRecords.front());
                auto& original = bases[static_cast<std::size_t>(tag - 1)];
                CountControls(*original.basis, targetControls, original.owner);
                Need(interval.IsContainedBy(original.basis->GetNaturalInterval(), 0.), "PeriodicNormalizationParameterRange", original.owner);
                coverage[tag].emplace_back(interval.GetMin(), interval.GetMax());
                if (original.source)
                {
                    origin.sourceEdgeId = Id(*original.source);
                }
                else
                {
                    origin.derivedFromFaceId = Id(original.owner);
                }
                // Kernel trimming can alter a primitive's NURBS representation.
                // Retain the ORIGINAL complete basis with the child interval;
                // do not fit it or relabel the altered primitive as correct.
                // Always copy the original analytic class as well as its NURBS
                // basis. Equal nets alone cannot prove an edited primitive's
                // analytic evaluator or its cached parameter map is unchanged.
                {
                    auto saved = SaveEdgeUv(*edge, original.owner);
                    SmCurve* raw = nullptr;
                    Step(original.owner);
                    Good(original.basis->Copy(*m.context, raw), original.owner);
                    std::unique_ptr<SmCurve> owned(raw);
                    Need(raw && raw != original.basis.get(), "PeriodicNormalizationBasisAlias", original.owner);
                    raw->SetOwner(nullptr);
                    Step(original.owner);
                    edge->SetCurve(raw, TRUE);
                    raw->SetOwner(edge);
                    owned.release();
                    Restore(saved, original.owner);
                }
                Need(SameDefinition(*edge->GetCurve(), *original.basis), "PeriodicNormalizationSourceBasisChanged", original.owner);
                m.targetEdgeOwners.push_back(original.owner);
            }
            else
            {
                const auto found = cutOwners.find(edge);
                Need(found != cutOwners.end(), "PeriodicNormalizationUnmappedDerivedEdge", faceRecords.front());
                CountControls(*edge->GetCurve(), targetControls, found->second);
                origin.derivedFromFaceId = Id(found->second);
                m.targetEdgeOwners.push_back(found->second);
            }
            info.edges.push_back(std::move(origin));
            m.targetEdges.push_back(edge);
        }
        for (std::size_t i = 0; i < bases.size(); ++i)
        {
            auto ranges = coverage[static_cast<long>(i + 1)];
            std::sort(ranges.begin(), ranges.end());
            double end = bases[i].interval.GetMin();
            Need(!ranges.empty(), "PeriodicNormalizationSourceEdgeMissing", bases[i].owner);
            for (const auto& range : ranges)
            {
                Need(range.first == end && range.second > range.first, "PeriodicNormalizationSourceCoverage", bases[i].owner);
                end = range.second;
            }
            Need(end == bases[i].interval.GetMax(), "PeriodicNormalizationSourceCoverage", bases[i].owner);
        }
        std::vector<NativeFaceData> ordered;
        std::vector<std::vector<SmFace*>> children(originalFaces.size());
        for (const auto face : allFaces)
        {
            const auto tag = dynamic_cast<SmLongAttribute*>(face->FindAttribute(FaceLineage));
            Need(
                tag && tag->GetValue() > 0 && static_cast<std::size_t>(tag->GetValue()) <= children.size(),
                "PeriodicNormalizationFaceLineageMissing",
                faceRecords.front()
            );
            children[static_cast<std::size_t>(tag->GetValue() - 1)].push_back(face);
        }
        // Stable target face order groups all children by original source face.
        for (std::size_t sourceIndex = 0; sourceIndex < originalFaces.size(); ++sourceIndex)
        {
            const auto countBefore = ordered.size();
            for (const auto face : children[sourceIndex])
            {
                NativeFaceData target;
                target.face = face;
                target.faceId = Id(faceRecords[sourceIndex]);
                SmTArray<SmLoop*> loops;
                face->GetLoops(loops);
                for (const auto loop : loops)
                {
                    target.loops.push_back(loop);
                }
                const auto domain = face->GetSurface()->GetNaturalUVDomain();
                TargetFaceOrigin origin;
                origin.target = ordered.size();
                origin.sourceFaceId = target.faceId;
                origin.sourceSurfaceId = Id(Ref(faceRecords[sourceIndex], "face_geometry"));
                origin.nativeDomain = { domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y };
                const auto original = dynamic_cast<const SmBSplineSurface*>(sourceSurfaces[sourceIndex].get());
                const auto current = dynamic_cast<const SmBSplineSurface*>(face->GetSurface());
                Need(original && current, "PeriodicNormalizationSurfaceType", faceRecords[sourceIndex], Status::Unsupported);
                const auto beforeDomain = original->GetNaturalUVDomain();
                origin.originalNativeDomain = { beforeDomain.GetMin().x, beforeDomain.GetMin().y, beforeDomain.GetMax().x, beforeDomain.GetMax().y };
                const auto proof = ProveSurfaceRestriction(
                    *original,
                    *current,
                    Control(q.quality.definitionMm),
                    cap,
                    cap,
                    [&]
                    {
                        Step(faceRecords[sourceIndex]);
                    }
                );
                Need(
                    proof.established && proof.maximumMm.has_value(),
                    "PeriodicNormalizationSourceSurfaceUnestablished",
                    faceRecords[sourceIndex],
                    proof.workLimit ? Status::ResourceLimit : Status::Indeterminate
                );
                origin.nativeSurfaceDifferenceBoundMm = *proof.maximumMm;
                info.faces.push_back(std::move(origin));
                ordered.push_back(std::move(target));
                m.targetFaceSources.push_back(sourceIndex);
            }
            Need(ordered.size() > countBefore, "PeriodicNormalizationSourceFaceMissing", faceRecords[sourceIndex]);
        }
        Need(ordered.size() == allFaces.GetSize(), "PeriodicNormalizationFaceInventory", faceRecords.front());
        std::map<SmVertex*, std::size_t> originalVertexIndices;
        for (std::size_t i = 0; i < m.vertices.size(); ++i)
        {
            originalVertexIndices.emplace(m.vertices[i], i);
        }
        for (ULONG i = 0; i < allVertices.GetSize(); ++i)
        {
            TargetVertexOrigin origin;
            origin.target = i;
            const auto found = originalVertexIndices.find(allVertices[i]);
            std::size_t owner = faceRecords.front();
            if (found != originalVertexIndices.end())
            {
                const auto sourceIndex = found->second;
                owner = vertexRecords[sourceIndex];
                origin.sourceVertexId = Id(owner);
            }
            else
            {
                SmTArray<SmEdge*> incident;
                allVertices[i]->GetEdges(incident);
                for (const auto edge : incident)
                {
                    const auto index = targetEdgeIndex.at(edge);
                    const auto& edgeOrigin = info.edges[index];
                    if (!edgeOrigin.sourceEdgeId)
                    {
                        continue;
                    }
                    const auto primary = edge->GetPrimaryEdgeuse();
                    const bool atPrimary = primary->GetVertexuse()->GetVertex() == allVertices[i];
                    const bool minimum = atPrimary == (primary->GetOrientation() == SM_OT_SAME);
                    const double parameter = edgeOrigin.basisParameterInterval[minimum ? 0 : 1];
                    owner = m.targetEdgeOwners[index];
                    SmPoint3d onBasis;
                    Step(owner);
                    Good(edge->GetCurve()->EvaluatePoint(parameter, onBasis), owner);
                    const double residual = onBasis.DistanceBetween(allVertices[i]->GetPoint());
                    Need(
                        std::isfinite(residual) && residual <= Control(q.quality.consistencyMm),
                        "PeriodicNormalizationDerivedVertexResidual",
                        owner,
                        Status::Indeterminate
                    );
                    origin.basisEdgeParameters.emplace_back(*edgeOrigin.sourceEdgeId, parameter);
                }
                Need(!origin.basisEdgeParameters.empty(), "PeriodicNormalizationDerivedVertexUnmapped", owner);
            }
            info.vertices.push_back(std::move(origin));
            m.targetVertexOwners.push_back(owner);
            m.targetVertices.push_back(allVertices[i]);
        }
        // Temporary lineage never becomes application data or native-file data.
        for (const auto face : allFaces)
        {
            if (const auto tag = face->FindAttribute(FaceLineage))
            {
                face->RemoveAttribute(tag);
            }
        }
        for (const auto edge : allEdges)
        {
            if (const auto tag = edge->FindAttribute(EdgeLineage))
            {
                edge->RemoveAttribute(tag);
            }
        }
        m.faces = std::move(ordered);
        info.sourceVerticesPreserved = info.sourceCurveIntervalsCovered = info.completed = true;
        m.topologyNormalization = info;
        report.topologyNormalization = std::move(info);
    }

private:

    ModelData& m;
    std::span<const std::size_t> faceRecords, edgeRecords, vertexRecords, derivedOwners;
    const Options& q;
    Report& report;
    const std::function<void(std::size_t)>& charge;
    std::vector<std::unique_ptr<SmSurface>> sourceSurfaces;
    const step_semantics::Snapshot& source() const
    {
        return *m.source;
    }
    std::string Id(std::size_t record) const
    {
        return source().Source().Records().at(record).id;
    }
    bool Is(std::size_t record, const char* type) const
    {
        return source().IsA(record, type);
    }
    std::size_t Ref(std::size_t record, const char* field) const
    {
        return source().Field(record, field).ReferenceTarget();
    }
    static Point PointOf(const SmVertex* vertex)
    {
        const auto& p = vertex->GetPoint();
        return { p.x, p.y, p.z };
    }
    static double Control(Fraction value)
    {
        return Enclose(value).low;
    } // Never round an acceptance budget upward.
    void Step(std::size_t record) const
    {
        charge(record);
    }
    void Need(bool okay, const char* key, std::size_t record, Status status = Status::PostcheckFailure) const
    {
        if (!okay)
        {
            throw Failure{ status, key, "Periodic target normalization evidence is unestablished", record, "PeriodicNormalization" };
        }
    }
    void Good(SmStatus status, std::size_t record) const
    {
        Need(status == SM_SUCCESS, "PeriodicNormalizationNativeFailure", record, Status::BuildFailure);
    }
    void CountControls(const SmCurve& curve, std::size_t& used, std::size_t record) const
    {
        ULONG count = 0;
        double* net = nullptr;
        const auto spline = dynamic_cast<const SmBSplineCurve*>(&curve);
        Need(
            spline && spline->GetControlPointsPointer(count, net) == SM_SUCCESS && net,
            "PeriodicNormalizationBasisUnsupported",
            record,
            Status::Unsupported
        );
        const auto cap = q.sourceLimits.values;
        Need(count <= cap - std::min(used, cap), "PeriodicNormalizationControlLimit", record, Status::ResourceLimit);
        used += count;
    }
    bool SameDefinition(const SmCurve& first, const SmCurve& second) const
    {
        const auto a = dynamic_cast<const SmBSplineCurve*>(&first), b = dynamic_cast<const SmBSplineCurve*>(&second);
        if (!a || !b || a->GetType() != b->GetType() || a->GetDim() != b->GetDim() || a->GetDegree() != b->GetDegree())
        {
            return false;
        }
        SmTArray<SmPoint3d> pa, pb;
        SmTArray<double> wa, wb, ka, kb;
        if (a->GetControlPolygon(pa, wa) != SM_SUCCESS || b->GetControlPolygon(pb, wb) != SM_SUCCESS || a->GetKnotsAll(ka) != SM_SUCCESS ||
            b->GetKnotsAll(kb) != SM_SUCCESS || pa.GetSize() != pb.GetSize() || wa.GetSize() != wb.GetSize() || ka.GetSize() != kb.GetSize())
        {
            return false;
        }
        for (ULONG i = 0; i < pa.GetSize(); ++i)
        {
            if (pa[i].x != pb[i].x || pa[i].y != pb[i].y || pa[i].z != pb[i].z)
            {
                return false;
            }
        }
        for (ULONG i = 0; i < wa.GetSize(); ++i)
        {
            if (wa[i] != wb[i])
            {
                return false;
            }
        }
        for (ULONG i = 0; i < ka.GetSize(); ++i)
        {
            if (ka[i] != kb[i])
            {
                return false;
            }
        }
        return true;
    }
    std::vector<SavedUv> Save(SmTArray<SmEdgeuse*>& uses, std::size_t record)
    {
        std::set<SmEdgeuse*> owners;
        std::vector<SavedUv> saved;
        std::size_t controls = 0;
        for (const auto use : uses)
        {
            SmEdgeuse* owner = nullptr;
            const auto uv = use->GetUVTrimCurve(&owner);
            Need(uv && owner, "PeriodicNormalizationMissingUv", record);
            if (!owners.insert(owner).second)
            {
                continue;
            }
            CountControls(*uv, controls, record);
            SmCurve* raw = nullptr;
            Step(record);
            Good(uv->Copy(*m.context, raw), record);
            std::unique_ptr<SmCurve> copy(raw);
            Need(raw && raw != uv, "PeriodicNormalizationUvAlias", record);
            raw->SetOwner(nullptr);
            saved.push_back({ owner, std::move(copy) });
        }
        return saved;
    }
    std::vector<SavedUv> SaveEdgeUv(SmEdge& edge, std::size_t record)
    {
        SmTArray<SmEdgeuse*> uses;
        Good(edge.GetEdgeuses(uses), record);
        return Save(uses, record);
    }
    void Restore(std::vector<SavedUv>& saved, std::size_t record)
    {
        for (auto& item : saved)
        {
            const auto curve = dynamic_cast<SmBSplineCurve*>(item.curve.get());
            Need(curve, "PeriodicNormalizationUvType", record);
            Step(record);
            item.owner->SetUVTrimCurve(curve, 0., TRUE);
            Need(curve->GetOwner() == item.owner || curve->GetOwner() == item.owner->GetMate(), "PeriodicNormalizationUvOwner", record);
            item.curve.release();
        }
    }
    void Crop(SmFace& face, std::size_t record)
    {
        const auto original = dynamic_cast<SmBSplineSurface*>(face.GetSurface());
        Need(original, "PeriodicNormalizationSurfaceType", record, Status::Unsupported);
        SmTArray<SmEdgeuse*> uses;
        face.GetUpwardEdgeuses(uses);
        double loU = INFINITY, loV = INFINITY, hiU = -INFINITY, hiV = -INFINITY;
        std::size_t controls = 0;
        for (const auto use : uses)
        {
            const auto uv = use->GetUVTrimCurve();
            ULONG count = 0;
            double* net = nullptr;
            Need(uv && uv->GetControlPointsPointer(count, net) == SM_SUCCESS && net, "PeriodicNormalizationUvHull", record);
            Need(
                count <= q.sourceLimits.values - std::min(controls, q.sourceLimits.values),
                "PeriodicNormalizationControlLimit",
                record,
                Status::ResourceLimit
            );
            controls += count;
            for (ULONG i = 0; i < count; ++i)
            {
                const double weight = uv->IsRational() ? net[4 * i + 3] : 1.;
                Need(weight > 0 && std::isfinite(weight), "PeriodicNormalizationUvWeights", record, Status::Indeterminate);
                const double u = net[4 * i] / weight, v = net[4 * i + 1] / weight;
                Need(std::isfinite(u) && std::isfinite(v), "PeriodicNormalizationUvHull", record, Status::Indeterminate);
                loU = std::min(loU, u);
                hiU = std::max(hiU, u);
                loV = std::min(loV, v);
                hiV = std::max(hiV, v);
            }
        }
        const auto natural = original->GetNaturalUVDomain();
        Need(
            loU < hiU && loV < hiV && loU >= natural.GetMin().x && hiU <= natural.GetMax().x && loV >= natural.GetMin().y &&
                hiV <= natural.GetMax().y,
            "PeriodicNormalizationUvRegion",
            record,
            Status::Indeterminate
        );
        SmSurface* raw = nullptr;
        Step(record);
        // Retain the native analytic subclass and its parameter correspondence.
        // A generic base copy can be recognized as a different analytic chart on
        // archive readback; equal control nets alone do not qualify that change.
        // The whole retained NURBS rectangle must still pass the proof below,
        // and the final native validators check the analytic/NURBS consistency.
        Good(original->Copy(*m.context, raw), record);
        std::unique_ptr<SmSurface> candidate(raw);
        Need(raw && raw != original, "PeriodicNormalizationSurfaceAlias", record);
        raw->SetOwner(nullptr);
        const auto restricted = dynamic_cast<SmBSplineSurface*>(raw);
        Need(restricted, "PeriodicNormalizationSurfaceType", record);
        SmExtent2d requested(loU, loV, hiU, hiV);
        Step(record);
        Good(restricted->TrimWithDomain(requested), record);
        const auto actual = restricted->GetNaturalUVDomain();
        Need(
            actual.GetMin().x <= loU && actual.GetMax().x >= hiU && actual.GetMin().y <= loV && actual.GetMax().y >= hiV,
            "PeriodicNormalizationRegionClipped",
            record
        );
        const auto proof = ProveSurfaceRestriction(
            *original,
            *restricted,
            Control(q.quality.definitionMm),
            q.sourceLimits.values,
            q.sourceLimits.values,
            [&]
            {
                Step(record);
            }
        );
        Need(
            proof.established,
            "PeriodicNormalizationSurfaceDefinitionUnestablished",
            record,
            proof.workLimit ? Status::ResourceLimit : Status::Indeterminate
        );
        Need(proof.maximumMm.has_value(), "PeriodicNormalizationSurfaceDefinitionUnestablished", record, Status::Indeterminate);
        auto saved = Save(uses, record);
        Step(record);
        face.SetSurface(raw, TRUE);
        const bool adopted = face.GetSurface() == raw;
        if (adopted)
        {
            candidate.release();
        }
        Need(adopted && raw->GetOwner() == &face, "PeriodicNormalizationSurfaceOwner", record);
        face.SetUVDomain(actual);
        Restore(saved, record);
    }
};
} // namespace
void NormalizePeriodicTopology(
    ModelData& model,
    std::span<const std::size_t> faces,
    std::span<const std::size_t> edges,
    std::span<const std::size_t> vertices,
    std::span<const std::size_t> derived,
    const Options& options,
    Report& report,
    const std::function<void(std::size_t)>& step
)
{
    Normalizer(model, faces, edges, vertices, derived, options, report, step).Run();
}
} // namespace step_import::detail
