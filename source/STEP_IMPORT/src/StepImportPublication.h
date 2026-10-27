// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportAssemblySnapshot.h"
#include "StepImportGroupedBuild.h"

namespace step_import::detail
{
// One request's unpublished candidate and mandatory report. Execution remains
// the sole cancellation/success arbiter; this owner only orders its checkpoints.
class Publication
{
public:

    Publication(const Options& options, Outcome& outcome, const step_source::InspectOutcome& source, Execution& execution);
    Publication(const Options& options, CollectionOutcome& outcome, const step_source::InspectOutcome& source, Execution& execution);
    Publication(const Options& options, PartOutcome& outcome, const step_source::InspectOutcome& source, Execution& execution);
    Publication(const Options& options, DocumentOutcome& outcome, const step_source::InspectOutcome& source, Execution& execution);
    Publication(const Options& options, FileOutcome& outcome, const step_source::InspectOutcome& source, Execution& execution);
    Publication(const Options& options, AssemblyOutcome& outcome, const step_source::InspectOutcome& source, Execution& execution);
    [[nodiscard]] Result<void> AcceptAssembly(Result<AssemblyNativeCandidate> candidate);
    Publication(const Publication&) = delete;
    Publication& operator=(const Publication&) = delete;

    [[nodiscard]] Result<void> Accept(std::shared_ptr<ModelData> candidate);
    [[nodiscard]] Result<void> Accept(Result<NativeCandidate> candidate);
    [[nodiscard]] Result<void> AcceptCollection(std::shared_ptr<const step_semantics::Snapshot> source, MaterialCandidates candidates);
    [[nodiscard]] Result<void> AcceptDocument(std::shared_ptr<const DocumentSnapshot> candidate);
    [[nodiscard]] Result<void> AcceptCollection(std::shared_ptr<const step_semantics::Snapshot> source, Result<MaterialCandidates> candidates);
    [[nodiscard]] Result<void> AcceptDocument(Result<DocumentCandidate> candidate);
    void Fail(const Failure& failure);
    void FinishFailure();

private:

    const Options& m_options;
    Status& m_status;
    Report& m_report;
    const step_source::InspectOutcome& m_source;
    Execution& m_execution;
    PartModel* m_partSink = nullptr;
    FileModel* m_fileSink = nullptr;
    std::shared_ptr<const ModelSnapshot>* m_modelSink = nullptr;
    std::shared_ptr<const MaterialCollectionSnapshot>* m_collectionSink = nullptr;
    std::shared_ptr<const DocumentSnapshot>* m_documentSink = nullptr;
    std::shared_ptr<const DocumentSnapshot> m_preparedDocument;
    std::shared_ptr<const AssemblySnapshot>* m_assemblySink = nullptr;
    std::shared_ptr<const AssemblySnapshot> m_preparedAssembly;
    std::shared_ptr<const AssemblyEvidence> m_assemblyEvidence; // Never owns native bodies.
    MaterialCandidates m_collectionCandidates;
    std::shared_ptr<const MaterialCollectionSnapshot> m_preparedCollection;
    std::shared_ptr<ModelData> m_candidate;
    std::shared_ptr<const ModelSnapshot> m_prepared;

    [[nodiscard]] Result<void> FinishAccept();
    [[nodiscard]] Result<void> WriteReport();
    void Diagnostic(const Failure& failure);
};
} // namespace step_import::detail
