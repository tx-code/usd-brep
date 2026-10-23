// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAssemblyInputs.h"

#include <unordered_map>

namespace step_semantics
{
struct AssemblyInputData
{
    std::shared_ptr<const AssemblyBindingPlan> bindings;
    std::vector<AssemblyContextUnits> contexts;
    std::unordered_map<std::size_t, std::size_t> contextIndices;
    std::vector<Part> definitionChecks;
};
struct AssemblyDocumentData
{
    std::shared_ptr<const SemanticData> semantic;
    std::shared_ptr<const AssemblyInputs> inputs;
    std::optional<RetainedPlacementDeclarations> retainedPlacements;
};
} // namespace step_semantics
