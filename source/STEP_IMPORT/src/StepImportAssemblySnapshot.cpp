// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportAssemblySnapshot.h"

#include "StepImportNativeData.h"
#include "StepImportPlan.h"

#include <stdexcept>

namespace step_import::detail
{
std::shared_ptr<const AssemblySnapshot> AssemblyAccess::Seal(AssemblyNativeCandidate candidate, std::shared_ptr<const AssemblyEvidence> evidence)
{
    if (!candidate.source || !evidence || candidate.definitions.size() != evidence->definitions.size())
    {
        throw std::logic_error("Complete matching assembly evidence required");
    }
    auto data = std::make_shared<AssemblySnapshotData>();
    data->native = std::move(candidate);
    data->evidence = std::move(evidence);
    for (std::size_t i = 0; i < data->native.definitions.size(); ++i)
    {
        if (!data->definitionIndices.emplace(data->native.definitions[i].sourceDefinition, i).second)
        {
            throw std::logic_error("Duplicate native assembly definition");
        }
    }
    for (const auto& node : data->native.instances.instances)
    {
        if (!data->definitionIndices.contains(node.definition))
        {
            throw std::logic_error("Missing native instance definition");
        }
    }
    return std::shared_ptr<const AssemblySnapshot>(new AssemblySnapshot(std::move(data)));
}
} // namespace step_import::detail
namespace step_import
{
AssemblySnapshot::AssemblySnapshot(std::shared_ptr<const detail::AssemblySnapshotData> data) : m_data(std::move(data))
{
}
std::size_t AssemblySnapshot::DefinitionCount() const
{
    return m_data->native.definitions.size();
}
std::string AssemblySnapshot::SourceDefinitionId(std::size_t definition) const
{
    return m_data->native.source->Inputs().SourceFields().Source().Records().at(m_data->native.definitions.at(definition).sourceDefinition).id;
}
std::string AssemblySnapshot::DefinitionRole(std::size_t definition) const
{
    return m_data->evidence->definitions.at(definition).role;
}
std::size_t AssemblySnapshot::BodyCount(std::size_t definition) const
{
    return m_data->native.definitions.at(definition).bodies.size();
}
std::shared_ptr<const ModelSnapshot> AssemblySnapshot::Body(std::size_t definition, std::size_t body) const
{
    return detail::Access::Snapshot(m_data->native.definitions.at(definition).bodies.at(body));
}
std::shared_ptr<const WireframeModelSnapshot> AssemblySnapshot::WireframeModel(std::size_t definition) const
{
    const auto& data = m_data->native.definitions.at(definition).wireframe;
    return data ? std::shared_ptr<const WireframeModelSnapshot>(new WireframeModelSnapshot(data)) : nullptr;
}
WireframeModelSnapshot::WireframeModelSnapshot(std::shared_ptr<detail::ModelData> data) : m_data(std::move(data))
{
    if (!m_data || m_data->brep || !m_data->source || m_data->source->PartDefinition().shapeRole != "WireframeModel" || !m_data->context ||
        !m_data->lengthUnits || m_data->wireframes.empty())
    {
        throw std::logic_error("Wireframe snapshot requires complete independent geometry ownership");
    }
}
std::shared_ptr<const step_semantics::Snapshot> WireframeModelSnapshot::Source() const
{
    return m_data->source;
}
UnitNormalization WireframeModelSnapshot::LengthUnits() const
{
    return *m_data->lengthUnits;
}
std::vector<WireframeView> WireframeModelSnapshot::Wireframes() const
{
    std::vector<WireframeView> result;
    result.reserve(m_data->wireframes.size());
    for (std::size_t i = 0; i < m_data->wireframes.size(); ++i)
    {
        result.push_back(WireframeView(m_data, i));
    }
    return result;
}
void WireframeModelSnapshot::WriteNative(const std::filesystem::path& path) const
{
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard modelLock(m_data->mutex);
    detail::WriteWireframeArchive(*m_data, path);
}
std::size_t AssemblySnapshot::InstanceCount() const
{
    return m_data->native.instances.instances.size();
}
AssemblyInstanceInfo AssemblySnapshot::Instance(std::size_t index) const
{
    const auto& node = m_data->native.instances.instances.at(index);
    AssemblyInstanceInfo result{
        m_data->definitionIndices.at(node.definition), node.rootIndex, node.depth, node.parentIndex, {}, node.rotationToRoot, node.translationToRootMm
    };
    if (node.usage)
    {
        result.sourceUsageId = m_data->native.source->Inputs().SourceFields().Source().Records().at(*node.usage).id;
    }
    return result;
}
std::shared_ptr<const step_semantics::AssemblyDocument> AssemblySnapshot::Source() const
{
    return m_data->native.source;
}
} // namespace step_import
