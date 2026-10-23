// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

namespace step_import::detail
{
class Execution;
// Requires the complete qualified source document, including inverse ownership
// and unit/structure checks. This does not recover or exclude source errors.
Result<std::vector<std::size_t>> SelectNativeDefinitions(
    const step_semantics::ProductDocument& source,
    const Options& options,
    Report& report,
    Execution& execution
);
} // namespace step_import::detail
