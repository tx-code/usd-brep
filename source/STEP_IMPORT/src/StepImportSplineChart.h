// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <SmCurve.h>
#include <SmSurface.h>
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>

namespace step_import::detail
{
struct SplineChartData;
struct SplineChartWitnessData;
struct SplineChartAccess;

struct SplineChartCallbacks
{
    // Both callbacks are required. Native calls and the final handoff have
    // cancellation checkpoints; scalar storage/comparisons use the work ledger.
    std::function<Result<void>()> beforeNativeCall;
    std::function<Result<void>(std::size_t)> chargeWork;
};

class BinarySplineChart
{
public:

    [[nodiscard]] int UExponent() const;
    [[nodiscard]] int VExponent() const;
    // Domains are {u_min,v_min,u_max,v_max}. Parameters are scaled by ldexp;
    // XYZ/W are untouched. MetricUv is the evaluated original UV midpoint;
    // derivative lengths there are physical |Su| and |Sv|, not error bounds.
    [[nodiscard]] std::array<double, 4> OriginalDomain() const;
    [[nodiscard]] std::array<double, 4> TargetDomain() const;
    [[nodiscard]] std::array<double, 2> MetricUv() const;
    [[nodiscard]] std::array<double, 2> MetricDerivativeLengths() const;
    [[nodiscard]] double MetricRatio() const;
    [[nodiscard]] double MetricCosine() const;
    [[nodiscard]] std::size_t SurfaceControlCount() const;

private:

    explicit BinarySplineChart(std::shared_ptr<const SplineChartData> data);
    std::shared_ptr<const SplineChartData> m_data;
    friend struct SplineChartAccess;
};

class SplineChartWitness
{
public:

    [[nodiscard]] std::size_t CurveControlCount() const;
    // Final topology ownership is allowed: this checks the actual complete
    // stored definition against privately owned immutable nets, not ownerless
    // construction state or mutable report text. A null uv checks only surface.
    // Effective zones are compared during ownerless construction; final owning
    // topology tolerances require the publication layer's own budget checks.
    [[nodiscard]] Result<void> Verify(const SmSurface& surface, const SmCurve* uv, const SplineChartCallbacks& callbacks) const;

private:

    explicit SplineChartWitness(std::shared_ptr<const SplineChartWitnessData> data);
    std::shared_ptr<const SplineChartWitnessData> m_data;
    friend struct SplineChartAccess;
};

struct SplineChartCandidate
{
    std::unique_ptr<SmSurface> surface;
    std::unique_ptr<SmCurve> uv;
    BinarySplineChart chart;
    std::shared_ptr<const SplineChartWitness> witness;
};

// Narrow, equivalent coordinate representation: plain clamped B-spline surface
// plus nonrational plain clamped 2D B-spline pcurve, both ownerless in context.
// Caps: 64x64 surface controls, 4096 curve controls, 8192 knots per definition.
// Scope/scalar nonreversibility returns nullopt; native API/postcondition,
// resource and callback failures propagate. A preset is bound to the original
// entire surface net and is never
// retuned. Original objects, pcurve t, work tolerances and physical budgets stay.
[[nodiscard]] Result<std::optional<SplineChartCandidate>> TryConditionSplinePcurve(
    const SmSurface& surface,
    const SmCurve& uv,
    SmContext& context,
    const std::optional<BinarySplineChart>& preset,
    const SplineChartCallbacks& callbacks
);

// Create the same chart for a target face before ownership/topology insertion.
// Mismatching source definitions fail; this never borrows or mutates original.
[[nodiscard]] Result<std::unique_ptr<SmSurface>> ApplySplineSurfaceChart(
    const SmSurface& original,
    const BinarySplineChart& chart,
    SmContext& context,
    const SplineChartCallbacks& callbacks
);
} // namespace step_import::detail
