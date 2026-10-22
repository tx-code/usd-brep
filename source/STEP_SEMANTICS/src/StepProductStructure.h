// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalog.h"

#include <functional>

namespace step_semantics
{
struct ProductStructureFailure
{
    Status status;
    Diagnostic diagnostic;
};
struct ProductStructureResult
{
    std::shared_ptr<const ProductStructure> structure;
    std::optional<ProductStructureFailure> failure;
};
// Requires the shared field/reference bindings. No source reparsing or parallel
// type interpretation; all observations keep the original source owner.
ProductStructureResult InspectProductStructure(
    const SemanticData& data,
    const std::function<void(step_source::Span)>& visit,
    bool includeSingleDefinition = false
);
} // namespace step_semantics
