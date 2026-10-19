// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
class SmCurve;
class SmSurface;
namespace step_import::detail
{
struct TrimMeasurement
{
    double maximumMm = 0;
    std::size_t samples = 0;
};
// Read-only paired-parameter measurement of the FINAL installed trim. Samples
// include both curves' knots and eight subintervals per union span. This is a
// numerical development observation, not a continuous error certificate.
// Finite trim/edge parameter domains must agree within SmEdgeuse::AssertValid's
// parameter comparison policy (tests 25/26); no affine remapping or mm-budget
// growth is permitted. Every sample uses the same actual parameter on both.
void CheckFinalTrimParameters(const SmCurve& edge, const SmCurve& uv, double begin, double end);
TrimMeasurement MeasureFinalTrim(const SmCurve& edge, const SmCurve& uv, const SmSurface& surface, double begin, double end);
} // namespace step_import::detail
