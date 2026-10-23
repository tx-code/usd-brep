// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <SmMessages.h>
#include <SmTypes.h>
#include <string>
#include <utility>

namespace step_import::detail
{
enum class NativeStatusOrigin
{
    PlanarConstruction,
    GeneralConstruction
};
// Native/measurement operation failures are not completed strict-attempt
// contract observations and never carry an orientation-repair witness.
[[nodiscard]] inline Failure NativeConstructionFailure(std::string message)
{
    return Failure{
        Status::BuildFailure, "NativeReturnOrConstructionFailure", std::move(message), {}, "D5", NativeFailureKind::ConstructionOperation
    };
}
// Preserve the former importer exception diagnostic exactly. A failed native
// status is neither a source rejection nor a configured quota. In particular,
// do not add a source record/stage that the original diagnostic did not carry.
[[nodiscard]] inline Result<void> CheckNativeStatus(SmStatus status, NativeStatusOrigin origin)
{
    if (status == SM_SUCCESS)
    {
        return {};
    }
    const char* prefix = origin == NativeStatusOrigin::GeneralConstruction ? "GeneralNativeReturn:" : "NativeReturnFailure:";
    return tl::unexpected(NativeConstructionFailure(prefix + std::to_string(status)));
}
} // namespace step_import::detail
