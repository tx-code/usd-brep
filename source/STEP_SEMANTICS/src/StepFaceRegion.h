// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepSemantics.h"

namespace step_semantics
{
// Fixed AP214 FACE.WR2 counts nominal FACE_OUTER_BOUND uses. This source
// predicate does not classify an unmarked region or alter either orientation.
// Callers own visit accounting, diagnostic policy and explicit repair deferrals.
inline std::size_t SourceOuterBoundCount(const SourceBindings& source, std::size_t face)
{
    std::size_t count = 0;
    for (const auto& bound : source.Field(face, "bounds").Value().children)
    {
        count += source.IsA(source.ReferenceTarget(bound), "FACE_OUTER_BOUND") ? 1 : 0;
    }
    return count;
}
} // namespace step_semantics
