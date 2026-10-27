// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <functional>
#include <optional>
#include <span>

class SmCone;
class SmCircle;
class SmCurve;
class SmSurface;

namespace step_import::detail
{
struct ConicalApexProofLimits
{
    Fraction maximumDisplacementMm, maximumWorkToleranceMm, definitionMm;
    std::size_t maximumControls = 0;
};
struct ConicalApexProofCallbacks
{
    std::function<Result<void>()> beforeNative;
    std::function<Result<void>(std::size_t)> chargeWork;
};
struct ConicalApexPointProof
{
    Point originalMm, correctedMm;
    double displacementUpperMm = 0, localProtectionLowerMm = 0;
};
// Only a numerical/clearance plan, NEVER mutation or import qualification.
// Inputs are actual stored native definitions in mm. The caller must separately
// bind the complete original-source use set and selected body, retain the strict
// failure, account for request-wide movement, and validate the fresh candidate.
// Pass ALL other original/target vertices, source curves and nonparticipating
// physical faces; the participating cone itself is excluded explicitly. No
// samples, mesh or guessed fallback clearance can establish the 1% protection.
// A zero move is normal absence; unknown geometry/clearance is a diagnostic error.
[[nodiscard]] Result<std::optional<ConicalApexPointProof>> ProveConicalApexPointCorrection(
    const Point& originalMm,
    const SmCone& cone,
    const SmCircle& ring,
    std::span<const Point> otherVerticesMm,
    std::span<const SmCurve* const> sourceCurves,
    std::span<const SmSurface* const> nonparticipatingFaces,
    const ConicalApexProofLimits& limits,
    const ConicalApexProofCallbacks& callbacks
);
} // namespace step_import::detail
