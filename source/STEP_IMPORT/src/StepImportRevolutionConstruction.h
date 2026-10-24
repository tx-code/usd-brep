// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <memory>

class SmSurface;
namespace step_import::detail::construction
{
class GeometryConstruction;
// Build the source generator/axis definition. No source/native UV identity is
// assumed, and no fitting, topology, repair or model publication occurs here.
[[nodiscard]] Result<std::unique_ptr<SmSurface>> MakeSourceRevolution(const GeometryConstruction& inputs, std::size_t surface, std::size_t use);
} // namespace step_import::detail::construction
