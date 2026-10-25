// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportSplineChart.h"

#include <array>
#include <memory>

namespace step_import::detail
{
struct PcurveLineLiftData;
class PcurveLineLiftWitness
{
public:

    [[nodiscard]] double MaximumMm() const;
    // Empty when both UV endpoint conversions are exact: no derivative bound
    // is needed, and zero must not be reported as a measured surface metric.
    [[nodiscard]] std::optional<std::array<double, 2>> DerivativeBoundsMmPerUv() const;
    [[nodiscard]] Result<void> Verify(const SmSurface&, const SmCurve&, const SplineChartCallbacks&) const;

private:

    explicit PcurveLineLiftWitness(std::shared_ptr<const PcurveLineLiftData>);
    std::shared_ptr<const PcurveLineLiftData> m_data;
    friend Result<std::shared_ptr<const PcurveLineLiftWitness>> ProvePcurveLineLift(
        const SmSurface&,
        const SmCurve&,
        const std::array<std::array<double, 2>, 2>&,
        const std::array<double, 2>&,
        double,
        const SplineChartCallbacks&
    );
};
// The exact LINE-window planner supplies nearest-rounded UV ends and outward
// per-axis errors. A degree-one native UV B-spline interpolates those ends on
// tau=[0,1]. Convex interpolation bounds its UV error over the WHOLE window.
// Global positive-homogeneous derivative hulls lift that bound to millimetres
// on the actual plain clamped B-spline carrier (4096 controls / 8192 knots).
// This is a definition-conversion bound, never a sampled consistency check,
// surface fit, extension, physical allowance or source-priority change.
[[nodiscard]] Result<std::shared_ptr<const PcurveLineLiftWitness>> ProvePcurveLineLift(
    const SmSurface& surface,
    const SmCurve& uv,
    const std::array<std::array<double, 2>, 2>& uvEnds,
    const std::array<double, 2>& maximumRoundingUv,
    double maximumMm,
    const SplineChartCallbacks& callbacks
);
} // namespace step_import::detail
