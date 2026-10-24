// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportExecution.h"

#include <algorithm>
#include <cmath>

namespace step_import::detail
{
namespace
{
tl::unexpected<Failure> Refuse(Status status, const char* key)
{
    return tl::unexpected(
        Failure{ status, key, "Explicit terminal-domain interpretation contract is unestablished", {}, "SourceDomainInterpretation" }
    );
}
} // namespace
Result<void> Execution::BindTerminalDomainSource(const step_source::SourceSnapshot& source)
{
    if (!m_terminalDomainInterpretation)
    {
        return {};
    }
    // Bind to the immutable capture, never a mutable report hash or a second
    // path read. An assembly's definition views share this same original.
    if (source.Sha256() != m_terminalDomainInterpretation->sourceSha256)
    {
        return Refuse(Status::Unsupported, "TerminalDomainSourceMismatch");
    }
    m_terminalDomainSourceBound = true;
    return {};
}
Result<void> Execution::SpendTerminalDomainWork(std::size_t units)
{
    if (auto boundary = Check(Phase::BeforeNativeCall); !boundary)
    {
        return tl::unexpected(std::move(boundary.error()));
    }
    if (!m_terminalDomainInterpretation || !m_terminalDomainSourceBound)
    {
        return Refuse(Status::Unsupported, "TerminalDomainSourceNotBound");
    }
    if (units > m_terminalDomainInterpretation->maximumProofWorkUnits - m_terminalDomainInterpretation->proofWorkUnits)
    {
        return Refuse(Status::ResourceLimit, "TerminalDomainProofWorkLimit");
    }
    m_terminalDomainInterpretation->proofWorkUnits += units;
    return {};
}
Result<void> Execution::RecordTerminalDomainAction(std::size_t plan, TerminalDomainInterpretationAction action)
{
    if (!m_terminalDomainInterpretation || !m_terminalDomainSourceBound || plan >= m_terminalDomainPlansApplied.size() ||
        action.sourceSurfaceId != m_terminalDomainInterpretation->plans[plan].surfaceId)
    {
        return Refuse(Status::Unsupported, "TerminalDomainPlanScopeConflict");
    }
    const auto definition = m_options.quality.definitionMm;
    const double allowance = std::nextafter(
        std::nextafter(double(definition.Numerator()), 0.) / std::nextafter(double(definition.Denominator()), INFINITY),
        0.
    );
    if (!std::isfinite(action.wholeNewDomainErrorUpperMm) || action.wholeNewDomainErrorUpperMm < 0 || action.wholeNewDomainErrorUpperMm > allowance ||
        !std::all_of(
            action.targetNativeDomain.begin(),
            action.targetNativeDomain.end(),
            [](double x)
            {
                return std::isfinite(x);
            }
        ) ||
        !(action.targetNativeDomain[2] > action.targetNativeDomain[0]) || !(action.targetNativeDomain[3] > action.targetNativeDomain[1]))
    {
        return Refuse(Status::Indeterminate, "TerminalDomainFinalEvidenceUnestablished");
    }
    // Repeated native uses may retain independent contexts, but one source
    // surface counts once against the explicit plan's surface budget.
    if (!m_terminalDomainPlansApplied[plan] &&
        static_cast<std::size_t>(std::count(m_terminalDomainPlansApplied.begin(), m_terminalDomainPlansApplied.end(), true)) >=
            m_terminalDomainInterpretation->maximumSurfaces)
    {
        return Refuse(Status::ResourceLimit, "TerminalDomainSurfaceLimit");
    }
    m_terminalDomainInterpretation->actions.push_back(std::move(action));
    m_terminalDomainPlansApplied[plan] = true;
    return {};
}
Result<void> Execution::CheckTerminalDomainCompletion()
{
    if (!m_terminalDomainInterpretation)
    {
        return {};
    }
    if (!m_terminalDomainSourceBound || m_terminalDomainPlansApplied.empty() ||
        std::find(m_terminalDomainPlansApplied.begin(), m_terminalDomainPlansApplied.end(), false) != m_terminalDomainPlansApplied.end())
    {
        return Refuse(Status::Unsupported, "TerminalDomainPlanNotCompleted");
    }
    for (const auto& plan : m_terminalDomainInterpretation->plans)
    {
        for (const auto& id : plan.pcurveIds)
        {
            if (std::find(m_terminalDomainPcurvesApplied.begin(), m_terminalDomainPcurvesApplied.end(), id) == m_terminalDomainPcurvesApplied.end())
            {
                return Refuse(Status::Unsupported, "TerminalDomainPcurveNotConstructed");
            }
        }
    }
    // Finish before the request's final report/commit. Published snapshots own
    // the const receipt, which is never changed after that commit point.
    m_terminalDomainInterpretation->requestComplete = true;
    return {};
}
Result<void> Execution::ClaimTerminalDomainPcurve(const std::string& id)
{
    if (!m_terminalDomainInterpretation)
    {
        return {};
    }
    if (!m_terminalDomainSourceBound)
    {
        return Refuse(Status::Unsupported, "TerminalDomainSourceNotBound");
    }
    for (const auto& plan : m_terminalDomainInterpretation->plans)
    {
        if (std::find(plan.pcurveIds.begin(), plan.pcurveIds.end(), id) != plan.pcurveIds.end())
        {
            if (std::find(m_terminalDomainPcurvesApplied.begin(), m_terminalDomainPcurvesApplied.end(), id) == m_terminalDomainPcurvesApplied.end())
            {
                m_terminalDomainPcurvesApplied.push_back(id);
            }
            break;
        }
    }
    return {};
}
} // namespace step_import::detail
