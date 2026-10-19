// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <functional>
#include <optional>
class SmBSplineSurface;
namespace step_import::detail
{
struct SurfaceRestrictionProof
{
    bool established = false;
    bool workLimit = false;
    std::optional<double> maximumMm;
    std::size_t patches = 0;
};
// Compare the native binary64 definitions on the target's whole natural domain.
// Positive rational weights and clamped, compatible degrees are prerequisites.
// Outward interval common-knot refinement and Bernstein coefficient bounds are
// continuous evidence, not sampling. Uncertain arithmetic returns unestablished.
// Limits bound temporary controls and product terms; callbacks may cancel/throw.
SurfaceRestrictionProof ProveSurfaceRestriction(
    const SmBSplineSurface& original,
    const SmBSplineSurface& restricted,
    double maximumMm,
    std::size_t maximumControls,
    std::size_t maximumProducts,
    const std::function<void()>& checkpoint
);
} // namespace step_import::detail
