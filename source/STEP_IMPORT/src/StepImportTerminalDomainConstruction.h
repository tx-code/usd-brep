// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <memory>
#include <optional>
class SmSurface;
namespace step_import::detail::construction
{
class GeometryConstruction;
struct TerminalDomainCache;
std::shared_ptr<TerminalDomainCache> MakeTerminalDomainCache();
// Absence means the immutable request has no plan for this basis. A selected
// plan either returns its fully checked private definition or its real failure.
[[nodiscard]] Result<std::optional<std::unique_ptr<SmSurface>>> TryTerminalDomainSurface(
    const GeometryConstruction& inputs,
    std::size_t surface,
    std::size_t use,
    double angularStartDegrees = 0
);
} // namespace step_import::detail::construction
