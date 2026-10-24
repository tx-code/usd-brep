// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportSurfaceCurveSession.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace step_import::detail
{
namespace
{
bool CanonicalLabel(std::string_view label)
{
    return !label.empty() && label.size() <= 64 && (label.size() == 1 || label.front() != '0') &&
           std::all_of(
               label.begin(),
               label.end(),
               [](char c)
               {
                   return c >= '0' && c <= '9';
               }
           );
}

bool SourceIdentity(std::string_view hash)
{
    return hash.size() == 64 && std::all_of(
                                    hash.begin(),
                                    hash.end(),
                                    [](char c)
                                    {
                                        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                                    }
                                );
}

bool AuxiliaryConflict(Status status, std::string_view key)
{
    if (status != Status::PostcheckFailure && status != Status::Indeterminate)
    {
        return false;
    }
    // These are comparison observations, not permission to suppress source
    // construction, type/unit, native API, execution or resource failures.
    return key == "GeneralSplineEndpointInversion" || key == "GeneralSurfaceCurveRepresentationsConflict" ||
           key == "GeneralSurfaceCurveAssociationSenseConflict" || key == "GeneralSurfaceCurveDirectionsConflict" ||
           key == "GeneralSurfaceCurveDirectionUnestablished" || key == "GeneralSurfaceCurveImageInversionUnestablished";
}

bool AuxiliaryConflict(const SurfaceCurve3dInterpretationConflict& conflict)
{
    if (conflict.status == "PostcheckFailure")
    {
        return AuxiliaryConflict(Status::PostcheckFailure, conflict.key);
    }
    if (conflict.status == "Indeterminate")
    {
        return AuxiliaryConflict(Status::Indeterminate, conflict.key);
    }
    return false;
}

tl::unexpected<Failure> Refuse(Status status, const char* key)
{
    return tl::unexpected(
        Failure{ status, key, "Explicit source-bound surface-curve interpretation contract is unestablished", {}, "SurfaceCurve3dInterpretation" }
    );
}

std::array<std::int64_t, 2> ExactBudget(const Fraction& budget)
{
    return { budget.Numerator(), budget.Denominator() };
}
Result<void> CheckConflict(const SurfaceCurve3dInterpretationUse& plan, const SurfaceCurve3dInterpretationConflict& conflict)
{
    // Match occurrences, not unique IDs: repeated identities in the source
    // LIST remain independent, ordered comparison obligations.
    if (!conflict.associationOccurrence || conflict.associationOccurrence > plan.pcurveIds.size() ||
        conflict.pcurveId != plan.pcurveIds[conflict.associationOccurrence - 1] || !CanonicalLabel(conflict.recordId))
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dConflictScopeConflict");
    }
    if (!AuxiliaryConflict(conflict))
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dConflictNotEligible");
    }
    return {};
}
} // namespace

void ValidateSurfaceCurve3dInterpretationOptions(const Options& options)
{
    if (!options.surfaceCurve3dInterpretation)
    {
        return;
    }
    const auto& request = *options.surfaceCurve3dInterpretation;
    if (!options.generalBrep || options.faceOrientationRepair || options.cylindricalBandRepair || options.definitionSelection ||
        options.sourceErrorIsolation || options.restrictUnusedSupportDomains || options.normalizePeriodicFaces ||
        !SourceIdentity(request.sourceSha256) || request.uses.empty() || !request.maximumUses || request.uses.size() > request.maximumUses ||
        request.maximumUses > options.sourceLimits.records)
    {
        throw std::invalid_argument("Surface-curve interpretation requires an explicit source-bound general scope and bounded nonempty uses");
    }
    std::set<std::tuple<std::string, std::string, std::string>> uses;
    for (const auto& plan : request.uses)
    {
        if (!CanonicalLabel(plan.definitionId) || !CanonicalLabel(plan.shapeId) || !CanonicalLabel(plan.edgeId) ||
            !CanonicalLabel(plan.surfaceCurveId) || !CanonicalLabel(plan.curve3dId) ||
            (plan.expectedMaster != SurfaceCurveDeclaredMaster::PcurveS1 && plan.expectedMaster != SurfaceCurveDeclaredMaster::PcurveS2) ||
            plan.pcurveIds.empty() || plan.pcurveIds.size() > 2 ||
            (plan.expectedMaster == SurfaceCurveDeclaredMaster::PcurveS2 && plan.pcurveIds.size() != 2) ||
            !std::all_of(
                plan.pcurveIds.begin(),
                plan.pcurveIds.end(),
                [](const auto& id)
                {
                    return CanonicalLabel(id);
                }
            ) ||
            !uses.emplace(plan.definitionId, plan.shapeId, plan.edgeId).second)
        {
            throw std::invalid_argument("Surface-curve interpretation requires unique canonical source uses and an exact associated PCURVE list");
        }
    }
}

