// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportConicalApexRepair.h"

#include "StepImportConicalApexProof.h"
#include "StepImportConicalApexSession.h"
#include "StepImportCylindricalBandRepair.h"
#include "StepImportExecution.h"
#include "StepImportNativeData.h"

#include <SmCircle.h>
#include <SmCone.h>
#include <SmEdge.h>
#include <SmFace.h>
#include <SmTol.h>
#include <SmVertex.h>
#include <algorithm>
#include <map>

namespace step_import::detail
{
namespace
{
Failure Refusal(const char* key, std::optional<std::size_t> record = {})
{
    return { Status::Indeterminate, key, "Explicit conical apex repair evidence is unestablished", record, "Repair" };
}
template <class T>
Point P(const T& p)
{
    return { p.x, p.y, p.z };
}
Result<void> SameZone(const SmObject& a, const SmObject& b)
{
    if (SmTol::GetZoneTol3d(&a) != SmTol::GetZoneTol3d(&b))
    {
        return tl::unexpected(Refusal("ConicalApexRepairChangedWorkTolerance"));
    }
    return {};
}
Result<void> Compare(
    const ModelData& original,
    const ModelData& candidate,
    const ConicalApexRepairHistory& history,
    Execution& execution,
    Report& report
)
{
    if (original.repairVertexSources != candidate.repairVertexSources || original.repairEdgeSources != candidate.repairEdgeSources ||
        original.faces.size() != candidate.faces.size() || original.vertices.size() != candidate.vertices.size() ||
        original.targetVertices.size() != candidate.targetVertices.size() || original.targetEdges.size() != candidate.targetEdges.size() ||
        original.targetVertexOwners != candidate.targetVertexOwners || original.targetEdgeOwners != candidate.targetEdgeOwners)
    {
        return tl::unexpected(Refusal("ConicalApexRepairChangedSourceInventory"));
    }
    if (auto same = SameZone(*original.brep, *candidate.brep); !same)
    {
        return tl::unexpected(std::move(same.error()));
    }
    std::map<std::size_t, Point> changed;
    for (const auto& action : history.actions)
    {
        changed.emplace(action.targetVertex, action.correctedMm);
    }
    // Source vertices occupy the unchanged prefix of the final target table.
    // Include generated ring anchors and seams in the comparison: a local
    // source-point edit cannot silently move any other computational vertex or
    // change its working tolerance. Only the derived seam definition may change.
    for (std::size_t i = 0; i < original.targetVertices.size(); ++i)
    {
        if (auto work = execution.ConicalApexRepair()->Spend(execution); !work)
        {
            return tl::unexpected(std::move(work.error()));
        }
        const auto edited = changed.find(i);
        const auto expected = edited == changed.end() ? P(original.targetVertices[i]->GetPoint()) : edited->second;
        if (P(candidate.targetVertices[i]->GetPoint()) != expected)
        {
            return tl::unexpected(Refusal("ConicalApexRepairChangedUnrequestedVertex"));
        }
        if (auto same = SameZone(*original.targetVertices[i], *candidate.targetVertices[i]); !same)
        {
            return tl::unexpected(std::move(same.error()));
        }
    }
    for (std::size_t i = 0; i < original.repairEdgeSources.size(); ++i)
    {
        if (auto call = execution.ChargeNative(report, original.repairEdgeSources[i]); !call)
        {
            return tl::unexpected(std::move(call.error()));
        }
        if (auto same = RequireSameNativeCurveDefinition(*original.edges.at(i)->GetCurve(), *candidate.edges.at(i)->GetCurve()); !same)
        {
            return tl::unexpected(std::move(same.error()));
        }
    }
    for (std::size_t i = 0; i < original.targetEdges.size(); ++i)
    {
        if (auto work = execution.ConicalApexRepair()->Spend(execution); !work)
        {
            return tl::unexpected(std::move(work.error()));
        }
        if (auto same = SameZone(*original.targetEdges[i], *candidate.targetEdges[i]); !same)
        {
            return tl::unexpected(std::move(same.error()));
        }
    }
    for (std::size_t i = 0; i < original.faces.size(); ++i)
    {
        if (auto work = execution.ConicalApexRepair()->Spend(execution); !work)
        {
            return tl::unexpected(std::move(work.error()));
        }
        if (original.faces[i].faceId != candidate.faces[i].faceId)
        {
            return tl::unexpected(Refusal("ConicalApexRepairChangedFaceInventory"));
        }
        if (auto call = execution.ChargeNative(report, 0); !call)
        {
            return tl::unexpected(std::move(call.error()));
        }
        if (auto same = RequireSameNativeSurfaceDefinition(*original.faces[i].face->GetSurface(), *candidate.faces[i].face->GetSurface()); !same)
        {
            return tl::unexpected(std::move(same.error()));
        }
        if (auto same = SameZone(*original.faces[i].face, *candidate.faces[i].face); !same)
        {
            return tl::unexpected(std::move(same.error()));
        }
    }
    return {};
}
} // namespace
Result<NativeCandidate> BuildWithConicalApexRepair(
    std::shared_ptr<const step_semantics::Snapshot> source,
    std::optional<step_semantics::Part::Shape> binding,
    const Options& options,
    Report& report,
    Execution& execution,
    const ConicalApexRepairBuild& build
)
{
    auto* session = execution.ConicalApexRepair();
    if (!session)
    {
        return FinishConstructionAttempt(build(report, false, {}));
    }
    const auto& request = session->Request();
    const auto& part = source->PartDefinition();
    const auto& records = source->Source().Records();
    const auto definition = std::find_if(
        part.roles.begin(),
        part.roles.end(),
        [](const auto& role)
        {
            return role.name == "definition";
        }
    );
    if (definition == part.roles.end())
    {
        return tl::unexpected(Refusal("ConicalApexRepairDefinitionMissing"));
    }
    const auto shape = binding ? *binding : part.shapes.at(0);
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < request.uses.size(); ++i)
    {
        if (request.uses[i].definitionId == records[definition->recordIndex].id && request.uses[i].shapeId == records[shape.shape].id)
        {
            indices.push_back(i);
        }
    }
    if (indices.empty())
    {
        return FinishConstructionAttempt(build(report, false, {}));
    }
    if (shape.role != "MaterialSolid" || std::any_of(
                                             part.roles.begin(),
                                             part.roles.end(),
                                             [](const auto& role)
                                             {
                                                 return role.name.find("wireframe") != std::string::npos ||
                                                        role.name.find("supporting") != std::string::npos;
                                             }
                                         ))
    {
        return tl::unexpected(Refusal("ConicalApexRepairAssociatedScopeUnestablished"));
    }
    auto original = std::make_shared<Report>();
    original->generalBrep = true;
    original->encoding = report.encoding;
    original->sourceEvidenceScope = "RepairParent";
    original->source = report.source;
    original->sourceSha256 = report.sourceSha256;
    original->sourceSchema = report.sourceSchema;
    original->numericalIntegration = report.numericalIntegration;
    report.originalAttempt = original;
    report.repairStage = "StrictConicalApexCandidate";
    auto strict = build(*original, true, {});
    if (strict)
    {
        original->candidateDiscarded = true;
        report.originalAttemptStatus = HasAppliedSourceInterpretation(*source) ? Status::AcceptedUnderInterpretation : Status::Accepted;
        return tl::unexpected(
            Failure{ Status::Unsupported, "RepairNotRequired", "An already accepted candidate cannot be altered", shape.shape, "Repair" }
        );
    }
    auto* captured = std::get_if<ConicalApexFailureCandidate>(&strict.error());
    if (!captured)
    {
        auto failed = FinishConstructionAttempt(std::move(strict));
        auto failure = std::move(failed.error());
        report.originalAttemptStatus = failure.status;
        original->candidateDiscarded = original->nativeCalls > 0;
        AddFailureDiagnostic(*original, failure);
        return tl::unexpected(std::move(failure));
    }
    auto witness = std::move(*captured);
    report.originalAttemptStatus = witness.failure.status;
    original->candidateDiscarded = true;
    AddFailureDiagnostic(*original, witness.failure);
    if (!witness.model || !witness.model->topologyNormalization || witness.model->repairVertexSources.size() != witness.model->vertices.size())
    {
        return tl::unexpected(Refusal("ConicalApexRepairReferenceIncomplete"));
    }
    report.repairStage = "ConicalApexEvidenceBefore";
    ConicalApexRepairHistory history;
    history.sourceSha256 = request.sourceSha256;
    history.originalDiagnostics = original->diagnostics;
    history.sourceInterpretationApplied = HasAppliedSourceInterpretation(*source);
    history.budget = session->Budget();
    VertexPointOverrides overrides;
    const auto& model = *witness.model;
    for (const auto index : indices)
    {
        const auto& use = request.uses[index];
        if (auto work = session->Spend(execution); !work)
        {
            return tl::unexpected(std::move(work.error()));
        }
        const auto* vertexRecord = source->Source().Find(use.vertexId);
        if (!vertexRecord)
        {
            return tl::unexpected(Refusal("ConicalApexRepairVertexMissing"));
        }
        const auto record = static_cast<std::size_t>(vertexRecord - records.data());
        const auto vertex = std::find(model.repairVertexSources.begin(), model.repairVertexSources.end(), record);
        if (vertex == model.repairVertexSources.end())
        {
            return tl::unexpected(Refusal("ConicalApexRepairVertexOutsideBody", record));
        }
        const auto ordinal = static_cast<std::size_t>(vertex - model.repairVertexSources.begin());
        const auto cut = std::find_if(
            model.topologyNormalization->vertexLoopCuts.begin(),
            model.topologyNormalization->vertexLoopCuts.end(),
            [&](const auto& value)
            {
                return value.sourceFaceId == use.faceId && value.sourceBoundId == use.boundId && value.sourceLoopId == use.loopId &&
                       value.sourceVertexId == use.vertexId;
            }
        );
        if (cut == model.topologyNormalization->vertexLoopCuts.end())
        {
            return tl::unexpected(Refusal("ConicalApexRepairCutBinding", record));
        }
        const auto* cone = dynamic_cast<const SmCone*>(model.faces.at(cut->targetFace).face->GetSurface());
        if (!cone)
        {
            return tl::unexpected(Refusal("ConicalApexRepairNativeCone", record));
        }
        const auto* faceRecord = source->Source().Find(use.faceId);
        const auto face = static_cast<std::size_t>(faceRecord - records.data());
        const auto bounds = source->Field(face, "bounds").ReferenceTargets();
        const SmCircle* ring = nullptr;
        for (const auto bound : bounds)
        {
            const auto loop = source->Field(bound, "bound").ReferenceTarget();
            if (!source->IsA(loop, "EDGE_LOOP"))
            {
                continue;
            }
            const auto uses = source->Field(loop, "edge_list").ReferenceTargets();
            if (uses.size() != 1)
            {
                return tl::unexpected(Refusal("ConicalApexRepairRingScope", face));
            }
            const auto edge = source->Field(uses[0], "edge_element").ReferenceTarget();
            const auto found = std::find(model.repairEdgeSources.begin(), model.repairEdgeSources.end(), edge);
            if (found == model.repairEdgeSources.end())
            {
                return tl::unexpected(Refusal("ConicalApexRepairRingMissing", face));
            }
            ring = dynamic_cast<const SmCircle*>(model.edges.at(static_cast<std::size_t>(found - model.repairEdgeSources.begin()))->GetCurve());
        }
        if (!ring)
        {
            return tl::unexpected(Refusal("ConicalApexRepairRingDefinition", face));
        }
        std::vector<Point> otherVertices;
        const auto& targets = model.targetVertices;
        for (const auto* point : targets)
        {
            if (point != model.vertices[ordinal])
            {
                otherVertices.push_back(P(point->GetPoint()));
            }
        }
        std::vector<const SmCurve*> curves;
        for (std::size_t i = 0; i < model.repairEdgeSources.size(); ++i)
        {
            curves.push_back(model.edges.at(i)->GetCurve());
        }
        std::vector<const SmSurface*> surfaces;
        for (std::size_t i = 0; i < model.faces.size(); ++i)
        {
            if (i != cut->targetFace)
            {
                surfaces.push_back(model.faces[i].face->GetSurface());
            }
        }
        const auto originalPoint = P(model.vertices[ordinal]->GetPoint());
        auto proof = ProveConicalApexPointCorrection(
            originalPoint,
            *cone,
            *ring,
            otherVertices,
            curves,
            surfaces,
            { request.maximumDisplacementMm, options.quality.maximumWorkToleranceMm, options.quality.definitionMm, request.maximumProofControls },
            { [&]
              {
                  return execution.ChargeNative(report, record);
              },
              [&](std::size_t work)
              {
                  return session->Spend(execution, work);
              } }
        );
        if (!proof)
        {
            auto failure = std::move(proof.error());
            if (!failure.record)
            {
                failure.record = record;
            }
            return tl::unexpected(std::move(failure));
        }
        if (!*proof)
        {
            return tl::unexpected(Refusal("ConicalApexRepairPointNotRequired", record));
        }
        const auto pointRecord = source->Field(record, "vertex_geometry").ReferenceTarget();
        history.actions.push_back(
            { use,
              records[pointRecord].id,
              ordinal,
              originalPoint,
              (**proof).correctedMm,
              (**proof).displacementUpperMm,
              (**proof).localProtectionLowerMm }
        );
        overrides.emplace_back(record, (**proof).correctedMm);
    }
    if (auto claimed = session->Claim(indices, history.actions); !claimed)
    {
        return tl::unexpected(std::move(claimed.error()));
    }
    report.repairStage = "ConicalApexIndependentCandidate";
    report.repairEditsAttempted = history.actions.size();
    if (auto ready = execution.Check(Phase::BeforeRepair); !ready)
    {
        return tl::unexpected(std::move(ready.error()));
    }
    auto rebuilt = FinishConstructionAttempt(build(report, false, overrides));
    if (!rebuilt)
    {
        return tl::unexpected(std::move(rebuilt.error()));
    }
    auto candidate = std::move(*rebuilt);
    if (auto compared = Compare(*witness.model, *candidate, history, execution, report); !compared)
    {
        return tl::unexpected(std::move(compared.error()));
    }
    history.curvesAndSurfacesIdentical = history.unrequestedVerticesIdentical = history.workingTolerancesIdentical = true;
    candidate->conicalApexRepairs = history;
    report.conicalApexRepairs = std::move(history);
    report.conicalApexBudget = session->Budget();
    report.repairStage = "ConicalApexVerified";
    if (auto ready = execution.Check(Phase::RepairReady); !ready)
    {
        return tl::unexpected(std::move(ready.error()));
    }
    return candidate;
}
} // namespace step_import::detail
