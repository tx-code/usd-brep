// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyInputsInternal.h"

#include <stdexcept>
#include <utility>

namespace step_semantics
{
AssemblyInputs::AssemblyInputs(std::shared_ptr<const AssemblyInputData> data) : m_data(std::move(data))
{
    if (!m_data || !m_data->bindings || m_data->contexts.empty() || m_data->contextIndices.size() != m_data->contexts.size())
    {
        throw std::invalid_argument("Assembly inputs require complete bindings and context observations");
    }
}
const AssemblyBindingPlan& AssemblyInputs::Bindings() const
{
    return *m_data->bindings;
}
const SourceBindings& AssemblyInputs::SourceFields() const
{
    return m_data->bindings->structure->Bindings();
}
std::span<const AssemblyContextUnits> AssemblyInputs::Contexts() const
{
    return m_data->contexts;
}
const AssemblyContextUnits* AssemblyInputs::FindContext(std::size_t context) const
{
    const auto found = m_data->contextIndices.find(context);
    return found == m_data->contextIndices.end() ? nullptr : &m_data->contexts.at(found->second);
}
std::span<const Part> AssemblyInputs::DefinitionChecks() const
{
    return m_data->definitionChecks;
}
} // namespace step_semantics
