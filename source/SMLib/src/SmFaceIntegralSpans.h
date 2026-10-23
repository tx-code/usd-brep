// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SmBSplineCurve.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <typeinfo>

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

// Read-only homogeneous de Boor evaluation in a span's local parameter x.
// Forming t=min+x*width first can round away the quadrature node when native
// knots have a large offset and a narrow interval. Compute basis ratios from
// (min-knot)+x*width instead, and propagate d/dx through the same recurrence.
// Original knots, controls, curve parameters and UV coordinates are unchanged.
// This is a numerical evaluator, not a continuous error certificate.
class LocalCurve
{
    SmTArray<double> m_knots;
    const double* m_controls = nullptr;
    ULONG m_count = 0, m_degree = 0;
    bool m_rational = false;

public:

    SmStatus Prepare(const SmBSplineCurve& curve)
    {
        // A failed preparation cannot leave a previous curve's borrowed controls
        // usable: their owning boundary may already have been released.
        m_controls = nullptr;
        m_count = m_degree = 0;
        m_knots.ReSet();
        double* controls = nullptr;
        m_degree = curve.GetDegree();
        m_rational = curve.IsRational() != FALSE;
        if (curve.GetType() != SmBSplineCurve_TYPE || typeid(curve) != typeid(SmBSplineCurve) || curve.GetDim() != 2 || !m_degree || m_degree > 32 ||
            curve.GetControlPointsPointer(m_count, controls) != SM_SUCCESS || !controls || m_count <= m_degree)
        {
            return SM_ERR_INVALID_INPUT;
        }
        const auto status = curve.GetKnotsAll(m_knots);
        if (status != SM_SUCCESS)
        {
            return status;
        }
        if (m_knots.GetSize() != m_count + m_degree + 1)
        {
            return SM_ERR_INVALID_INPUT;
        }
        for (ULONG i = 0; i < m_knots.GetSize(); ++i)
        {
            if (!std::isfinite(m_knots[i]) || (i && m_knots[i] < m_knots[i - 1]))
            {
                return SM_ERR_INVALID_INPUT;
            }
        }
        m_controls = controls; // Borrowed only while the owning boundary curve lives, under the caller's kernel serialization.
        return SM_SUCCESS;
    }
    SmStatus Evaluate(Span span, double x, SmPoint3d (&values)[2]) const
    {
        if (!m_controls || !std::isfinite(x) || x < 0 || x > 1 || !std::isfinite(span.Length()) || !(span.Length() > 0))
        {
            return SM_ERR_INVALID_INPUT;
        }
        const auto* begin = &m_knots[0];
        const auto* end = begin + m_knots.GetSize();
        const auto* upper = std::upper_bound(begin, end, span.min);
        if (upper == begin)
        {
            return SM_ERR_INVALID_INPUT;
        }
        const auto knot = static_cast<ULONG>(upper - begin - 1);
        if (knot < m_degree || knot >= m_count || m_knots[knot] > span.min || m_knots[knot + 1] < span.max)
        {
            return SM_ERR_INVALID_INPUT;
        }
        using H = std::array<double, 4>;
        std::array<H, 33> points{}, derivatives{};
        for (ULONG j = 0; j <= m_degree; ++j)
        {
            const auto index = knot - m_degree + j;
            points[j] = { m_controls[4 * index], m_controls[4 * index + 1], 0., m_rational ? m_controls[4 * index + 3] : 1. };
        }
        for (ULONG level = 1; level <= m_degree; ++level)
        {
            for (ULONG j = m_degree; j >= level; --j)
            {
                const auto index = knot - m_degree + j;
                const double denominator = m_knots[index + m_degree - level + 1] - m_knots[index];
                if (!(denominator > 0) || !std::isfinite(denominator))
                {
                    return SM_ERR_INVALID_INPUT;
                }
                const double numerator = x == 0 ? span.min - m_knots[index] :
                                         x == 1 ? span.max - m_knots[index] :
                                                  std::fma(x, span.Length(), span.min - m_knots[index]);
                const double alpha = numerator / denominator, slope = span.Length() / denominator;
                for (ULONG component = 0; component < 4; ++component)
                {
                    const double delta = points[j][component] - points[j - 1][component];
                    derivatives[j][component] = std::lerp(derivatives[j - 1][component], derivatives[j][component], alpha) + slope * delta;
                    points[j][component] = std::lerp(points[j - 1][component], points[j][component], alpha);
                }
            }
        }
        const auto& p = points[m_degree];
        const auto& d = derivatives[m_degree];
        if (!std::isfinite(p[3]) || p[3] == 0)
        {
            return SM_ERR_NOT_CONVERGING;
        }
        values[0].Set(p[0] / p[3], p[1] / p[3], 0.);
        values[1].Set((d[0] - values[0].x * d[3]) / p[3], (d[1] - values[0].y * d[3]) / p[3], 0.);
        return std::isfinite(values[0].x) && std::isfinite(values[0].y) && std::isfinite(values[1].x) && std::isfinite(values[1].y) ?
                   SM_SUCCESS :
                   SM_ERR_NOT_CONVERGING;
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
    if (curve.GetType() != SmBSplineCurve_TYPE || typeid(curve) != typeid(SmBSplineCurve) || !std::isfinite(min) || !std::isfinite(max) ||
        !(max > min) || !std::isfinite(max - min))
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
