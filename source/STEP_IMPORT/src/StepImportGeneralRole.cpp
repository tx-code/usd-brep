// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportGeneralRole.h"

#include "StepImportNativeData.h"
#include "StepImportNativeStatus.h"

#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <SmFaceuse.h>
#include <SmLoopuse.h>
#include <SmRegion.h>
#include <SmShell.h>
#include <SmVertex.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace step_import::detail
{
namespace
{
template <class T>
bool SameInventory(const SmTArray<T*>& actual, const std::vector<T*>& expected)
{
    std::set<T*> actualSet;
    for (ULONG i = 0; i < actual.GetSize(); ++i)
    {
        actualSet.insert(actual[i]);
    }
    const std::set<T*> expectedSet(expected.begin(), expected.end());
    return !expectedSet.contains(nullptr) && actual.GetSize() == actualSet.size() && expected.size() == expectedSet.size() &&
           actualSet == expectedSet;
}

class RoleVerifier
{
public:

    RoleVerifier(
        const ModelData& model,
        GeneralBodyRole role,
        std::span<const GeneralRoleFace> faces,
        std::span<const GeneralRoleEdge> edges,
        std::span<const std::size_t> derived,
        const NativeStep& before
    )
        : m(model), r(role), sourceFaces(faces), sourceEdges(edges), derivedOwners(derived), charge(before)
    {
    }

    Result<void> Run(Report& report)
    {
        if (auto result = Need(
                m.brep && m.context && m.source && charge && !sourceFaces.empty() && !GeneralRoleName(r).empty(),
                "GeneralRolePrerequisite",
                FirstRecord()
            );
            !result)
        {
            return result;
        }
        if (auto operation = charge(FirstRecord()); !operation)
        {
            return operation;
        }
        if (auto result = Prepare(); !result)
        {
            return result;
        }
        if (auto result = Regions(); !result)
        {
            return result;
        }
        if (auto result = Faces(); !result)
        {
            return result;
        }
        if (auto result = Edges(); !result)
        {
            return result;
        }
        // Commit only complete value observations. No earlier error can leave a
        // partial adjacency ledger which resembles a successful role check.
        report.nativeRole = observation;
        report.adjacency = std::move(adjacency);
        return {};
    }

private:

    struct FaceSide
    {
        std::size_t source;
        SmFaceuse* upward;
        SmFaceuse* opposite;
        SmFaceuse* selected;
    };
    struct ActualUse
    {
        std::size_t source;
        bool forward;
        SmEdgeuse* upward;
        SmEdgeuse* selected;
    };
    const ModelData& m;
    GeneralBodyRole r;
    std::span<const GeneralRoleFace> sourceFaces;
    std::span<const GeneralRoleEdge> sourceEdges;
    std::span<const std::size_t> derivedOwners;
    const NativeStep& charge;
    std::map<std::string, std::size_t> faceIds, edgeIds;
    std::map<SmFace*, FaceSide> faceSides;
    std::set<SmVertex*> vertices;
    std::vector<bool> sourceEdgesSeen;
    NativeRoleObservation observation;
    std::vector<EdgeAdjacency> adjacency;

    std::size_t FirstRecord() const
    {
        return sourceFaces.empty() ? 0 : sourceFaces.front().record;
    }
    std::string Id(std::size_t record) const
    {
        return m.source && record < m.source->Source().Records().size() ? m.source->Source().Records()[record].id : std::string{};
    }
    Result<void> Need(bool condition, const char* key, std::size_t record) const
    {
        if (!condition)
        {
            return tl::unexpected(
                Failure{ Status::PostcheckFailure, key, "Native candidate does not preserve the declared general body role", record, "GeneralBrep" }
            );
        }
        return {};
    }
    const std::vector<SmEdge*>& TargetEdges() const
    {
        return m.topologyNormalization ? m.targetEdges : m.edges;
    }
    const std::vector<SmVertex*>& TargetVertices() const
    {
        return m.topologyNormalization ? m.targetVertices : m.vertices;
    }

    Result<void> Prepare()
    {
        for (std::size_t i = 0; i < sourceFaces.size(); ++i)
        {
            const auto id = Id(sourceFaces[i].record);
            if (auto result = Need(!id.empty() && faceIds.emplace(id, i).second, "GeneralRoleSourceFaceIdentity", sourceFaces[i].record); !result)
            {
                return result;
            }
        }
        for (std::size_t i = 0; i < sourceEdges.size(); ++i)
        {
            const auto& edge = sourceEdges[i];
            const auto id = Id(edge.record);
            if (auto result = Need(!id.empty() && edgeIds.emplace(id, i).second, "GeneralRoleSourceEdgeIdentity", edge.record); !result)
            {
                return result;
            }
            const auto count = edge.uses.size();
            if (auto result = Need(count == 2 || (!IsClosedBoundary(r) && count == 1), "GeneralRoleSourceIncidence", edge.record); !result)
            {
                return result;
            }
            for (const auto& use : edge.uses)
            {
                if (auto result = Need(use.first < sourceFaces.size(), "GeneralRoleSourceUseFace", edge.record); !result)
                {
                    return result;
                }
            }
            // Compare selected-side direction after composing face.same; the
            // input direction is still the upward, surface-normal convention.
            if (count == 2)
            {
                const auto& a = edge.uses[0];
                const auto& b = edge.uses[1];
                if (auto result = Need(
                        (a.second == sourceFaces[a.first].same) != (b.second == sourceFaces[b.first].same),
                        "GeneralRoleSourceUseDirection",
                        edge.record
                    );
                    !result)
                {
                    return result;
                }
            }
        }
        if (auto result = Need(
                m.brep->GetContext() == m.context.get() &&
                    (m.topologyNormalization ? m.targetFaceSources.size() == m.faces.size() : m.faces.size() == sourceFaces.size()),
                "GeneralRoleTargetContextOrFaceMap",
                FirstRecord()
            );
            !result)
        {
            return result;
        }
        if (m.topologyNormalization)
        {
            const auto& origin = *m.topologyNormalization;
            if (auto result = Need(
                    origin.faces.size() == m.faces.size() && origin.edges.size() == TargetEdges().size() &&
                        origin.vertices.size() == TargetVertices().size(),
                    "GeneralRoleNormalizationInventory",
                    FirstRecord()
                );
                !result)
            {
                return result;
            }
            for (std::size_t i = 0; i < m.faces.size(); ++i)
            {
                if (auto result = Need(
                        m.targetFaceSources[i] < sourceFaces.size() && origin.faces[i].target == i &&
                            origin.faces[i].sourceFaceId == Id(sourceFaces[m.targetFaceSources[i]].record),
                        "GeneralRoleNormalizationFaceMap",
                        FirstRecord()
                    );
                    !result)
                {
                    return result;
                }
            }
            for (std::size_t i = 0; i < TargetVertices().size(); ++i)
            {
                if (auto result = Need(origin.vertices[i].target == i, "GeneralRoleNormalizationVertexMap", FirstRecord()); !result)
                {
                    return result;
                }
            }
        }
        else if (auto result = Need(m.edges.size() == sourceEdges.size() + derivedOwners.size(), "GeneralRoleSourceEdgeInventory", FirstRecord()); !result)
        {
            return result;
        }
        std::vector<SmFace*> expectedFaces;
        for (const auto& face : m.faces)
        {
            expectedFaces.push_back(face.face);
        }
        SmTArray<SmFace*> actualFaces;
        SmTArray<SmEdge*> actualEdges, wireEdges;
        SmTArray<SmVertex*> actualVertices, shellVertices;
        m.brep->GetFaces(actualFaces);
        m.brep->GetEdges(actualEdges);
        m.brep->GetVertices(actualVertices);
        m.brep->GetWireEdges(wireEdges);
        m.brep->GetShellVertices(shellVertices);
        if (auto result = Need(
                SameInventory(actualFaces, expectedFaces) && SameInventory(actualEdges, TargetEdges()) &&
                    SameInventory(actualVertices, TargetVertices()) && wireEdges.GetSize() == 0 && shellVertices.GetSize() == 0,
                "GeneralRoleNativeInventory",
                FirstRecord()
            );
            !result)
        {
            return result;
        }
        for (auto* vertex : TargetVertices())
        {
            const auto point = vertex->GetPoint();
            if (auto result = Need(
                    vertex->GetContext() == m.context.get() && vertex->GetBrep() == m.brep.get() && std::isfinite(point.x) &&
                        std::isfinite(point.y) && std::isfinite(point.z),
                    "GeneralRoleNativeVertex",
                    FirstRecord()
                );
                !result)
            {
                return result;
            }
            vertices.insert(vertex);
        }
        sourceEdgesSeen.resize(sourceEdges.size(), false);
        return {};
    }

    Result<void> Regions()
    {
        auto* infinite = m.brep->GetInfiniteRegion();
        const bool closed = IsClosedBoundary(r);
        if (auto result = Need(
                infinite && m.outside &&
                    (closed ? m.bounded && m.bounded != infinite && m.inside && m.inside != m.outside &&
                                  m.material == (IsMaterial(r) ? m.bounded : nullptr) :
                              !m.bounded && !m.inside && !m.material),
                "GeneralRoleRegionPolicy",
                FirstRecord()
            );
            !result)
        {
            return result;
        }
        SmTArray<SmRegion*> actualRegions;
        SmTArray<SmShell*> actualShells;
        m.brep->GetRegions(actualRegions);
        m.brep->GetShells(actualShells);
        std::vector<SmRegion*> expectedRegions{ infinite };
        std::vector<SmShell*> expectedShells{ m.outside };
        if (closed)
        {
            expectedRegions.push_back(m.bounded);
            expectedShells.push_back(m.inside);
        }
        // The BRep validator validates the live graph, not ModelData's borrowed
        // caches. Prove cached role pointers are live before dereferencing them.
        if (auto result = Need(
                SameInventory(actualRegions, expectedRegions) && SameInventory(actualShells, expectedShells),
                "GeneralRoleRegionShellInventory",
                FirstRecord()
            );
            !result)
        {
            return result;
        }
        if (auto result = Need(
                infinite->IsVoid() == TRUE && m.outside->IsFaceuseShell() && m.outside->GetRegion() == infinite &&
                    (!closed ||
                     (m.inside->IsFaceuseShell() && m.inside->GetRegion() == m.bounded && m.bounded->IsVoid() == (IsMaterial(r) ? FALSE : TRUE))),
                "GeneralRoleRegionPolicy",
                FirstRecord()
            );
            !result)
        {
            return result;
        }
        if (closed)
        {
            SmTArray<SmShell*> boundedShells;
            m.bounded->GetShells(boundedShells);
            if (auto result = Need(SameInventory(boundedShells, std::vector<SmShell*>{ m.inside }), "GeneralRoleBoundedShellInventory", FirstRecord());
                !result)
            {
                return result;
            }
        }
        SmTArray<SmShell*> infiniteShells;
        infinite->GetShells(infiniteShells);
        if (auto result = Need(SameInventory(infiniteShells, std::vector<SmShell*>{ m.outside }), "GeneralRoleInfiniteShellInventory", FirstRecord());
            !result)
        {
            return result;
        }
        for (auto* region : expectedRegions)
        {
            if (auto result = Need(
                    region->GetContext() == m.context.get() && region->GetBrep() == m.brep.get(),
                    "GeneralRoleRegionContext",
                    FirstRecord()
                );
                !result)
            {
                return result;
            }
            if (!region->IsVoid())
            {
                ++observation.materialRegions;
            }
            else if (region != infinite)
            {
                ++observation.boundedVoidRegions;
            }
        }
        for (auto* shell : expectedShells)
        {
            if (auto result = Need(
                    shell->GetContext() == m.context.get() && shell->GetBrep() == m.brep.get(),
                    "GeneralRoleShellContext",
                    FirstRecord()
                );
                !result)
            {
                return result;
            }
        }
        observation.regions = actualRegions.GetSize();
        observation.shells = actualShells.GetSize();
        // A closed surface may satisfy this topology predicate while both
        // regions are void. It is an observation, never material qualification.
        observation.manifoldSolidPredicate = m.brep->IsManifoldSolid() != FALSE;
        return {};
    }

    Result<void> Faces()
    {
        std::vector<SmFaceuse*> expectedOutside, expectedInside;
        std::vector<bool> seen(sourceFaces.size(), false);
        for (std::size_t i = 0; i < m.faces.size(); ++i)
        {
            const auto source = m.topologyNormalization ? m.targetFaceSources[i] : i;
            const auto record = sourceFaces[source].record;
            if (auto operation = charge(record); !operation)
            {
                return operation;
            }
            auto* face = m.faces[i].face;
            SmFaceuse* upward = nullptr;
            SmFaceuse* opposite = nullptr;
            face->GetFaceuses(upward, opposite);
            auto* surface = face->GetSurface();
            if (auto result = Need(
                    face->GetContext() == m.context.get() && face->GetBrep() == m.brep.get() && surface && surface->GetContext() == m.context.get() &&
                        surface->GetOwner() == face && upward && opposite && upward != opposite && upward->GetFace() == face &&
                        opposite->GetFace() == face && upward->GetMate() == opposite && opposite->GetMate() == upward &&
                        upward->GetOrientation() == SM_OT_SAME && opposite->GetOrientation() == SM_OT_OPPOSITE &&
                        upward->GetContext() == m.context.get() && opposite->GetContext() == m.context.get(),
                    "GeneralRoleFaceSides",
                    record
                );
                !result)
            {
                return result;
            }
            auto* selected = sourceFaces[source].same ? upward : opposite;
            if (auto result = Need(
                    selected->GetShell() == m.outside && selected->GetMate()->GetShell() == (IsClosedBoundary(r) ? m.inside : m.outside),
                    "GeneralRoleSelectedFaceSide",
                    record
                );
                !result)
            {
                return result;
            }
            faceSides.emplace(face, FaceSide{ source, upward, opposite, selected });
            expectedOutside.push_back(selected);
            (IsClosedBoundary(r) ? expectedInside : expectedOutside).push_back(selected->GetMate());
            seen[source] = true;
        }
        SmTArray<SmFaceuse*> actualOutside;
        m.outside->GetFaceuses(actualOutside);
        if (auto result = Need(
                SameInventory(actualOutside, expectedOutside) && std::all_of(
                                                                     seen.begin(),
                                                                     seen.end(),
                                                                     [](bool value)
                                                                     {
                                                                         return value;
                                                                     }
                                                                 ),
                "GeneralRoleOutsideFaceInventory",
                FirstRecord()
            );
            !result)
        {
            return result;
        }
        if (IsClosedBoundary(r))
        {
            SmTArray<SmFaceuse*> actualInside;
            m.inside->GetFaceuses(actualInside);
            if (auto result = Need(SameInventory(actualInside, expectedInside), "GeneralRoleInsideFaceInventory", FirstRecord()); !result)
            {
                return result;
            }
        }
        return {};
    }

    Result<void> Edges()
    {
        for (std::size_t i = 0; i < TargetEdges().size(); ++i)
        {
            auto* edge = TargetEdges()[i];
            std::optional<std::size_t> source, derivedFace;
            if (m.topologyNormalization)
            {
                const auto& origin = m.topologyNormalization->edges[i];
                if (auto result = Need(
                        origin.target == i && origin.sourceEdgeId.has_value() != origin.derivedFromFaceId.has_value(),
                        "GeneralRoleNormalizationEdgeMap",
                        FirstRecord()
                    );
                    !result)
                {
                    return result;
                }
                if (origin.sourceEdgeId)
                {
                    const auto found = edgeIds.find(*origin.sourceEdgeId);
                    if (auto result = Need(found != edgeIds.end(), "GeneralRoleNormalizationSourceEdge", FirstRecord()); !result)
                    {
                        return result;
                    }
                    source = found->second;
                }
                else
                {
                    const auto found = faceIds.find(*origin.derivedFromFaceId);
                    if (auto result = Need(found != faceIds.end(), "GeneralRoleNormalizationDerivedFace", FirstRecord()); !result)
                    {
                        return result;
                    }
                    derivedFace = found->second;
                }
                const auto interval = edge->GetInterval();
                if (auto result = Need(
                        interval.GetMin() == origin.basisParameterInterval[0] && interval.GetMax() == origin.basisParameterInterval[1],
                        "GeneralRoleNormalizationEdgeInterval",
                        source ? sourceEdges[*source].record : sourceFaces[*derivedFace].record
                    );
                    !result)
                {
                    return result;
                }
            }
            else if (i < sourceEdges.size())
            {
                source = i;
            }
            else
            {
                // Constructor-owned seam provenance is independent of the
                // observed native face. Never infer an expected origin from
                // the very incidence being checked.
                const auto found = faceIds.find(Id(derivedOwners[i - sourceEdges.size()]));
                if (auto result = Need(found != faceIds.end(), "GeneralRoleDerivedEdgeOrigin", FirstRecord()); !result)
                {
                    return result;
                }
                derivedFace = found->second;
            }
            const auto record = source ? sourceEdges[*source].record : derivedFace ? sourceFaces[*derivedFace].record : FirstRecord();
            if (auto operation = charge(record); !operation)
            {
                return operation;
            }
            auto* curve = edge->GetCurve();
            const auto interval = edge->GetInterval();
            if (auto result = Need(
                    edge->GetContext() == m.context.get() && edge->GetBrep() == m.brep.get() && curve && curve->GetContext() == m.context.get() &&
                        curve->GetOwner() == edge && curve->GetDim() == 3 && std::isfinite(interval.GetMin()) && std::isfinite(interval.GetMax()) &&
                        interval.GetMax() > interval.GetMin() && interval.IsContainedBy(curve->GetNaturalInterval(), 0.) &&
                        vertices.contains(edge->GetStartVertex()) && vertices.contains(edge->GetEndVertex()),
                    "GeneralRoleEdgeDefinition",
                    record
                );
                !result)
            {
                return result;
            }
            SmTArray<SmEdgeuse*> nativeUses;
            if (auto result = CheckNativeStatus(edge->GetEdgeuses(nativeUses), NativeStatusOrigin::GeneralConstruction); !result)
            {
                return result;
            }
            std::set<SmEdgeuse*> all;
            for (ULONG j = 0; j < nativeUses.GetSize(); ++j)
            {
                all.insert(nativeUses[j]);
            }
            if (auto result = Need(
                    !all.contains(nullptr) && all.size() == nativeUses.GetSize() && (all.size() == 2 || all.size() == 4),
                    "GeneralRoleNativeEdgeIncidence",
                    record
                );
                !result)
            {
                return result;
            }
            std::vector<ActualUse> actual;
            std::multiset<std::pair<std::size_t, bool>> actualKeys;
            for (auto* use : all)
            {
                auto* mate = use->GetMate();
                auto* radial = use->GetRadial();
                if (auto result = Need(
                        use->IsLoopEdgeuse() && use->GetContext() == m.context.get() && use->GetEdge() == edge && all.contains(mate) &&
                            all.contains(radial) && mate != use && radial != use && mate->GetMate() == use && radial->GetRadial() == use &&
                            (use->GetOrientation() == SM_OT_SAME || use->GetOrientation() == SM_OT_OPPOSITE) &&
                            use->GetOrientation() != mate->GetOrientation(),
                        "GeneralRoleMateRadial",
                        record
                    );
                    !result)
                {
                    return result;
                }
                auto* loop = use->GetLoopuse();
                auto* fu = loop ? loop->GetFaceuse() : nullptr;
                const auto face = fu ? faceSides.find(fu->GetFace()) : faceSides.end();
                if (auto result = Need(
                        loop && loop->GetContext() == m.context.get() && face != faceSides.end() &&
                            (fu == face->second.upward || fu == face->second.opposite) && mate->IsLoopEdgeuse() &&
                            mate->GetFaceuse() == fu->GetMate() && radial->IsLoopEdgeuse() && radial->GetFaceuse()->GetShell() == fu->GetShell(),
                        "GeneralRoleEdgeuseFace",
                        record
                    );
                    !result)
                {
                    return result;
                }
                if (fu == face->second.upward)
                {
                    const auto& side = face->second;
                    const bool forward = use->GetOrientation() == SM_OT_SAME;
                    auto* selected = side.selected == fu ? use : mate;
                    if (auto result = Need(
                            (selected->GetOrientation() == SM_OT_SAME) == (forward == sourceFaces[side.source].same),
                            "GeneralRoleSelectedUseDirection",
                            record
                        );
                        !result)
                    {
                        return result;
                    }
                    actual.push_back({ side.source, forward, use, selected });
                    actualKeys.emplace(side.source, forward);
                }
            }
            std::multiset<std::pair<std::size_t, bool>> expected;
            if (source)
            {
                expected.insert(sourceEdges[*source].uses.begin(), sourceEdges[*source].uses.end());
                sourceEdgesSeen[*source] = true;
            }
            else
            {
                // Appended unnormalized seams have no source edge identity.
                // Their two occurrences must belong to one source face, even
                // when a periodic cut creates two different target child faces.
                if (auto result = Need(derivedFace.has_value(), "GeneralRoleDerivedEdgeOrigin", record); !result)
                {
                    return result;
                }
                expected.emplace(*derivedFace, false);
                expected.emplace(*derivedFace, true);
            }
            if (auto result = Need(
                    actualKeys == expected && nativeUses.GetSize() == 2 * expected.size(),
                    "GeneralRoleSourceUseCorrespondence",
                    record
                );
                !result)
            {
                return result;
            }
            // Same shell is insufficient for an open sheet: both sides of
            // every face are there. Radials must join the selected sides of the
            // two logical occurrences, not silently cross to the other side.
            const bool radialCorrect = actual.size() == 1 ? actual[0].selected->GetRadial() == actual[0].selected->GetMate() :
                                                            actual.size() == 2 && actual[0].selected->GetRadial() == actual[1].selected &&
                                                                actual[1].selected->GetRadial() == actual[0].selected &&
                                                                actual[0].selected->GetMate()->GetRadial() == actual[1].selected->GetMate() &&
                                                                actual[1].selected->GetMate()->GetRadial() == actual[0].selected->GetMate();
            if (auto result = Need(radialCorrect, "GeneralRoleSelectedRadialPair", record); !result)
            {
                return result;
            }
            EdgeAdjacency row;
            row.edgeId = source ? Id(sourceEdges[*source].record) : std::string{};
            row.nativeUses = nativeUses.GetSize();
            for (const auto& key : expected)
            {
                row.faceIds.push_back(Id(sourceFaces[key.first].record));
            }
            adjacency.push_back(std::move(row));
        }
        return Need(
            std::all_of(
                sourceEdgesSeen.begin(),
                sourceEdgesSeen.end(),
                [](bool value)
                {
                    return value;
                }
            ),
            "GeneralRoleSourceEdgeCoverage",
            FirstRecord()
        );
    }
};
} // namespace

Result<void> VerifyGeneralRole(
    const ModelData& model,
    GeneralBodyRole role,
    std::span<const GeneralRoleFace> sourceFaces,
    std::span<const GeneralRoleEdge> sourceEdges,
    std::span<const std::size_t> derivedEdgeFaces,
    Report& report,
    const NativeStep& before
)
{
    return RoleVerifier(model, role, sourceFaces, sourceEdges, derivedEdgeFaces, before).Run(report);
}
} // namespace step_import::detail
