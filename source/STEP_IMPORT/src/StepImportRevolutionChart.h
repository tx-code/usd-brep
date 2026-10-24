// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportExecutionCallbacks.h"

#include <SmSurface.h> // Owned support copy; complete at every result destruction site.
#include <array>
#include <memory>
#include <optional>
#include <vector>
class SmFace;
class SmAssertArray;
namespace step_import::detail
{
// Scalar part of the stored-net certificate, exposed only to contract tests.
bool ExactRevolutionChartScale(double before, double after, int exponent);
struct RevolutionChartChange
{
    int exponent = 0;
    std::array<double, 4> before{}, after{}, step{};
    double conditionBefore = 0, conditionAfter = 0;
    std::size_t exactValues = 0, trims = 0;
    std::unique_ptr<SmSurface> original;
    // Builder-only witness. Captured coefficient copies own their storage;
    // topology pointers borrow the unpublished BRep and must never escape it.
    std::function<bool()> verifyStoredIdentity;
};
// Admit only a COMPLETE finding set of small-radius failures on stored,
// face-owned derived 2D trims. Source composition UV and mixed failures refuse.
std::optional<std::vector<SmFace*>> RevolutionChartFaces(const SmAssertArray& findings);
// Private unpublished face only. Select ONE U power-of-two scale from the
// center surface metric; never choose a scale from a validator result. V,
// generator, STEP coordinates, source curves and edge t remain unchanged.
// Exact stored-net identities establish a positive affine UV bijection and
// identical lifted error function for every t, not a sampled geometry bound.
// Normal absence means inapplicable/unestablished with defining data untouched.
// Failure/cancellation after application starts can leave this PRIVATE graph
// partly updated: caller MUST propagate and discard the whole unpublished BRep,
// never fall back or publish it. There is no rollback contract.
[[nodiscard]] Result<std::optional<RevolutionChartChange>> ConditionRevolutionChart(
    SmFace& face,
    std::size_t maximumControls,
    const ExecutionCheckpoint& beforeNativeCall
);
} // namespace step_import::detail
