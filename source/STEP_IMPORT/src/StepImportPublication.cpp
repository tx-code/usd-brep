// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPublication.h"

#include "StepImportExecution.h"
#include "StepImportNativeData.h"

#include <algorithm>

namespace step_import::detail
{
Publication::Publication(const Options& options, Outcome& outcome, const step_source::InspectOutcome& source, Execution& execution)
    : m_options(options), m_status(outcome.status), m_report(outcome.report), m_source(source), m_execution(execution), m_modelSink(&outcome.model)
{
}
Publication::Publication(const Options& options, CollectionOutcome& outcome, const step_source::InspectOutcome& source, Execution& execution)
    : m_options(options),
      m_status(outcome.status),
      m_report(outcome.report),
      m_source(source),
      m_execution(execution),
      m_collectionSink(&outcome.collection)
{
}
Publication::Publication(const Options& options, PartOutcome& outcome, const step_source::InspectOutcome& source, Execution& execution)
    : m_options(options), m_status(outcome.status), m_report(outcome.report), m_source(source), m_execution(execution), m_partSink(&outcome.part)
{
}
Publication::Publication(const Options& options, DocumentOutcome& outcome, const step_source::InspectOutcome& source, Execution& execution)
    : m_options(options),
      m_status(outcome.status),
      m_report(outcome.report),
      m_source(source),
      m_execution(execution),
      m_documentSink(&outcome.document)
{
}
Publication::Publication(const Options& options, FileOutcome& outcome, const step_source::InspectOutcome& source, Execution& execution)
    : m_options(options), m_status(outcome.status), m_report(outcome.report), m_source(source), m_execution(execution), m_fileSink(&outcome.model)
{
}
void Publication::AcceptDocument(std::shared_ptr<const DocumentSnapshot> candidate)
{
    if ((!m_documentSink && !m_fileSink) || !candidate || candidate->PartCount() != m_report.definitions.size() ||
        std::any_of(
            m_report.definitions.begin(),
            m_report.definitions.end(),
            [](const auto& definition)
            {
                return !definition.checksPassed;
            }
        ))
    {
        throw std::logic_error("CompleteDocumentPublicationRequired");
    }
    m_preparedDocument = std::move(candidate);
    FinishAccept();
}
void Publication::Accept(std::shared_ptr<ModelData> candidate)
{
    m_candidate = std::move(candidate);
    if (!m_modelSink && !m_partSink && !m_fileSink)
    {
        throw std::logic_error("SinglePublicationTargetRequired");
    }
    m_prepared = Access::Snapshot(std::move(m_candidate));
    FinishAccept();
}
void Publication::AcceptCollection(std::shared_ptr<const step_semantics::Snapshot> source, std::vector<std::shared_ptr<ModelData>> candidates)
{
    if (!m_collectionSink && !m_partSink && !m_fileSink)
    {
        throw std::logic_error("CollectionPublicationTargetRequired");
    }
    if (candidates.size() != m_report.bodies.size() || std::any_of(
                                                           m_report.bodies.begin(),
                                                           m_report.bodies.end(),
                                                           [](const auto& body)
                                                           {
                                                               return !body.checksPassed;
                                                           }
                                                       ))
    {
        m_report.candidateDiscarded = m_report.nativeCalls > 0;
        throw std::logic_error("UncheckedMaterialCollection");
    }
    m_collectionCandidates = std::move(candidates);
    try
    {
        m_preparedCollection = Access::Collection(std::move(source), std::move(m_collectionCandidates));
    }
    catch (...)
    {
        m_report.candidateDiscarded = m_report.nativeCalls > 0;
        throw;
    }
    FinishAccept();
}
void Publication::AcceptCollection(std::shared_ptr<const step_semantics::Snapshot> source, Result<MaterialCandidates> candidates)
{
    if (!candidates)
    {
        Fail(candidates.error());
        FinishFailure();
        return;
    }
    AcceptCollection(std::move(source), std::move(*candidates));
}
void Publication::AcceptDocument(Result<DocumentCandidate> candidate)
{
    if (!candidate)
    {
        Fail(candidate.error());
        FinishFailure();
        return;
    }
    AcceptDocument(std::move(*candidate));
}
void Publication::FinishAccept()
{
    m_execution.CheckRepairCompletion();
    m_execution.Check(Phase::NativeReady);
    // Complete checks do not authenticate an assumed source schema or carrier.
    // Choose this result before either report and final arbitration; after
    // Commit the sole remaining action remains the owning transfer.
    bool interpreted = false;
    bool isolated = false;
    {
        // Release this temporary source owner before the commit region. The
        // prepared model remains the authoritative owner of both assumptions.
        const auto semantic = m_preparedDocument   ? m_preparedDocument->Source()->Part(0) :
                              m_preparedCollection ? m_preparedCollection->Source() :
                                                     m_prepared->Source();
        interpreted = HasAppliedSourceInterpretation(*semantic);
        isolated = semantic->SourceSelection() && !semantic->SourceSelection()->failures.empty();
    }
    bool repaired = m_prepared && m_prepared->Repairs().has_value();
    repaired = repaired || (m_prepared && m_prepared->CylindricalBandRepairs().has_value());
    if (m_preparedCollection)
    {
        for (std::size_t i = 0; i < m_preparedCollection->BodyCount(); ++i)
        {
            repaired = repaired || m_preparedCollection->Body(i)->Repairs().has_value();
            repaired = repaired || m_preparedCollection->Body(i)->CylindricalBandRepairs().has_value();
        }
    }
    if (m_preparedDocument)
    {
        for (std::size_t i = 0; i < m_preparedDocument->BodyCount(); ++i)
        {
            repaired = repaired || m_preparedDocument->Body(i)->Repairs().has_value();
            repaired = repaired || m_preparedDocument->Body(i)->CylindricalBandRepairs().has_value();
        }
    }
    m_status = repaired    ? Status::AcceptedWithRepairs :
               isolated    ? Status::AcceptedWithSourceIsolation :
               interpreted ? Status::AcceptedUnderInterpretation :
                             Status::Accepted;
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
    if (m_fileSink)
    {
        static_assert(std::is_nothrow_assignable_v<FileModel&, decltype(m_prepared)>);
        static_assert(std::is_nothrow_assignable_v<FileModel&, decltype(m_preparedCollection)>);
        static_assert(std::is_nothrow_assignable_v<FileModel&, decltype(m_preparedDocument)>);
        if (m_preparedDocument)
        {
            *m_fileSink = std::move(m_preparedDocument);
        }
        else if (m_preparedCollection)
        {
            *m_fileSink = std::move(m_preparedCollection);
        }
        else
        {
            *m_fileSink = std::move(m_prepared);
        }
    }
    else if (m_documentSink)
    {
        *m_documentSink = std::move(m_preparedDocument);
    }
    else if (m_partSink)
    {
        static_assert(std::is_nothrow_assignable_v<PartModel&, decltype(m_prepared)>);
        static_assert(std::is_nothrow_assignable_v<PartModel&, decltype(m_preparedCollection)>);
        if (m_preparedCollection)
        {
            *m_partSink = std::move(m_preparedCollection);
        }
        else
        {
            *m_partSink = std::move(m_prepared);
        }
    }
    else if (m_collectionSink)
    {
        *m_collectionSink = std::move(m_preparedCollection);
    }
    else
    {
        *m_modelSink = std::move(m_prepared);
    }
}
void Publication::WriteReport()
{
    Serialize(
        m_status,
        m_report,
        m_options.quality,
        m_source,
        std::min(m_options.nativeLimits.reportBytes, m_source.limits.reportBytes),
        m_report.json
    );
}
void Publication::Fail(const Failure& failure)
{
    m_status = failure.status;
    Diagnostic(failure);
}
void Publication::FinishFailure()
{
    // Preserve the existing disposal-before-report ordering and any discard
    // evidence already recorded by a builder that failed before handoff.
    const bool held = bool(m_candidate) || bool(m_prepared) || bool(m_preparedCollection) || bool(m_preparedDocument) ||
                      !m_collectionCandidates.empty();
    m_preparedDocument.reset();
    m_prepared.reset();
    m_preparedCollection.reset();
    m_collectionCandidates.clear();
    m_candidate.reset();
    for (auto& definition : m_report.definitions)
    {
        if (definition.checks && definition.checks->nativeCalls)
        {
            definition.checks->candidateDiscarded = true;
        }
        if (definition.checks)
        {
            for (auto& body : definition.checks->bodies)
            {
                if (body.checks && body.checks->nativeCalls)
                {
                    body.checks->candidateDiscarded = true;
                }
            }
        }
    }
    for (auto& body : m_report.bodies)
    {
        if (body.checks && body.checks->nativeCalls)
        {
            body.checks->candidateDiscarded = true;
        }
    }
    auto& report = m_report;
    report.candidateDiscarded = report.candidateDiscarded || held;
    try
    {
        WriteReport();
    }
    catch (const Failure& failure)
    {
        // A bounded report failure cannot undo observed cancellation, and no
        // partial JSON may escape even when the failure report also overflows.
        if (m_status != Status::Cancelled)
        {
            m_status = failure.status;
        }
        Diagnostic(failure);
        report.json.clear();
    }
}
void Publication::Diagnostic(const Failure& failure)
{
    AddFailureDiagnostic(m_report, failure);
}
void AddFailureDiagnostic(Report& report, const Failure& failure)
{
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
    if (failure.status == Status::PostcheckFailure && failure.nativeKind == NativeFailureKind::FaceLoopOrientation)
    {
        diagnostic.classification = "NativeFaceLoopOrientationFailure";
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
