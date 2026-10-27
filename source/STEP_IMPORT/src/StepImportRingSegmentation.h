// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"
#include "StepImportExecutionCallbacks.h"

#include <SmCurve.h>
#include <functional>
#include <optional>

class SmContext;
class SmEdge;
class SmCircle;
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
    std::size_t sourceCurve = 0, record = 0;
    std::size_t originalVertex = 0; // Vertex at interval[0]; derived for an interior-phase source cycle.
    std::array<double, 2> interval{};
    std::vector<RingCut> cuts;
    std::unique_ptr<SmCurve> reference; // Must die before its allocating candidate context.
    std::vector<RingSegment> segments; // Increasing ORIGINAL native parameters, shared by all source uses.
    std::optional<std::size_t> originalEndVertex; // Vertex at interval[1]; absence reuses the interval-start vertex.
};
struct ClosedSplineCycle
{
    double phase = 0;
    std::array<double, 2> interval{};
    Point boundaryPoint{};
    double closureResidualMm = 0; // Native endpoint observation, not a continuous source conversion certificate.
};
// The caller establishes a declared closed, non-self-intersecting source spline
// edge with one source vertex and an interior inverted phase. Establish a finite
// full natural interval and its native endpoint closure using the unchanged mm
// definition allowance. No periodic extension, phase shift, fitting or vertex
// movement: children retain the full basis and traverse [phase,max],[min,phase].
// This private candidate plan needs final definition/incidence/UV checks too.
[[nodiscard]] Result<ClosedSplineCycle> PlanClosedSplineCycle(
    const SmCurve& curve,
    double phase,
    double definitionMm,
    const ExecutionCheckpoint& checkpoint
);
struct ConicalRingSupport
{
    Point origin{}, xAxis{}, yAxis{}, zAxis{};
    double radiusMm = 0, slope = 0;
};
struct ConicalRingInput
{
    const SmCircle* circle = nullptr;
    std::array<double, 2> interval{};
    Point sourceVertex{};
};
struct ConicalRingCut
{
    double angleRadians = 0;
    std::array<double, 2> parameters{}, residualsMm{};
    std::array<Point, 2> pointsMm{};
};
// Choose a different computational longitude only; all original vertices and
// complete 3D curves remain unchanged. Parameters are located ON each original
// circle using definitionMm. A NEW cut vertex lies on the cone and its connection
// to that circle uses the existing consistency allowance, as in
// PeriodicNormalizationDerivedVertexResidual. This is not a curve fit, source
// displacement, tolerance increase or continuous source-geometry certificate.
[[nodiscard]] Result<std::optional<ConicalRingCut>> PlanConicalRingCut(
    const ConicalRingSupport& support,
    const std::array<ConicalRingInput, 2>& rings,
    double oldAngleRadians,
    double definitionMm,
    double consistencyMm,
    double workMm,
    const ExecutionCheckpoint& checkpoint
);
// Caller holds NativeMutex and owns an unpublished candidate/context. All basis
// copies below must be destroyed before that context on success and every exit.
// Borrow an unowned complete native circle/arc basis. Establish an interior native
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
