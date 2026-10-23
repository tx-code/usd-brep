// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

namespace step_import::detail
{
class Execution;
struct Plan;
using NativeCandidate = std::shared_ptr<ModelData>;
[[nodiscard]] Result<NativeCandidate> Build(const Plan& plan, const Options& options, Report& report, Execution& execution);
[[nodiscard]] Result<NativeCandidate> BuildGeneral(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const Options& options,
    Report& report,
    Execution& execution
);
[[nodiscard]] Result<NativeCandidate> BuildGeneralBody(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const step_semantics::Part::Shape& binding,
    const Options& options,
    Report& report,
    Execution& execution
);
} // namespace step_import::detail
