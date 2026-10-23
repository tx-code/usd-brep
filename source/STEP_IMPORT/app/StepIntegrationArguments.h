// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <charconv>
#include <cmath>
#include <stdexcept>

namespace step_import_app
{
inline bool IntegrationArgument(std::string_view flag, std::string_view value, step_import::Options& options)
{
    if (flag != "--integration-relative-accuracy")
    {
        return false;
    }
    double accuracy = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), accuracy);
    // Match NumericalIntegrationSettings without clamping or treating this
    // dimensionless stopping control as a geometric/repair tolerance.
    if (options.numericalIntegration || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !std::isfinite(accuracy) ||
        accuracy < 1e-8 || accuracy > 1e-4)
    {
        throw std::invalid_argument("One finite integration relative accuracy in [1e-8,1e-4] required");
    }
    options.numericalIntegration = step_import::NumericalIntegrationSettings{ accuracy };
    return true;
}
} // namespace step_import_app