bool IsSurfaceCurve3dAuxiliaryConflict(const Failure& failure)
{
    return AuxiliaryConflict(failure.status, failure.key);
}

SurfaceCurve3dInterpretationSession::SurfaceCurve3dInterpretationSession(const Options& options)
{
    ValidateSurfaceCurve3dInterpretationOptions(options);
    if (!options.surfaceCurve3dInterpretation)
    {
        throw std::invalid_argument("Surface-curve interpretation session requires an explicit request");
    }
    // This copy precedes progress callbacks. Caller mutation of its request or
    // quality cannot enlarge scope or change the budgets retained as evidence.
    auto request = *options.surfaceCurve3dInterpretation;
    m_receipt = std::make_shared<SurfaceCurve3dInterpretationReceipt>();
    m_receipt->sourceSha256 = std::move(request.sourceSha256);
    m_receipt->qualityId = options.quality.id;
    m_receipt->maximumUses = request.maximumUses;
    m_receipt->plans = std::move(request.uses);
    m_receipt->consistencyBudgetMm = ExactBudget(options.quality.consistencyMm);
    m_receipt->derivedUvBudgetMm = ExactBudget(options.quality.derivedUvMm);
    m_receipt->maximumWorkToleranceMm = ExactBudget(options.quality.maximumWorkToleranceMm);
    m_recorded.assign(m_receipt->plans.size(), false);
    for (std::size_t i = 0; i < m_receipt->plans.size(); ++i)
    {
        const auto& plan = m_receipt->plans[i];
        m_uses.emplace(std::make_tuple(plan.definitionId, plan.shapeId, plan.edgeId), i);
    }
}

Result<void> SurfaceCurve3dInterpretationSession::BindSource(const step_source::SourceSnapshot& source)
{
    if (m_receipt->requestComplete)
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dSessionCompleted");
    }
    if (source.Sha256() != m_receipt->sourceSha256)
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dSourceMismatch");
    }
    m_bound = true;
    return {};
}

Result<std::optional<std::size_t>> SurfaceCurve3dInterpretationSession::FindUse(
    std::string_view definition,
    std::string_view shape,
    std::string_view edge
) const
{
    if (m_receipt->requestComplete)
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dSessionCompleted");
    }
    if (!m_bound)
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dSourceNotBound");
    }
    if (!CanonicalLabel(definition) || !CanonicalLabel(shape) || !CanonicalLabel(edge))
    {
        return std::optional<std::size_t>{};
    }
    const auto found = m_uses.find(std::make_tuple(std::string(definition), std::string(shape), std::string(edge)));
    return found == m_uses.end() ? std::optional<std::size_t>{} : std::optional<std::size_t>{ found->second };
}

const SurfaceCurve3dInterpretationUse& SurfaceCurve3dInterpretationSession::Plan(std::size_t index) const
{
    return m_receipt->plans.at(index);
}

