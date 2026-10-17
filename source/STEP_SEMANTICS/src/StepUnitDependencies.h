// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalog.h"

#include <functional>
namespace step_semantics
{
struct UnitDependencyFailure
{
    Status status;
    Diagnostic diagnostic;
};
// Exact source dimensions only; no unit-scale evaluation, geometry conversion,
// source rewriting or complete AP214 qualification. Typed bindings are required.
std::optional<UnitDependencyFailure> CheckAp214UnitDependencies(
    SemanticData& data,
    const std::function<void(step_source::Span)>& visit,
    UnitDependencyCounts& checks
);
} // namespace step_semantics
