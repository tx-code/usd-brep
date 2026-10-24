// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <memory>
class SmSurface;
namespace step_import::detail::construction
{
class GeometryConstruction;
// Shared exact definition factory for face supports and source pcurve supports.
[[nodiscard]] Result<std::unique_ptr<SmSurface>> MakeSourceSplineSurface(const GeometryConstruction& inputs, std::size_t geometry, std::size_t use);
} // namespace step_import::detail::construction
