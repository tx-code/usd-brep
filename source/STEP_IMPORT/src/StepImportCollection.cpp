// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportAssociatedGeometry.h"
#include "StepImportExecution.h"
#include "StepImportGroupedBuild.h"
#include "StepImportNativeData.h"
#include "StepImportObservation.h"
#include "StepImportPlan.h"

#include <SmBrepData.h>
#include <algorithm>
#include <stdexcept>

namespace step_import::detail
{
[[nodiscard]] Result<MaterialCandidates> BuildMaterialCollection(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const Options& options,
    Report& report,
    Execution& execution
)
{
    const auto& part = source->PartDefinition();
    report.selectedBodyCount = part.shapes.size();
    if (part.shapes.empty() || std::any_of(
                                   part.shapes.begin(),
                                   part.shapes.end(),
                                   [](const auto& shape)
                                   {
                                       return shape.role != "MaterialSolid";
                                   }
                               ))
    {
        return tl::unexpected(
            Failure{ Status::Unsupported,
                     "MaterialCollectionRequiresSolids",
                     "Every collection member must retain a source material-solid role",
                     {},
                     "GeneralBrep" }
        );
    }
    MaterialCandidates candidates;
    auto associated = BuildPartAssociatedGeometry(source, options, report, execution);
    if (!associated)
    {
        return tl::unexpected(std::move(associated.error()));
    }
    candidates.associated = std::move(*associated);
    candidates.bodies.reserve(part.shapes.size());
    report.bodies.reserve(part.shapes.size());
    return ObserveBodies(
        *source,
        report,
        execution,
        [&](const step_semantics::Part::Shape& binding, Report& checks) -> Result<void>
        {
            auto body = BuildGeneralBody(source, binding, options, checks, execution);
            if (!body)
            {
                return tl::unexpected(std::move(body.error()));
            }
            candidates.bodies.push_back(std::move(*body));
            return {};
        },
        [&]() -> Result<MaterialCandidates>
        {
            return std::move(candidates);
        }
    );
}
std::shared_ptr<const MaterialCollectionSnapshot> Access::Collection(
    std::shared_ptr<const step_semantics::Snapshot> source,
    MaterialCandidates candidates
)
{
    auto& bodies = candidates.bodies;
    if (!source || bodies.empty() || bodies.size() != source->PartDefinition().shapes.size() ||
        std::any_of(
            bodies.begin(),
            bodies.end(),
            [&](const auto& body)
            {
                return !body || !body->brep || body->source != source;
            }
        ))
    {
        throw std::logic_error("IncompleteMaterialCollection");
    }
    auto data = std::make_shared<CollectionData>();
    data->source = std::move(source);
    data->bodies = std::move(bodies);
    data->associated = std::move(candidates.associated);
    if (HasAssociatedGeometry(*data->source) != bool(data->associated) ||
        (data->associated && (data->associated->source != data->source || data->associated->brep)))
    {
        throw std::logic_error("IncompleteCollectionAssociatedGeometry");
    }
    return std::shared_ptr<const MaterialCollectionSnapshot>(new MaterialCollectionSnapshot(std::move(data)));
}
} // namespace step_import::detail

namespace step_import
{
MaterialCollectionSnapshot::MaterialCollectionSnapshot(std::shared_ptr<detail::CollectionData> data) : m_data(std::move(data))
{
}
std::size_t MaterialCollectionSnapshot::BodyCount() const
{
    return m_data->bodies.size();
}
std::shared_ptr<const ModelSnapshot> MaterialCollectionSnapshot::Body(std::size_t index) const
{
    return detail::Access::Snapshot(m_data->bodies.at(index));
}
std::string MaterialCollectionSnapshot::SourceShapeId(std::size_t index) const
{
    return m_data->source->Source().Records().at(m_data->source->PartDefinition().shapes.at(index).shape).id;
}
std::shared_ptr<const step_semantics::Snapshot> MaterialCollectionSnapshot::Source() const
{
    return m_data->source;
}
std::vector<WireframeView> MaterialCollectionSnapshot::Wireframes() const
{
    std::vector<WireframeView> result;
    if (m_data->associated)
    {
        for (std::size_t i = 0; i < m_data->associated->wireframes.size(); ++i)
        {
            result.push_back(WireframeView(m_data->associated, i));
        }
    }
    return result;
}
std::vector<SupportingGeometryView> MaterialCollectionSnapshot::SupportingGeometry() const
{
    std::vector<SupportingGeometryView> result;
    if (m_data->associated)
    {
        for (std::size_t i = 0; i < m_data->associated->supportingGeometry.size(); ++i)
        {
            result.push_back(SupportingGeometryView(m_data->associated, i));
        }
    }
    return result;
}
void MaterialCollectionSnapshot::WriteNative(const std::filesystem::path& path) const
{
    std::lock_guard lock(detail::NativeMutex());
    std::vector<const detail::ModelData*> bodies;
    for (const auto& body : m_data->bodies)
    {
        bodies.push_back(body.get());
    }
    const std::array<const detail::ModelData*, 1> associated{ m_data->associated.get() };
    detail::WritePartArchive(associated, bodies, path);
}

} // namespace step_import
