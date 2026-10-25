// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <memory>
class SmCurve;
namespace step_import::detail::construction
{
class GeometryConstruction;
struct TrimmedSplineDefinition
{
    std::unique_ptr<SmCurve> curve;
    std::array<double, 2> binarySourceCuts{}; // trim_1, trim_2 order, never sorted away.
    std::array<Point, 2> nativeBasisEnds{}; // Increasing target domain; actual numerical endpoint observations.
    double measuredSourceResidualMm = 0; // Euclidean measurement, separate from the certificate below.
    CurveTrimEvidence evidence;
};
// One-level TRIMMED_CURVE of an explicit-knot source B-spline. Both cuts need
// PARAMETER_VALUE and PARAMETER/UNSPECIFIED preference; an optional point is an
// independent required selector. Reuses the real construction context, source
// units and request ledger. No new source interpretation or implicit repair.
[[nodiscard]] Result<TrimmedSplineDefinition> MakeTrimmedSplineDefinition(const GeometryConstruction& inputs, std::size_t record);
} // namespace step_import::detail::construction
