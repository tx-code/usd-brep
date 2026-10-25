// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportNativeBuild.h"

namespace step_import::detail
{
// Standalone geometry owner, no fabricated BRep. Uses the same source binding,
// curve/point construction module and destruction lock as associated geometry.
// Caller holds NativeMutex and owns whole-assembly report/publication decisions.
Result<NativeCandidate> BuildWireframeModel(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const Options& options,
    Report& report,
    Execution& execution
);
} // namespace step_import::detail
