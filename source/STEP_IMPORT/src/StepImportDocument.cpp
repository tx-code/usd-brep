// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportDefinitionSelection.h"
#include "StepImportExecution.h"
#include "StepImportGroupedBuild.h"
#include "StepImportNativeData.h"
#include "StepImportObservation.h"
#include "StepImportPlan.h"

#include <SmBrepData.h>
#include <algorithm>

namespace step_import::detail
{
[[nodiscard]] Result<DocumentCandidate> BuildDocument(
    std::shared_ptr<const step_semantics::ProductDocument> source,
    const Options& options,
    Report& report,
    Execution& execution
)
{
    auto selected = SelectNativeDefinitions(*source, options, report, execution);
    if (!selected)
    {
        return tl::unexpected(std::move(selected.error()));
    }
    auto candidate = std::make_shared<DocumentData>();
    candidate->source = source;
    candidate->selection = report.definitionSelection;
    candidate->sourcePartIndices = *selected;
    return ObserveDefinitions(
        *source,
        *selected,
        report,
        execution,
        [&](const DefinitionMember& member, Report& checks) -> Result<void>
        {
            const auto& part = member.source;
            const auto& definition = part->PartDefinition();
            if (definition.shapes.size() > 1)
            {
                checks.materialCollection = true;
                auto bodies = BuildMaterialCollection(part, options, checks, execution);
                if (!bodies)
                {
                    return tl::unexpected(std::move(bodies.error()));
                }
                for (std::size_t k = 0; k < bodies->bodies.size(); ++k)
                {
                    candidate->bodies.push_back({ member.ordinal, member.sourceDefinitionRecord, definition.shapes[k].shape, bodies->bodies[k] });
                }
                candidate->associated.push_back(bodies->associated);
                candidate->parts.emplace_back(Access::Collection(part, std::move(*bodies)));
            }
            else
            {
                auto body = BuildGeneral(part, options, checks, execution);
                if (!body)
                {
                    return tl::unexpected(std::move(body.error()));
                }
                candidate->bodies.push_back({ member.ordinal, member.sourceDefinitionRecord, definition.shapes[0].shape, *body });
                candidate->associated.push_back(*body);
                candidate->parts.emplace_back(Access::Snapshot(std::move(*body)));
            }
            return {};
        },
        [&]() -> Result<DocumentCandidate>
        {
            return Access::Document(std::move(candidate));
        }
    );
}
std::shared_ptr<const DocumentSnapshot> Access::Document(std::shared_ptr<DocumentData> data)
{
    if (!data || !data->source || data->parts.size() != data->sourcePartIndices.size() || data->parts.size() != data->associated.size() ||
        data->parts.empty() || data->parts.size() > data->source->PartCount() || data->bodies.empty() ||
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
    std::vector<const detail::ModelData*> associated, bodies;
    for (const auto& definition : m_data->associated)
    {
        associated.push_back(definition.get());
    }
    for (const auto& body : m_data->bodies)
    {
        bodies.push_back(body.model.get());
    }
    detail::WritePartArchive(associated, bodies, path);
}

} // namespace step_import
