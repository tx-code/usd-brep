// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalog.h"

#include <functional>

namespace step_semantics
{
// Selected AP214 source prerequisites only. Requires complete typed field bindings;
// does not evaluate geometry, apply units, or grant full entity/RULE qualification.
std::optional<Diagnostic> CheckAp214GeometryDependencies(
    const SemanticData& data,
    const std::function<void(step_source::Span)>& visit,
    GeometryDependencyCounts& checks
);
} // namespace step_semantics
