// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyInputsInternal.h"

#include <stdexcept>
#include <utility>

namespace step_semantics
{
AssemblyInputs::AssemblyInputs(std::shared_ptr<const AssemblyInputData> data) : m_data(std::move(data))
{
    if (!m_data || !m_data->bindings || m_data->contexts.empty())
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
} // namespace step_semantics
