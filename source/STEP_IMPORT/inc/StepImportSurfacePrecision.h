// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace step_import
{
struct TargetSurfacePrecisionUse
{
    std::string definitionId, shapeId, faceId, surfaceId;
};
struct TargetSurfacePrecisionRequest
{
    std::string sourceSha256;
    std::size_t maximumFaces = 0;
    // Exact numerator/positive denominator, in model mm. This is a geometry
    // calculation resolution, distinct from every topology/error/repair budget.
    std::array<std::int64_t, 2> workResolutionMm{ 0, 1 };
    std::vector<TargetSurfacePrecisionUse> faces;
};
struct TargetSurfacePrecisionAction
{
    std::size_t planIndex = 0, targetFaceIndex = 0;
    double requestedMm = 0, effectiveMm = 0, topologyFaceToleranceMm = 0;
    bool nativeChecksPassed = false;
};
struct TargetSurfacePrecisionReceipt
{
    std::string sourceSha256, qualityId;
    std::size_t maximumFaces = 0;
    std::array<std::int64_t, 2> workResolutionMm{};
    std::vector<TargetSurfacePrecisionUse> plans;
    std::vector<TargetSurfacePrecisionAction> actions;
    bool requestComplete = false;
};
} // namespace step_import
