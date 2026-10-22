// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepProductStructure.h"

namespace step_semantics
{
// Run only after all original candidates completed ownership, unit and metadata
// checks. No candidate may be filtered before this proof. The function retains
// original facts and replaces candidate parts with qualified selected views only
// after both selected influence and the complete incoming boundary are proved.
std::optional<ProductStructureFailure> QualifySelectedSource(
    SemanticData& data,
    const ProductStructure& structure,
    std::span<const std::vector<std::size_t>> outgoing,
    const std::function<void(step_source::Span)>& visit
);
} // namespace step_semantics
