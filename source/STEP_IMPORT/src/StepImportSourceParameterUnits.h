// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepSemantics.h"

#include <cmath>
#include <optional>
namespace step_import::detail::construction
{
// Native development parameter policy: only the already selected nominal SI
// RADIAN definition supplies this scale. Absence is unsupported by the caller;
// neither uncertainty nor a pcurve's 2D context supplies a physical angle unit.
inline std::optional<double> SourceSiAngleToRadians(const step_semantics::Units& units)
{
    std::optional<double> result;
    for (const auto& unit : units.siDefinitions)
    {
        if (unit.recordIndex == units.planeAngleUnit && unit.siName == "RADIAN")
        {
            result = std::pow(10., unit.canonicalPower10);
        }
    }
    return result;
}
} // namespace step_import::detail::construction
