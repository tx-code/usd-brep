// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"
#include "StepImportExecutionCallbacks.h"

#include <SmCurve.h>
#include <functional>

class SmContext;
class SmEdge;
namespace step_import::detail
{
struct RingCut
{
    double parameter = 0;
    std::size_t vertex = 0; // Target vertex ordinal; never a fabricated source identity.
};
struct RingSegment
{
    std::size_t firstVertex = 0, lastVertex = 0;
    std::array<double, 2> interval{};
    std::unique_ptr<SmCurve> curve; // Complete original basis, released only on native adoption.
    SmEdge* edge = nullptr;
};
struct RingSubdivision
{
    std::size_t sourceCurve = 0, record = 0, originalVertex = 0;
    std::array<double, 2> interval{};
    std::vector<RingCut> cuts;
    std::unique_ptr<SmCurve> reference; // Must die before its allocating candidate context.
    std::vector<RingSegment> segments; // Increasing ORIGINAL native parameters, shared by all source uses.
};
// Caller holds NativeMutex and owns an unpublished candidate/context. All basis
// copies below must be destroyed before that context on success and every exit.
// Borrow an unowned complete source-circle basis. Establish an interior native
// parameter without trimming, snapping endpoints or altering any source vertex.
// The numerical point residual uses the definition allowance in millimetres;
// the near-end guard instead uses the kernel's parameter comparison convention.
[[nodiscard]] Result<double> LocateRingCut(
    const SmCurve& circle,
    std::array<double, 2> interval,
    Point point,
    double maximumMm,
    const ExecutionCheckpoint& checkpoint
);
// Freeze a global, sorted subdivision before constructing ANY incident face.
// Every child owns a copy of the full original analytic/NURBS basis. Only edge
// intervals differ, so their exact binary64 endpoint union covers the source.
[[nodiscard]] Result<void> PrepareRingSegments(
    RingSubdivision& plan,
    const SmCurve& circle,
    const SmContext& context,
    const ExecutionCheckpoint& checkpoint
);
// Run again after UV, validators and queries, while all candidates are private.
// Requires original bases, exact interval cover and reciprocal shared endpoints.
[[nodiscard]] Result<void> VerifyRingSegments(const RingSubdivision& plan, const ExecutionCheckpoint& checkpoint);
} // namespace step_import::detail
