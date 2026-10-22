// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepSemantics.h"

namespace step_semantics
{
struct DefinitionInspectionData
{
    std::shared_ptr<const ProductStructure> structure;
    std::vector<DefinitionPrerequisiteCheck> checks;
    std::vector<Diagnostic> unapprovedDiagnostics;
    bool allDefinitionsVisited = false;
};
} // namespace step_semantics
