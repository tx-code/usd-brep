// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportSurfaceProof.h"

#include <array>
#include <functional>
#include <string>
class SmFace;
class SmTorus;
namespace step_import::detail
{
enum class SupportDomainStatus
{
    NotEligible,
    Unestablished,
    WorkLimit,
    Normalized
};
struct SupportDomainEvidence
{
    SupportDomainStatus status = SupportDomainStatus::NotEligible;
    std::array<double, 4> original{}, requested{}, actual{}; // umin,vmin,umax,vmax, unchanged native parameters.
    SurfaceRestrictionProof numeric;
    std::array<double, 4> analyticDomainDegrees{};
    std::array<double, 2> generatorRadiusBoundsMm{};
    double minimumAbsoluteGeneratorRadiusMm = 0;
    std::string reason = "ExplicitOuterBoundOrSupportEligibility";
};
// For an unpublished face under NativeMutex only. A unique explicit outer bound,
// an open/nonperiodic chart and positive UV controls establish that the bounded
// parameter region lies inside the boundary control hull. Hole boundaries are
// retained too. No source pcurve is replaced. Geometry outside that used region
// remains in the owning source snapshot; no fit, point movement or tol growth.
// All candidate/proof/copy work precedes installation; failures/cancellation
// after installation abort the caller's whole unpublished model.
// A supplied original torus instead requires a closed planar UV chart without
// modulo jumps and a strict signed generator radius bound away from the axis.
// Its ordinary original-basis candidate may be periodic before extraction; the
// continuous comparison is against the retained original torus, not a rebuild.
SupportDomainEvidence RestrictUnusedSupportDomain(
    SmFace& face,
    bool uniqueExplicitOuter,
    double maximumMm,
    std::size_t maximumControls,
    std::size_t maximumProducts,
    const std::function<void()>& beforeNativeCall,
    const SmTorus* originalTorus = nullptr // Detached original definition retained by the unpublished model, never an unrelated support.
);
} // namespace step_import::detail
