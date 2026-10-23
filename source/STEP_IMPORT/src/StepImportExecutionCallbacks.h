// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <functional>

namespace step_import::detail
{
// Synchronous safe-boundary callbacks. A failure must propagate to the request
// owner before any subsequent native/proof work, never be discarded by a void
// callback or converted back to an exception.
using ExecutionCheckpoint = std::function<Result<void>()>;
using NativeStep = std::function<Result<void>(std::size_t)>;
} // namespace step_import::detail
