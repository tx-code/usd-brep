// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAssemblyInputs.h"

namespace step_semantics
{
struct AssemblyDefinitionScope
{
    std::size_t definition, rootRepresentation, context;
    std::vector<std::size_t> representations, relationships, directItems;
    bool hasOccurrences = false;
};
struct AssemblyDefinitionPlan
{
    std::shared_ptr<const AssemblyInputs> inputs;
    std::vector<AssemblyDefinitionScope> definitions;
};
struct AssemblyDefinitionOutcome
{
    Status status = Status::Unsupported;
    std::shared_ptr<const AssemblyDefinitionPlan> plan;
    std::vector<Diagnostic> diagnostics;
    std::size_t dependencyVisits = 0;
};
// Partition same-definition shape relationships while keeping occurrence links
// in the owning input graph. Does not qualify geometry, material role or complete
// document closure; item identities are never discarded based on their type.
AssemblyDefinitionOutcome PlanAssemblyDefinitions(std::shared_ptr<const AssemblyInputs> inputs, Limits limits);
} // namespace step_semantics
