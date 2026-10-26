// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportUnits.h"

#include <cmath>
#include <optional>
namespace step_import::detail::construction
{
struct SourceAngleCache
{
    std::optional<double> radiansPerUnit;
};
// Preserve the established SI calculation and its observer order. Conversion
// units use the same checked nominal exact engine as length normalization, with
// a separate RADIAN terminal. A name such as 'degree' never supplies pi/180:
// the declared decimal factor is the physical definition. The owning source
// retains its chain; consumers still check their geometric/UV error budgets.
// Neither uncertainty nor a pcurve's 2D context supplies an angle unit.
inline Result<double> SourceAngleToRadians(const step_semantics::Snapshot& source, const ExecutionCheckpoint& boundary)
{
    const auto& units = source.UnitContext();
    for (const auto& unit : units.siDefinitions)
    {
        if (unit.recordIndex == units.planeAngleUnit && unit.siName == "RADIAN")
        {
            return std::pow(10., unit.canonicalPower10);
        }
    }
    auto scale = ResolvePlaneAngleScale(source, units.planeAngleUnit, boundary);
    if (!scale)
    {
        return tl::unexpected(std::move(scale.error()));
    }
    return double(scale->Numerator()) / double(scale->Denominator());
}
} // namespace step_import::detail::construction
