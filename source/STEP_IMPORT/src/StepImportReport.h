// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

namespace step_import::detail
{
// Private report composition result. The shared source writer owns bounded
// buffer publication; only its complete output may reach the import report.
Result<void> Serialize(
    Status status,
    const Report& report,
    const Quality& quality,
    const step_source::InspectOutcome& source,
    std::size_t limit,
    std::string& output
);
} // namespace step_import::detail
