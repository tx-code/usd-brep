// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportExecutionCallbacks.h"
#include "StepImportSourceFrame.h"

#include <functional>
#include <optional>
#include <span>
class SmCurve;
namespace step_import::detail
{
struct SphereChartBoundary
{
    const SmCurve* curve = nullptr;
    std::array<double, 2> interval{};
};
struct SphereHemisphereBounds
{
    // Strict lower distances in mm to the +X/-X/+Y/-Y/+Z/-Z equatorial
    // planes, on their NEGATIVE sides. Positive means the WHOLE retained
    // boundary lies away from that chart's longitude cut and both poles.
    std::array<double, 6> negativeSideLowerMm{};
    std::size_t controls = 0;
};
// Positive native control hulls are continuous enclosures, not sampled traces.
// A full-basis hull is deliberately conservative for an edge subinterval.
// Unsupported implicit curves, nonpositive weights, or an interval outside the
// stored basis give normal absence, preserving the previous construction path.
// No curve/surface/topology/owner is mutated. Callback failures propagate intact.
[[nodiscard]] Result<std::optional<SphereHemisphereBounds>> BoundSphereChartBoundary(
    std::span<const SphereChartBoundary> boundaries,
    const SourceFrame& frame,
    std::size_t maximumControls,
    const ExecutionCheckpoint& native,
    const std::function<Result<void>(std::size_t)>& work
);
// Right-handed signed permutation only; preserves nominal sphere center/radius.
SourceFrame SphereChartFrame(const SourceFrame& source, std::size_t signedXAxis);
} // namespace step_import::detail
