// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportNativeBuild.h"

#include <variant>
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
// Only the strict repair attempt may retain a native candidate as evidence.
// Keep this owning channel separate from ordinary business/execution failures;
// converting it at the outer build boundary releases its owner before reporting.
using ConstructionFailure = std::variant<Failure, OrientationFailureCandidate>;
static_assert(std::is_nothrow_move_constructible_v<ConstructionFailure>);
template <class T>
using ConstructionResult = tl::expected<T, ConstructionFailure>;
using GeneralRepairBuild = std::function<ConstructionResult<NativeCandidate>(Report&, bool, const FaceSenseOverrides&)>;
[[nodiscard]] Result<NativeCandidate> FinishConstructionAttempt(ConstructionResult<NativeCandidate> attempt);
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
