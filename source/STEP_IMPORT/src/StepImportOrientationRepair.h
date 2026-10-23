// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportNativeBuild.h"
namespace step_import::detail
{
class Execution;
using FaceSenseOverrides = std::vector<std::pair<std::size_t, bool>>;
struct OrientationFailureCandidate
{
    Failure failure;
    std::shared_ptr<ModelData> model;
    std::vector<std::size_t> targetFaces;
};
using GeneralRepairBuild = std::function<Result<NativeCandidate>(Report&, bool, const FaceSenseOverrides&)>;
[[nodiscard]] Result<void> ValidateRepairTargets(const step_semantics::Outcome& source, const Options& options, Execution& execution);
[[nodiscard]] Result<NativeCandidate> BuildWithOrientationRepair(
    std::shared_ptr<const step_semantics::Snapshot> source,
    std::optional<step_semantics::Part::Shape> binding,
    const Options& options,
    Report& report,
    Execution& execution,
    const GeneralRepairBuild& build
);
} // namespace step_import::detail
