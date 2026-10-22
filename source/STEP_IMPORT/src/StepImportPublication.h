// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportPlan.h"

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
    Publication(const Publication&) = delete;
    Publication& operator=(const Publication&) = delete;

    void Accept(std::shared_ptr<ModelData> candidate);
    void AcceptCollection(std::shared_ptr<const step_semantics::Snapshot> source, std::vector<std::shared_ptr<ModelData>> candidates);
    void Fail(const Failure& failure);
    void FinishFailure();

private:

    const Options& m_options;
    Status& m_status;
    Report& m_report;
    const step_source::InspectOutcome& m_source;
    Execution& m_execution;
    PartModel* m_partSink = nullptr;
    std::shared_ptr<const ModelSnapshot>* m_modelSink = nullptr;
    std::shared_ptr<const MaterialCollectionSnapshot>* m_collectionSink = nullptr;
    std::vector<std::shared_ptr<ModelData>> m_collectionCandidates;
    std::shared_ptr<const MaterialCollectionSnapshot> m_preparedCollection;
    std::shared_ptr<ModelData> m_candidate;
    std::shared_ptr<const ModelSnapshot> m_prepared;

    void FinishAccept();
    void WriteReport();
    void Diagnostic(const Failure& failure);
};
} // namespace step_import::detail
