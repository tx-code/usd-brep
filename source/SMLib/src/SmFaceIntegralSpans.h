// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SmBSplineCurve.h>
#include <array>
#include <cmath>

namespace sm_face_integrals
{
inline constexpr std::array<double, 5> initialStepDivisors{ 2., 8., 64., 512., 4096. };
static_assert(initialStepDivisors.front() == 2. && initialStepDivisors.back() == 4096.);

struct Span
{
    double min, max;

    double Parameter(double unitParameter) const
    {
        return std::lerp(min, max, unitParameter);
    }
    double Length() const
    {
        return max - min;
    }
    // The curve's TRUE flag selects the span beginning at an interior knot;
    // FALSE selects the span ending there. Use the current span at both ends,
    // including when interpolation rounds an interior sample to its endpoint.
    bool FromLeft(double nativeParameter) const
    {
        return nativeParameter < max;
    }
};

struct Partition
{
    SmTArray<double> boundaries;

    ULONG Size() const
    {
        return boundaries.GetSize() > 2 ? boundaries.GetSize() - 1 : 1;
    }
    bool IsSplit() const
    {
        return boundaries.GetSize() > 2;
    }
    Span operator[](ULONG index) const
    {
        return { boundaries[index], boundaries[index + 1] };
    }
};

// Inspect a concrete, clamped B-spline's actual native knot vector. Exact
// duplicate knots have no integration width; no tolerance merges positive
// spans. Other registered evaluators/layouts retain the original integration.
inline SmStatus MakePartition(const SmBSplineCurve& curve, const SmExtent1d& interval, Partition& result)
{
    result.boundaries.ReSet();
    const double min = interval.GetMin(), max = interval.GetMax();
    if (curve.GetType() != SmBSplineCurve_TYPE || !std::isfinite(min) || !std::isfinite(max) || !(max > min) || !std::isfinite(max - min))
    {
        return SM_SUCCESS;
    }
    const SmStatus status = curve.GetKnotsAll(result.boundaries);
    if (status != SM_SUCCESS)
    {
        return status;
    }
    const ULONG size = result.boundaries.GetSize(), degree = curve.GetDegree();
    if (degree == 0 || degree > 32 || size < 2 * (degree + 1))
    {
        result.boundaries.ReSet();
        return SM_SUCCESS;
    }
    for (ULONG i = 0; i < size; ++i)
    {
        if (!std::isfinite(result.boundaries[i]) || (i && result.boundaries[i] < result.boundaries[i - 1]))
        {
            return SM_ERR_INVALID_INPUT;
        }
    }
    const double first = result.boundaries[0], last = result.boundaries[size - 1];
    bool clamped = min >= first && max <= last;
    for (ULONG i = 0; i <= degree; ++i)
    {
        clamped = clamped && result.boundaries[i] == first && result.boundaries[size - 1 - i] == last;
    }
    if (!clamped)
    {
        result.boundaries.ReSet();
        return SM_SUCCESS;
    }
    ULONG count = 1;
    result.boundaries[0] = min;
    for (ULONG i = 0; i < size; ++i)
    {
        const double knot = result.boundaries[i];
        if (knot > result.boundaries[count - 1] && knot < max)
        {
            result.boundaries[count++] = knot;
        }
    }
    result.boundaries[count++] = max;
    result.boundaries.SetSize(count);
    return SM_SUCCESS;
}

// Equal allocation is independent of native parameter spacing: a short span
// can carry substantial geometry. Round down so the sum of the requested span
// fractions cannot exceed the old per-curve fraction. The existing adaptive
// area scale is retained; this is not a certified global error bound.
inline double SpanAccuracy(double curveAccuracy, ULONG spans)
{
    return spans > 1 ? std::nextafter(curveAccuracy / static_cast<double>(spans), 0.) : curveAccuracy;
}
} // namespace sm_face_integrals
