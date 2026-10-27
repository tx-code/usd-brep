// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportConicalApexSession.h"

#include "StepImportExecution.h"
#include "StepImportInterval.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace step_import::detail
{
namespace
{
Failure Refusal(const char* key, Status status = Status::Unsupported, std::optional<std::size_t> record = {})
{
    return { status, key, "Explicit conical apex repair scope or budget is unestablished", record, "Repair" };
}
bool Id(const std::string& value)
{
    return !value.empty() && value.size() <= 64 && (value.size() == 1 || value.front() != '0') &&
           std::all_of(
               value.begin(),
               value.end(),
               [](char c)
               {
                   return c >= '0' && c <= '9';
               }
           );
}
} // namespace
void ValidateConicalApexOptions(const Options& o)
{
    if (!o.conicalApexRepair)
    {
        return;
    }
    const auto& r = *o.conicalApexRepair;
    if (!o.generalBrep || o.faceOrientationRepair || o.cylindricalBandRepair || o.definitionSelection || o.sourceErrorIsolation ||
        o.normalizePeriodicFaces || o.restrictUnusedSupportDomains || o.terminalDomainInterpretation || o.surfaceCurve3dInterpretation ||
        o.targetSurfacePrecision || o.quality.pcurveCarrierWorkToleranceMm || r.sourceSha256.size() != 64 ||
        !std::all_of(
            r.sourceSha256.begin(),
            r.sourceSha256.end(),
            [](char c)
            {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            }
        ) ||
        r.uses.empty() || !r.maximumVertices || r.uses.size() > r.maximumVertices || r.maximumVertices > o.sourceLimits.records ||
        !r.maximumProofControls || r.maximumProofControls > 2000000 || !r.maximumProofWork || r.maximumProofWork > 100000000 ||
        r.maximumDisplacementMm.Numerator() <= 0 || r.maximumTotalDisplacementMm.Numerator() <= 0)
    {
        throw std::invalid_argument(
            "Conical apex repair requires explicit source-bound general scope, movement/proof budgets and no unqualified combinations"
        );
    }
    std::set<std::tuple<std::string, std::string, std::string>> vertices;
    for (const auto& use : r.uses)
    {
        for (const auto* value : { &use.definitionId, &use.shapeId, &use.faceId, &use.boundId, &use.loopId, &use.vertexId, &use.surfaceId })
        {
            if (!Id(*value))
            {
                throw std::invalid_argument("Conical apex repair requires canonical source identities");
            }
        }
        if (!vertices.emplace(use.definitionId, use.shapeId, use.vertexId).second)
        {
            throw std::invalid_argument("Conical apex repair cannot choose multiple destinations for one source vertex");
        }
    }
}
ConicalApexRepairSession::ConicalApexRepairSession(const ConicalApexRepairRequest& request)
    : m_request(request), m_budget(std::make_shared<ConicalApexRepairBudget>()), m_claimed(request.uses.size(), false)
{
    m_budget->maximumVertices = request.maximumVertices;
    m_budget->maximumProofWork = request.maximumProofWork;
    m_budget->maximumDisplacementMm = request.maximumDisplacementMm;
    m_budget->maximumTotalDisplacementMm = request.maximumTotalDisplacementMm;
}
Result<void> ConicalApexRepairSession::BindSource(const step_source::SourceSnapshot& source)
{
    if (m_bound || source.Sha256() != m_request.sourceSha256)
    {
        return tl::unexpected(Refusal("ConicalApexRepairSourceMismatch"));
    }
    m_bound = true;
    return {};
}
Result<void> ConicalApexRepairSession::Spend(Execution& execution, std::size_t amount)
{
    if (auto check = execution.Check(Phase::BeforeRepairProof); !check)
    {
        return tl::unexpected(std::move(check.error()));
    }
    if (amount > m_request.maximumProofWork - m_budget->proofWork)
    {
        return tl::unexpected(Refusal("ConicalApexRepairProofWorkLimit", Status::ResourceLimit));
    }
    m_budget->proofWork += amount;
    return {};
}
Result<void> ConicalApexRepairSession::ValidateTargets(const step_semantics::Outcome& checked, Execution& execution)
{
    if (!m_bound || m_validated)
    {
        return tl::unexpected(Refusal("ConicalApexRepairBindingState"));
    }
    using Key = std::pair<std::string, std::string>;
    using Body = std::pair<std::shared_ptr<const step_semantics::Snapshot>, step_semantics::Part::Shape>;
    std::map<Key, Body> bodies;
    const auto include = [&](const std::shared_ptr<const step_semantics::Snapshot>& source) -> Result<void>
    {
        if (!source || (source->SelectedProfile().kind != step_semantics::ProfileKind::Ap214e3Source &&
                        source->SelectedProfile().kind != step_semantics::ProfileKind::Ap214MbxSource))
        {
            return tl::unexpected(Refusal("ConicalApexRepairSourceProfile"));
        }
        const auto& roles = source->PartDefinition().roles;
        const auto definition = std::find_if(
            roles.begin(),
            roles.end(),
            [](const auto& role)
            {
                return role.name == "definition";
            }
        );
        if (definition == roles.end())
        {
            return tl::unexpected(Refusal("ConicalApexRepairDefinitionMissing"));
        }
        for (const auto& shape : source->PartDefinition().shapes)
        {
            if (auto work = Spend(execution); !work)
            {
                return tl::unexpected(std::move(work.error()));
            }
            const auto& records = source->Source().Records();
            if (!bodies.emplace(Key{ records[definition->recordIndex].id, records[shape.shape].id }, Body{ source, shape }).second)
            {
                return tl::unexpected(Refusal("ConicalApexRepairBodyAmbiguous"));
            }
        }
        return {};
    };
    if (checked.document)
    {
        for (std::size_t i = 0; i < checked.document->PartCount(); ++i)
        {
            if (auto result = include(checked.document->Part(i)); !result)
            {
                return tl::unexpected(std::move(result.error()));
            }
        }
    }
    else if (auto result = include(checked.snapshot); !result)
    {
        return tl::unexpected(std::move(result.error()));
    }
    for (const auto& use : m_request.uses)
    {
        if (auto work = Spend(execution); !work)
        {
            return tl::unexpected(std::move(work.error()));
        }
        const auto body = bodies.find({ use.definitionId, use.shapeId });
        if (body == bodies.end() || body->second.second.role != "MaterialSolid")
        {
            return tl::unexpected(Refusal("ConicalApexRepairOutsideSelectedMaterial"));
        }
        const auto& source = *body->second.first;
        const auto& raw = source.Source();
        const auto index = [&](const std::string& id) -> std::optional<std::size_t>
        {
            const auto* record = raw.Find(id);
            return record ? std::optional<std::size_t>(static_cast<std::size_t>(record - raw.Records().data())) : std::nullopt;
        };
        const auto face = index(use.faceId), bound = index(use.boundId), loop = index(use.loopId), vertex = index(use.vertexId),
                   surface = index(use.surfaceId);
        if (!face || !bound || !loop || !vertex || !surface)
        {
            return tl::unexpected(Refusal("ConicalApexRepairIdentityMissing"));
        }
        const auto faces = source.Field(body->second.second.shell, "cfs_faces").ReferenceTargets();
        if (std::find(faces.begin(), faces.end(), *face) == faces.end() || !source.IsA(*face, "ADVANCED_FACE") ||
            source.Field(*face, "face_geometry").ReferenceTarget() != *surface || !source.IsA(*surface, "CONICAL_SURFACE"))
        {
            return tl::unexpected(Refusal("ConicalApexRepairFaceBinding", Status::Unsupported, *face));
        }
        const auto bounds = source.Field(*face, "bounds").ReferenceTargets();
        if (std::find(bounds.begin(), bounds.end(), *bound) == bounds.end() || source.IsA(*bound, "FACE_OUTER_BOUND") ||
            source.Field(*bound, "bound").ReferenceTarget() != *loop || !source.IsA(*loop, "VERTEX_LOOP") ||
            source.Field(*loop, "loop_vertex").ReferenceTarget() != *vertex || !source.IsA(*vertex, "VERTEX_POINT"))
        {
            return tl::unexpected(Refusal("ConicalApexRepairPointUseBinding", Status::Unsupported, *face));
        }
        // Complete reverse indexes prohibit moving a point also used by an
        // original edge, another loop/face, a foreign body or unknown metadata.
        // No physical use is truncated to make the whitelist appear local.
        for (const auto& [target, owner] : { std::pair{ use.vertexId, *loop }, { use.loopId, *bound }, { use.boundId, *face } })
        {
            const auto& incoming = raw.Incoming(target);
            if (incoming.size() != 1 || raw.References()[incoming.front()].recordIndex != owner)
            {
                return tl::unexpected(Refusal("ConicalApexRepairOutsidePhysicalUses", Status::Unsupported, *vertex));
            }
        }
    }
    m_validated = true;
    return {};
}
Result<void> ConicalApexRepairSession::Claim(std::span<const std::size_t> indices, std::span<const ConicalApexRepairAction> actions)
{
    if (!m_validated || indices.empty() || indices.size() != actions.size())
    {
        return tl::unexpected(Refusal("ConicalApexRepairClaimState"));
    }
    double cumulative = m_budget->cumulativeDisplacementUpperMm;
    double protection = m_budget->correctedVertices ? m_budget->minimumLocalProtectionLowerMm : INFINITY;
    std::set<std::size_t> unique;
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        const auto index = indices[i];
        const auto& action = actions[i];
        if (index >= m_claimed.size() || m_claimed[index] || !unique.insert(index).second || action.use != m_request.uses[index] ||
            !std::isfinite(action.displacementUpperMm) || !(action.displacementUpperMm > 0) ||
            action.displacementUpperMm > Enclose(m_request.maximumDisplacementMm).low || !std::isfinite(action.localProtectionLowerMm) ||
            !(action.localProtectionLowerMm > 0) || action.displacementUpperMm > Down(action.localProtectionLowerMm / 100))
        {
            return tl::unexpected(Refusal("ConicalApexRepairMovementOrClaim"));
        }
        cumulative = Up(cumulative + action.displacementUpperMm);
        protection = std::min(protection, action.localProtectionLowerMm);
    }
    if (indices.size() > m_request.maximumVertices - m_budget->correctedVertices || cumulative > Enclose(m_request.maximumTotalDisplacementMm).low)
    {
        return tl::unexpected(Refusal("ConicalApexRepairCumulativeMovementLimit", Status::ResourceLimit));
    }
    for (const auto index : indices)
    {
        m_claimed[index] = true;
    }
    m_budget->correctedVertices += indices.size();
    m_budget->cumulativeDisplacementUpperMm = cumulative;
    m_budget->minimumLocalProtectionLowerMm = protection;
    return {};
}
Result<void> ConicalApexRepairSession::Complete()
{
    if (!m_validated || std::find(m_claimed.begin(), m_claimed.end(), false) != m_claimed.end())
    {
        return tl::unexpected(Refusal("ConicalApexRepairUseNotApplied"));
    }
    m_budget->requestComplete = true;
    return {};
}
} // namespace step_import::detail
