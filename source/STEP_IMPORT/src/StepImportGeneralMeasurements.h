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

struct GeneralSurfaceIntegrals
{
    double areaMm2 = 0;
    Point centroidMm{}; // Area centroid; no material volume is requested or inferred.
    std::vector<GeneralFaceIntegral> faces;
};

// The caller has checked the nonmaterial role and complete face inventory.
// Integrate each geometric face once, retaining the existing face-area stopping
// scale and exact trim. Publish only after all charged/cancellable queries pass.
[[nodiscard]] Result<SmStatus> IntegrateGeneralSurface(
    const ModelData& model,
    const Point& accumulationOriginMm,
    const NativeStep& beforeFace,
    GeneralSurfaceIntegrals& output,
    double relativeAccuracy = 1e-4
);

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
