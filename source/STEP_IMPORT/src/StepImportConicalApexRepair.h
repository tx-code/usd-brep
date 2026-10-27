// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportOrientationRepair.h"

namespace step_import::detail
{
using VertexPointOverrides = std::vector<std::pair<std::size_t, Point>>; // Immutable source record index -> independent target point, in mm.
using ConicalApexRepairBuild = std::function<ConstructionResult<NativeCandidate>(Report&, bool, const VertexPointOverrides&)>;
[[nodiscard]] Result<NativeCandidate> BuildWithConicalApexRepair(
    std::shared_ptr<const step_semantics::Snapshot> source,
    std::optional<step_semantics::Part::Shape> binding,
    const Options& options,
    Report& report,
    Execution& execution,
    const ConicalApexRepairBuild& build
);
} // namespace step_import::detail
