// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"
#include "StepImportTerminalSpline.h"

#include <functional>
#include <span>
class SmFace;
class SmVertex;
namespace step_import::detail
{
struct RevolutionPoleVisitEvidence
{
    std::size_t mask = 0, checkedEdges = 0, patches = 0;
    std::size_t composedEdges = 0, composedRegions = 0;
    double radiusMm = 0;
};
// Private constructed graph only. Read stored defining curve coefficients,
// never fitted caches of an arbitrary derived SmCurve. Native face-zone pole
// visits must connect only to authorized original topology endpoints. This is
// evidence about the actual target curves, not source exact-pole semantics or
// absence of all possible interior surface singularities. No graph mutation.
[[nodiscard]] Result<RevolutionPoleVisitEvidence> CheckRevolutionPoleVisits(
    SmFace& face,
    std::span<SmVertex* const> sourceVertices,
    const terminal_spline::Limits& limits,
    const terminal_spline::Charge& work,
    const std::function<Result<void>()>& nativeStep
);
} // namespace step_import::detail
