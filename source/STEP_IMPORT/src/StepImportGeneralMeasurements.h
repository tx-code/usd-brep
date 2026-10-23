// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"
#include "StepImportExecutionCallbacks.h"

#include <SmTypes.h>

namespace step_import::detail
{
struct ModelData;

struct GeneralFaceIntegral
{
    double areaMm2 = 0;
    Point areaFirstMomentMm3{}; // About areaOriginMm, not the common volume origin.
    Point areaOriginMm{};
};

struct GeneralSolidIntegrals
{
    double areaMm2 = 0, volumeMm3 = 0;
    Point centroidMm{};
    std::vector<GeneralFaceIntegral> faces; // Same order as ModelData::faces.
};

// Internal general-development measurement policy. The default retains 1e-4;
// explicit requests tighten its numerical stopping control, without clamping.
// This is not a production quality default or an error bound.
// The caller owns/serializes the unpublished, qualified single-material model.
// beforeFace must charge/check cancellation before each fused native face query;
// its errors and exceptions propagate. No output is assigned until every integral succeeds.
[[nodiscard]] Result<SmStatus> IntegrateGeneralSolid(
    const ModelData& model,
    const Point& volumeOriginMm,
    const NativeStep& beforeFace,
    GeneralSolidIntegrals& output,
    double relativeAccuracy = 1e-4
);
} // namespace step_import::detail
