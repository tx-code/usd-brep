// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepDefinitionInspection.h"

#include <stdexcept>

namespace step_semantics
{
DefinitionInspection::DefinitionInspection(std::shared_ptr<const DefinitionInspectionData> data) : m_data(std::move(data))
{
    if (!m_data || !m_data->structure || m_data->checks.size() != m_data->structure->Definitions().size())
    {
        throw std::invalid_argument("Independent definition observations require the complete declared inventory");
    }
}
const ProductStructure& DefinitionInspection::Structure() const
{
    return *m_data->structure;
}
std::span<const DefinitionPrerequisiteCheck> DefinitionInspection::Checks() const
{
    return m_data->checks;
}
std::span<const Diagnostic> DefinitionInspection::UnapprovedDiagnostics() const
{
    return m_data->unapprovedDiagnostics;
}
bool DefinitionInspection::AllDefinitionsVisited() const
{
    return m_data->allDefinitionsVisited;
}
} // namespace step_semantics
