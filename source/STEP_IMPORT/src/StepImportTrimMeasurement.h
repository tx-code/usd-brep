// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <tl/expected.hpp>
class SmCurve;
class SmSurface;
namespace step_import::detail
{
struct TrimMeasurement
{
    double maximumMm = 0;
    std::size_t samples = 0;
};
// A local measurement refusal is not an execution cancellation or a request
// budget failure. Only callers with the original fallback policy consume it.
enum class TrimMeasurementError
{
    ParameterDomain,
    Knots,
    Evaluation,
    Nonfinite
};
constexpr const char* TrimMeasurementErrorName(TrimMeasurementError error) noexcept
{
    switch (error)
    {
        case TrimMeasurementError::ParameterDomain:
            return "FinalTrimParameterDomain";
        case TrimMeasurementError::Knots:
            return "FinalTrimKnots";
        case TrimMeasurementError::Evaluation:
            return "FinalTrimEvaluation";
        case TrimMeasurementError::Nonfinite:
            return "FinalTrimNonfinite";
    }
    return "UnknownFinalTrimFailure";
}
// Read-only paired-parameter measurement of the FINAL installed trim. Samples
// include both curves' knots and eight subintervals per union span. This is a
// numerical development observation, not a continuous error certificate.
// Finite trim/edge parameter domains must agree within SmEdgeuse::AssertValid's
// parameter comparison policy (tests 25/26); no affine remapping or mm-budget
// growth is permitted. Every sample uses the same actual parameter on both.
[[nodiscard]] tl::expected<void, TrimMeasurementError> CheckFinalTrimParameters(const SmCurve& edge, const SmCurve& uv, double begin, double end);
[[nodiscard]] tl::expected<TrimMeasurement, TrimMeasurementError> MeasureFinalTrim(
    const SmCurve& edge,
    const SmCurve& uv,
    const SmSurface& surface,
    double begin,
    double end
);
} // namespace step_import::detail
