// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <bit>
#include <cfenv>
#include <cstdint>
#include <limits>
namespace step_import::detail
{
// Outward-rounded interval arithmetic needs nearest binary64 operations and
// gradual underflow. Integer bit tests also reject x86 DAZ/FTZ independently
// of the ambient library's floating comparisons.
inline bool SurfaceProofNumericEnvironment()
{
    if (!std::numeric_limits<double>::is_iec559 || std::fegetround() != FE_TONEAREST)
    {
        return false;
    }
    volatile double tiny = std::numeric_limits<double>::denorm_min();
    volatile double twice = tiny + tiny;
    volatile double normal = std::numeric_limits<double>::min();
    volatile double third = normal / 3.;
    return std::bit_cast<std::uint64_t>(double(twice)) == 2 && std::bit_cast<std::uint64_t>(double(third)) == std::uint64_t(0x0005555555555555);
}
} // namespace step_import::detail
