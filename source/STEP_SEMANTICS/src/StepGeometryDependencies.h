// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalog.h"

#include <functional>
#include <tl/expected.hpp>

namespace step_semantics
{
// Selected AP214 source prerequisites only. Requires complete typed field bindings;
// does not evaluate geometry, apply units, or grant full entity/RULE qualification.
struct GeometryDependencyFailure
{
    Status status;
    Diagnostic diagnostic;
};
using GeometryDependencyResult = tl::expected<void, GeometryDependencyFailure>;
GeometryDependencyResult CheckAp214GeometryDependencies(
    const SemanticData& data,
    const std::function<void(step_source::Span)>& visit,
    GeometryDependencyCounts& checks
);
} // namespace step_semantics
