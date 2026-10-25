// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAssemblyDefinitions.h"
#include "StepProductStructure.h"

#include <map>

namespace step_semantics
{
struct IndependentProductScope
{
    std::size_t definition = 0;
    bool placementOnly = false;
    bool wireframeOnly = false;
    std::map<std::size_t, std::vector<std::size_t>> populations;
};
struct IndependentProductPlan
{
    std::vector<IndependentProductScope> parts;
    std::optional<ProductStructureFailure> failure;
};
// Candidate scopes only. Every selected definition still runs the established
// part/region/unit predicates, followed by one whole-document closure proof.
IndependentProductPlan PlanIndependentProducts(
    const SemanticData& data,
    const ProductStructure& graph,
    const std::function<void(step_source::Span)>& visit
);
IndependentProductPlan PlanAssemblyProductChecks(
    const SemanticData& data,
    const AssemblyDefinitionPlan& plan,
    const std::function<void(step_source::Span)>& visit
);
} // namespace step_semantics
