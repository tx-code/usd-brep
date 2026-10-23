// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalog.h"

#include <iterator>
#include <stdexcept>
#include <utility>

namespace step_semantics
{
bool IsBoundType(const SemanticData& data, std::size_t record, std::string_view type)
{
    const auto id = Schema(data).Find(type);
    if (!id)
    {
        throw std::invalid_argument("Unregistered source type query");
    }
    return data.types.at(record).test(*id);
}
std::size_t ResolveBoundReference(const SemanticData& data, const step_source::Value& value)
{
    if (value.kind != step_source::ValueKind::Reference)
    {
        throw std::invalid_argument("Source slot is not an explicit reference");
    }
    const auto found = data.referenceTargets.find(&value);
    if (found == data.referenceTargets.end())
    {
        throw std::invalid_argument("Reference value is not owned by this source snapshot");
    }
    return found->second;
}
SourceBindings::SourceBindings(std::shared_ptr<const SemanticData> data) : m_data(std::move(data))
{
    if (!m_data || !m_data->source || !m_data->bindingsComplete || m_data->fields.size() != m_data->source->Records().size())
    {
        throw std::invalid_argument("Source bindings require complete typed source fields");
    }
}
const step_source::SourceSnapshot& SourceBindings::Source() const
{
    return *m_data->source;
}
const QualifiedSchemaProfile& SourceBindings::SelectedProfile() const
{
    return m_data->profile;
}
bool SourceBindings::IsA(std::size_t record, std::string_view type) const
{
    return IsBoundType(*m_data, record, type);
}
FieldView SourceBindings::Field(std::size_t record, std::string_view name) const
{
    return FieldView(m_data, record, FindFieldBinding(*m_data, record, name));
}
FieldView SourceBindings::Field(std::size_t record, std::string_view declaredIn, std::string_view name) const
{
    return FieldView(m_data, record, FindFieldBinding(*m_data, record, name, declaredIn));
}
std::size_t SourceBindings::ReferenceTarget(const step_source::Value& value) const
{
    return ResolveBoundReference(*m_data, value);
}

std::size_t FindFieldBinding(const SemanticData& data, std::size_t record, std::string_view name, std::optional<std::string_view> declaredIn)
{
    if (!data.bindingsComplete)
    {
        throw std::logic_error("Semantic field bindings are not complete");
    }
    const auto& bound = data.fields.at(record);
    const auto matches = bound.named.equal_range(name);
    if (!declaredIn)
    {
        if (matches.first == matches.second || std::next(matches.first) != matches.second)
        {
            throw std::invalid_argument("Missing or ambiguous source field");
        }
        return matches.first->second;
    }
    std::optional<std::size_t> found;
    for (auto at = matches.first; at != matches.second; ++at)
    {
        if (bound.attributes[at->second].declaration.declaredIn == *declaredIn)
        {
            if (found)
            {
                throw std::invalid_argument("Ambiguous declared source field");
            }
            found = at->second;
        }
    }
    if (!found)
    {
        throw std::invalid_argument("Missing declared source field");
    }
    return *found;
}
} // namespace step_semantics
