// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAssemblyInputs.h"
#include "StepProductStructure.h"

namespace step_semantics
{
struct PlacementRetentionResult
{
    std::optional<RetainedPlacementDeclarations> receipt;
    std::optional<ProductStructureFailure> failure;
};
// Project scope extension only: complete disconnected raw declarations remain
// source-only. No representation/part/unit membership or target geometry added.
PlacementRetentionResult ProveUnreferencedPlacements(
    const SemanticData& data,
    const std::vector<std::vector<std::size_t>>& outgoing,
    std::span<const std::size_t> records,
    const Diagnostic& original,
    const std::function<void(step_source::Span)>& visit
);
} // namespace step_semantics
