// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportDefinitionSelection.h"
#include "StepImportExecution.h"
#include "StepImportNativeData.h"

#include <SmBrepData.h>
#include <algorithm>

namespace step_import::detail
{
std::shared_ptr<const DocumentSnapshot> BuildDocument(
    std::shared_ptr<const step_semantics::ProductDocument> source,
    const Options& options,
    Report& report,
    Execution& execution
)
{
    const auto selected = SelectNativeDefinitions(*source, options, report, execution);
    auto candidate = std::make_shared<DocumentData>();
    candidate->source = source;
    candidate->selection = report.definitionSelection;
    candidate->sourcePartIndices = selected;
    // Complete-request export of detached associated definitions is not yet
    // qualified for this container. Reject before native allocations rather
    // than duplicating or dropping one part's physical auxiliary geometry.
    for (const auto i : selected)
    {
        for (const auto& role : source->Part(i)->PartDefinition().roles)
        {
            if (role.name == "wireframe_representation" || role.name == "construction_representation")
            {
                throw Failure{ Status::Unsupported,
                               "DocumentAssociatedGeometryUnimplemented",
                               "Independent document associated geometry must be preserved at document level",
                               role.recordIndex,
                               "GeneralBrep" };
            }
        }
    }
    try
    {
        for (std::size_t i = 0; i < selected.size(); ++i)
        {
            const auto part = source->Part(selected[i]);
            const auto& definition = part->PartDefinition();
            const auto owner = std::find_if(
                definition.roles.begin(),
                definition.roles.end(),
                [](const auto& r)
                {
                    return r.name == "definition";
                }
            );
            if (owner == definition.roles.end() || definition.shapes.empty())
            {
                throw std::logic_error("UnqualifiedDocumentDefinition");
            }
            auto checks = std::make_shared<Report>();
            checks->generalBrep = true;
            checks->encoding = report.encoding;
            checks->sourceEvidenceScope = "ProductDocumentRoot";
            checks->source = report.source;
            checks->sourceSha256 = report.sourceSha256;
            checks->sourceSchema = report.sourceSchema;
            checks->numericalIntegration = report.numericalIntegration;
            checks->periodicNormalizationRequested = report.periodicNormalizationRequested;
            checks->supportDomainRestrictionRequested = report.supportDomainRestrictionRequested;
            report.definitions.push_back({ owner->recordIndex, false, checks });
            const auto before = report.nativeCalls;
            try
            {
                if (definition.shapes.size() > 1)
                {
                    checks->materialCollection = true;
                    auto bodies = BuildMaterialCollection(part, options, *checks, execution);
                    for (std::size_t k = 0; k < bodies.size(); ++k)
                    {
                        candidate->bodies.push_back({ i, owner->recordIndex, definition.shapes[k].shape, bodies[k] });
                    }
                    candidate->parts.emplace_back(Access::Collection(part, std::move(bodies)));
                }
                else
                {
                    auto body = BuildGeneral(part, options, *checks, execution);
                    candidate->bodies.push_back({ i, owner->recordIndex, definition.shapes[0].shape, body });
                    candidate->parts.emplace_back(Access::Snapshot(std::move(body)));
                }
            }
            catch (...)
            {
                checks->nativeCalls = report.nativeCalls - before;
                checks->candidateDiscarded = checks->nativeCalls > 0;
                throw;
            }
            checks->nativeCalls = report.nativeCalls - before;
            report.definitions.back().checksPassed = true;
        }
        if (report.numericalIntegration)
        {
            report.numericalIntegration->completed = std::all_of(
                report.definitions.begin(),
                report.definitions.end(),
                [](const auto& part)
                {
                    return part.checks->numericalIntegration && part.checks->numericalIntegration->completed;
                }
            );
            if (report.numericalIntegration->completed)
            {
                report.numericalIntegration->appliedRelativeAccuracy = report.definitions.front()
                                                                           .checks->numericalIntegration->appliedRelativeAccuracy;
            }
        }
        return Access::Document(std::move(candidate));
    }
    catch (...)
    {
        report.candidateDiscarded = report.nativeCalls > 0;
        for (auto& part : report.definitions)
        {
            if (part.checks && part.checks->nativeCalls)
            {
                part.checks->candidateDiscarded = true;
            }
            if (part.checks)
            {
                for (auto& body : part.checks->bodies)
                {
                    if (body.checks && body.checks->nativeCalls)
                    {
                        body.checks->candidateDiscarded = true;
                    }
                }
            }
        }
        // Candidate snapshots/contexts are private and unwind before reporting.
        throw;
    }
}
std::shared_ptr<const DocumentSnapshot> Access::Document(std::shared_ptr<DocumentData> data)
{
    if (!data || !data->source || data->parts.size() != data->sourcePartIndices.size() || data->parts.empty() ||
        data->parts.size() > data->source->PartCount() || data->bodies.empty() ||
        (!data->selection && data->parts.size() != data->source->PartCount()) ||
        (data->selection && data->selection->selectedDefinitionIds.size() != data->parts.size()))
    {
        throw std::logic_error("IncompleteNativeProductDocument");
    }
    return std::shared_ptr<const DocumentSnapshot>(new DocumentSnapshot(std::move(data)));
}
} // namespace step_import::detail
namespace step_import
{
DocumentSnapshot::DocumentSnapshot(std::shared_ptr<detail::DocumentData> data) : m_data(std::move(data))
{
}
std::size_t DocumentSnapshot::PartCount() const
{
    return m_data->parts.size();
}
PartModel DocumentSnapshot::Part(std::size_t i) const
{
    return m_data->parts.at(i);
}
std::size_t DocumentSnapshot::BodyCount() const
{
    return m_data->bodies.size();
}
std::shared_ptr<const ModelSnapshot> DocumentSnapshot::Body(std::size_t i) const
{
    return detail::Access::Snapshot(m_data->bodies.at(i).model);
}
std::size_t DocumentSnapshot::BodyPartIndex(std::size_t i) const
{
    return m_data->bodies.at(i).part;
}
std::string DocumentSnapshot::SourceDefinitionId(std::size_t i) const
{
    return m_data->source->Source().Records().at(m_data->bodies.at(i).definition).id;
}
std::string DocumentSnapshot::SourceShapeId(std::size_t i) const
{
    return m_data->source->Source().Records().at(m_data->bodies.at(i).shape).id;
}
std::shared_ptr<const step_semantics::ProductDocument> DocumentSnapshot::Source() const
{
    return m_data->source;
}
std::optional<DefinitionSelectionReceipt> DocumentSnapshot::Selection() const
{
    return m_data->selection;
}
void DocumentSnapshot::WriteNative(const std::filesystem::path& path) const
{
    std::lock_guard lock(detail::NativeMutex());
    auto context = std::make_unique<SmContext>();
    std::vector<std::unique_ptr<SmBrep>> copies;
    SmTArray<SmBrep*> bodies;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    for (const auto& body : m_data->bodies)
    {
        std::lock_guard bodyLock(body.model->mutex);
        copies.emplace_back(new (*context) SmBrep(*body.model->brep, nullptr, nullptr, TRUE, FALSE));
        if (!copies.back() || copies.back()->GetContext() != context.get())
        {
            throw std::runtime_error("DocumentArchiveContextMismatch");
        }
        bodies.Add(copies.back().get());
    }
    // Ordered independent material bodies, empty CSG tree; source definition
    // identities/qualification stay with the owning import snapshot/report.
    if (SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, bodies, SM_BINARY, TRUE, FALSE) != SM_SUCCESS)
    {
        throw std::runtime_error("DocumentArchiveWriteFailed");
    }
}
} // namespace step_import
