// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

namespace step_import::detail
{
struct CircleProjectionGeometry
{
    Point centerMm, normal;
    double radiusMm = 0;
};
struct CircleProjectionEvidence
{
    double displacementUpperMm = 0, projectionErrorUpperMm = 0, originalGapUpperMm = 0;
};
// All coefficients are the represented native geometry's binary64 values in mm;
// the normal is dimensionless and need not have unit length. The candidate is
// checked against the unique closest point, never used to fit/replace the circle.
// Budgets are explicit, independent of source uncertainty and working tolerance.
// Unsupported arithmetic yields Indeterminate; STL allocation failures propagate.
[[nodiscard]] Result<CircleProjectionEvidence> ProveCircleProjection(
    const Point& originalMm,
    const CircleProjectionGeometry& circle,
    const Point& candidateMm,
    Fraction maximumDisplacementMm,
    Fraction maximumProjectionErrorMm
);
[[nodiscard]] Result<double> CircleDistanceLower(const Point& pointMm, const CircleProjectionGeometry& circle);
[[nodiscard]] Result<double> SegmentDistanceLower(const Point& pointMm, const Point& firstMm, const Point& lastMm);
[[nodiscard]] Result<double> LineDistanceLower(const Point& pointMm, const Point& originMm, const Point& direction);
[[nodiscard]] Result<double> BoxDistanceLower(const Point& pointMm, const Bounds& boxMm);
[[nodiscard]] Result<double> PlaneDistanceLower(const Point& pointMm, const Point& originMm, const Point& normal);
[[nodiscard]] Result<double> CylinderDistanceLower(const Point& pointMm, const CircleProjectionGeometry& cylinder);
[[nodiscard]] Result<double> SphereDistanceLower(const Point& pointMm, const Point& centerMm, double radiusMm);
[[nodiscard]] Result<double> TorusDistanceLower(const Point& pointMm, const CircleProjectionGeometry& majorCircle, double minorRadiusMm);
} // namespace step_import::detail
