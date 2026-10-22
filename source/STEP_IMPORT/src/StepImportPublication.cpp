// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPublication.h"

#include "StepImportExecution.h"

#include <algorithm>

namespace step_import::detail
{
Publication::Publication(const Options& options, Outcome& outcome, const step_source::InspectOutcome& source, Execution& execution)
    : m_options(options), m_outcome(outcome), m_source(source), m_execution(execution)
{
}
void Publication::Accept(std::shared_ptr<ModelData> candidate)
{
    // Ownership stays here across callback/report failures. Expected failures
    // are disposed before their report; unexpected exceptions unwind the owner.
    m_candidate = std::move(candidate);
    m_prepared = Access::Snapshot(std::move(m_candidate));
    m_execution.Check(Phase::NativeReady);
    // Complete checks do not authenticate an assumed source schema or carrier.
    // Choose this result before either report and final arbitration; after
    // Commit the sole remaining action remains the owning transfer.
    bool interpreted = false;
    {
        // Release this temporary source owner before the commit region. The
        // prepared model remains the authoritative owner of both assumptions.
        const auto semantic = m_prepared->Source();
        interpreted = semantic->AppliedInterpretation() != step_semantics::SchemaInterpretation::DeclaredOnly ||
                      semantic->Source().AppliedCarrierAssumption() != nullptr || semantic->Source().HasAsciiInvariantDataAssumption();
    }
    m_outcome.status = interpreted ? Status::AcceptedUnderInterpretation : Status::Accepted;
    m_execution.Check(Phase::BeforeReport);
    WriteReport();
    m_execution.Check(Phase::ReportReady);
    m_execution.Check(Phase::BeforeCommit);
    // Checkpoints update report observations. Preserve both serializations and
    // their budget/failure points, then arbitrate against late cancellation.
    WriteReport();
    m_execution.Commit();
    // Publication contract: docs/design/step-import.md. After successful
    // arbitration, no allocation, callback or native operation is allowed.
    // The only remaining action is owner transfer.
    m_outcome.model = std::move(m_prepared);
}
void Publication::WriteReport()
{
    Serialize(
        m_outcome,
        m_options.quality,
        m_source,
        std::min(m_options.nativeLimits.reportBytes, m_source.limits.reportBytes),
        m_outcome.report.json
    );
}
void Publication::Fail(const Failure& failure)
{
    m_outcome.status = failure.status;
    Diagnostic(failure);
}
void Publication::FinishFailure()
{
    // Preserve the existing disposal-before-report ordering and any discard
    // evidence already recorded by a builder that failed before handoff.
    const bool held = bool(m_candidate) || bool(m_prepared);
    m_prepared.reset();
    m_candidate.reset();
    auto& report = m_outcome.report;
    report.candidateDiscarded = report.candidateDiscarded || held;
    try
    {
        WriteReport();
    }
    catch (const Failure& failure)
    {
        // A bounded report failure cannot undo observed cancellation, and no
        // partial JSON may escape even when the failure report also overflows.
        if (m_outcome.status != Status::Cancelled)
        {
            m_outcome.status = failure.status;
        }
        Diagnostic(failure);
        report.json.clear();
    }
}
void Publication::Diagnostic(const Failure& failure)
{
    auto& report = m_outcome.report;
    step_semantics::Diagnostic diagnostic;
    diagnostic.source.key = failure.key;
    diagnostic.source.stage = failure.stage;
    diagnostic.source.message = failure.message;
    diagnostic.classification = failure.status == Status::Cancelled     ? "Cancelled" :
                                failure.status == Status::Unsupported   ? "TargetUnsupported" :
                                failure.status == Status::ResourceLimit ? "ResourceLimit" :
                                failure.status == Status::Indeterminate ? "Indeterminate" :
                                                                          "NativeCandidateFailure";
    if (failure.stage == "Execution" && failure.status == Status::Unsupported)
    {
        diagnostic.classification = "ExecutionUnsupported";
    }
    if (failure.stage == "Execution" && failure.status == Status::BuildFailure)
    {
        diagnostic.classification = "ExecutionFailure";
    }
    diagnostic.responsibility = failure.stage == "Execution" ? "in-process cooperative execution" :
                                                               "native candidate preparation/construction/postcheck; Repair=None";
    if (failure.record && report.source)
    {
        const auto& record = report.source->Records().at(*failure.record);
        diagnostic.source.recordId = record.id;
        diagnostic.source.primary = record.span;
    }
    report.diagnostics.push_back(std::move(diagnostic));
}
} // namespace step_import::detail
