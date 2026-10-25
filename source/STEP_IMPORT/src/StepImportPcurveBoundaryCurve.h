// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportSplineChart.h"

#include <array>
#include <string>

namespace step_import::detail
{
class PendingBoundaryPcurveLineWindow;
struct PcurveBoundaryCurveData;
struct PcurveBoundaryCurveAccess;
class PcurveBoundaryCurveWitness
{
public:

    [[nodiscard]] unsigned FixedAxis() const;
    [[nodiscard]] unsigned FixedEnd() const;
    [[nodiscard]] double MaximumMm() const;
    [[nodiscard]] std::size_t Patches() const;
    [[nodiscard]] std::array<std::string, 2> SourceInterval() const;
    [[nodiscard]] std::array<double, 2> NativeInterval() const;
    // The full-surface/UV witness remains mandatory. This additionally binds
    // the actual raw boundary column to the source conversion certificate.
    [[nodiscard]] Result<void> Verify(const SmSurface&, const SplineChartCallbacks&) const;

private:

    explicit PcurveBoundaryCurveWitness(std::shared_ptr<const PcurveBoundaryCurveData>);
    std::shared_ptr<const PcurveBoundaryCurveData> m_data;
    friend struct PcurveBoundaryCurveAccess;
};
namespace construction
{
class GeometryConstruction;
// A pending parameter plan cannot qualify geometry. Capture the original
// exact source boundary and compare it to the actual stored homogeneous row/
// column under the declared source/native endpoint correspondence. Uses ONE
// existing definitionMm; caller must prove its UV endpoint rounding is zero.
[[nodiscard]] Result<std::shared_ptr<const PcurveBoundaryCurveWitness>> CertifySourcePcurveBoundary(
    const GeometryConstruction&,
    std::size_t pcurve,
    std::size_t use,
    const PendingBoundaryPcurveLineWindow&,
    const SmSurface&,
    const SplineChartCallbacks&
);
} // namespace construction
} // namespace step_import::detail