Result<std::size_t> SurfaceCurve3dInterpretationSession::RecordAction(SurfaceCurve3dInterpretationAction action)
{
    if (m_receipt->requestComplete)
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dSessionCompleted");
    }
    if (!m_bound)
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dSourceNotBound");
    }
    if (action.planIndex >= m_receipt->plans.size())
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dPlanScopeConflict");
    }
    if (m_recorded[action.planIndex] || std::any_of(
                                            m_receipt->actions.begin(),
                                            m_receipt->actions.end(),
                                            [&](const auto& recorded)
                                            {
                                                return recorded.planIndex == action.planIndex;
                                            }
                                        ))
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dPlanAlreadyRecorded");
    }
    if (m_receipt->actions.size() >= m_receipt->maximumUses)
    {
        return Refuse(Status::ResourceLimit, "SurfaceCurve3dActionLimit");
    }
    if (!std::isfinite(action.nativeEdgeInterval[0]) || !std::isfinite(action.nativeEdgeInterval[1]) ||
        !(action.nativeEdgeInterval[1] > action.nativeEdgeInterval[0]))
    {
        return Refuse(Status::Indeterminate, "SurfaceCurve3dIntervalUnestablished");
    }
    if (action.nativeChecksPassed)
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dNativeChecksMustBeObserved");
    }
    const auto& plan = m_receipt->plans[action.planIndex];
    for (const auto& conflict : action.originalConflicts)
    {
        if (auto checked = CheckConflict(plan, conflict); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
    }
    const auto planIndex = action.planIndex;
    const auto index = m_receipt->actions.size();
    m_receipt->actions.push_back(std::move(action));
    m_recorded[planIndex] = true;
    return index;
}

Result<void> SurfaceCurve3dInterpretationSession::RecordConflict(std::size_t action, SurfaceCurve3dInterpretationConflict conflict)
{
    if (m_receipt->requestComplete)
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dSessionCompleted");
    }
    if (!m_bound)
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dSourceNotBound");
    }
    if (action >= m_receipt->actions.size())
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dNativeCheckNotRecorded");
    }
    auto& recorded = m_receipt->actions[action];
    if (recorded.nativeChecksPassed)
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dActionAlreadyChecked");
    }
    if (auto checked = CheckConflict(m_receipt->plans.at(recorded.planIndex), conflict); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    // Own each observed refusal immediately. A later association, native call,
    // resource limit or cancellation must not erase it during stack unwinding.
    // A provisional action is never a completed plan or native qualification.
    recorded.originalConflicts.push_back(std::move(conflict));
    return {};
}

Result<void> SurfaceCurve3dInterpretationSession::MarkNativeChecked(std::size_t action)
{
    if (m_receipt->requestComplete)
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dSessionCompleted");
    }
    if (!m_bound)
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dSourceNotBound");
    }
    if (action >= m_receipt->actions.size() || m_receipt->actions[action].planIndex >= m_recorded.size() ||
        !m_recorded[m_receipt->actions[action].planIndex])
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dNativeCheckNotRecorded");
    }
    m_receipt->actions[action].nativeChecksPassed = true;
    return {};
}

Result<void> SurfaceCurve3dInterpretationSession::Complete()
{
    if (m_receipt->requestComplete)
    {
        return {};
    }
    if (!m_bound || m_recorded.empty() || m_receipt->actions.size() != m_receipt->plans.size() ||
        std::find(m_recorded.begin(), m_recorded.end(), false) != m_recorded.end())
    {
        return Refuse(Status::Unsupported, "SurfaceCurve3dPlanNotCompleted");
    }
    if (std::any_of(
            m_receipt->actions.begin(),
            m_receipt->actions.end(),
            [](const auto& action)
            {
                return !action.nativeChecksPassed;
            }
        ))
    {
        return Refuse(Status::Indeterminate, "SurfaceCurve3dNativeChecksUnestablished");
    }
    // All requested uses passed candidate native checks. Freeze observations
    // before the final report/commit decision; this is neither publication nor
    // qualification under the original source declaration. No later mutation.
    m_receipt->requestComplete = true;
    return {};
}

std::shared_ptr<const SurfaceCurve3dInterpretationReceipt> SurfaceCurve3dInterpretationSession::Evidence() const
{
    return m_receipt;
}
} // namespace step_import::detail
