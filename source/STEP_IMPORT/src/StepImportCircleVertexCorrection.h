// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <span>

class SmCurve;
class SmSurface;
namespace step_import::detail
{
class Execution;
struct CircleVertexCurveView
{
    std::size_t edgeRecord, curveRecord, firstVertexRecord, lastVertexRecord;
    const SmCurve* nativeCurve = nullptr;
    Point firstMm, lastMm;
    double firstParameter = 0, lastParameter = 0;
};
struct CircleVertexSurfaceView
{
    std::size_t faceRecord;
    const SmSurface* nativeSurface = nullptr;
    std::vector<std::size_t> sourceVertexRecords;
};
// Consumes the existing checked source bindings and constructed defining curves.
// Only the supplied body-local points change, after every action and the shared
// cumulative/local-feature budgets have succeeded. No curve/surface is modified.
[[nodiscard]] Result<std::vector<CircleVertexCorrectionEvidence>> CorrectCircleVertices(
    const step_semantics::Snapshot& source,
    const step_semantics::Part::Shape& shape,
    const Options& options,
    std::span<const std::size_t> sourceVertexRecords,
    std::span<Point> nativePointsMm,
    std::span<const CircleVertexCurveView> curves,
    Execution& execution,
    Report& report
);
// The candidate is still private. Check nonparticipating faces before UV
// installation/publication, tightening the same request-wide budget receipt.
// This prevents an edge-only clearance from overlooking a nearby thin wall.
[[nodiscard]] Result<void> ProtectCircleVerticesFromSurfaces(
    const step_semantics::Snapshot& source,
    const Options& options,
    std::span<const CircleVertexSurfaceView> surfaces,
    std::vector<CircleVertexCorrectionEvidence>& evidence,
    Execution& execution,
    Report& report
);
} // namespace step_import::detail
