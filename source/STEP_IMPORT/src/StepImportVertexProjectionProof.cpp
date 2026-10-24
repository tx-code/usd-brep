// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportVertexProjectionProof.h"

#include "StepImportInterval.h"

namespace step_import::detail
{
namespace
{
Failure Refusal(const char* key)
{
    return { Status::Indeterminate, key, "Bounded circle vertex projection evidence is unestablished", {}, "Repair" };
}
Interval Square(Interval a)
{
    const double maximum = std::max(std::abs(a.low), std::abs(a.high));
    const double minimum = a.low <= 0 && a.high >= 0 ? 0 : std::min(std::abs(a.low), std::abs(a.high));
    return { minimum == 0 ? 0 : std::max(0., Down(minimum * minimum)), Up(maximum * maximum) };
}
Interval NormSquared(IntervalPoint a)
{
    return Square(a[0]) + Square(a[1]) + Square(a[2]);
}
double NormLower(IntervalPoint a)
{
    return SqrtNonnegative(NormSquared(a)).low;
}
struct RadialProjection
{
    IntervalPoint radial;
    Interval planeDistanceSquared, radialLength;
};
Result<RadialProjection> Coordinates(const Point& point, const CircleProjectionGeometry& circle)
{
    if (!std::isfinite(circle.radiusMm) || !(circle.radiusMm > 0))
    {
        return tl::unexpected(Refusal("CircleVertexProjectionRadius"));
    }
    const auto normal = Exact(circle.normal), delta = Subtract(Exact(point), Exact(circle.centerMm));
    const auto norm = NormSquared(normal);
    if (!(norm.low > 0))
    {
        return tl::unexpected(Refusal("CircleVertexProjectionNormal"));
    }
    const auto height = Dot(delta, normal);
    const auto radial = Subtract(delta, Times(normal, height / norm));
    return RadialProjection{ radial, Square(height) / norm, SqrtNonnegative(NormSquared(radial)) };
}
template <class F>
auto NumericEvidence(F&& function) -> decltype(function())
{
    try
    {
        return function();
    }
    catch (const std::overflow_error&)
    {
        return tl::unexpected(Refusal("CircleVertexProjectionNumericCapacity"));
    }
    catch (const std::domain_error&)
    {
        return tl::unexpected(Refusal("CircleVertexProjectionNumericUnestablished"));
    }
}
} // namespace

Result<CircleProjectionEvidence> ProveCircleProjection(
    const Point& originalMm,
    const CircleProjectionGeometry& circle,
    const Point& candidateMm,
    Fraction maximumDisplacementMm,
    Fraction maximumProjectionErrorMm
)
{
    if (originalMm == candidateMm)
    {
        return tl::unexpected(Refusal("CircleVertexProjectionNotRequired"));
    }
    // C + r * radial/rho is the unique closest point iff rho > 0. Interval
    // enclosures retain the rounding uncertainty of every elementary operation.
    auto computed = NumericEvidence(
        [&]() -> Result<CircleProjectionEvidence>
        {
            const auto data = Coordinates(originalMm, circle);
            if (!data)
            {
                return tl::unexpected(data.error());
            }
            if (!(data->radialLength.low > 0))
            {
                return tl::unexpected(Refusal("CircleVertexProjectionNotUnique"));
            }
            const auto nearest = Add(Exact(circle.centerMm), Times(data->radial, Exact(circle.radiusMm) / data->radialLength));
            // A plan cannot turn ordinary evaluation roundoff into a repair.
            // Establish an actual defect beyond the independent definition
            // budget before granting any displacement of a retained vertex.
            if (NormLower(Subtract(Exact(originalMm), nearest)) <= Enclose(maximumProjectionErrorMm).high)
            {
                return tl::unexpected(Refusal("CircleVertexProjectionNotRequired"));
            }
            return CircleProjectionEvidence{ NormUpper(Subtract(Exact(candidateMm), Exact(originalMm))),
                                             NormUpper(Subtract(Exact(candidateMm), nearest)),
                                             NormUpper(Subtract(Exact(originalMm), nearest)) };
        }
    );
    if (!computed)
    {
        return tl::unexpected(std::move(computed.error()));
    }
    auto limits = NumericEvidence(
        [&]() -> Result<std::array<double, 2>>
        {
            return std::array<double, 2>{ Enclose(maximumDisplacementMm).low, Enclose(maximumProjectionErrorMm).low };
        }
    );
    if (!limits)
    {
        return tl::unexpected(std::move(limits.error()));
    }
    if (!(computed->displacementUpperMm > 0) || computed->displacementUpperMm > (*limits)[0] || computed->originalGapUpperMm > (*limits)[0])
    {
        return tl::unexpected(Refusal("CircleVertexDisplacementBudget"));
    }
    if (computed->projectionErrorUpperMm > (*limits)[1])
    {
        return tl::unexpected(Refusal("CircleVertexProjectionBound"));
    }
    return *computed;
}
Result<double> CircleDistanceLower(const Point& pointMm, const CircleProjectionGeometry& circle)
{
    return NumericEvidence(
        [&]() -> Result<double>
        {
            const auto data = Coordinates(pointMm, circle);
            if (!data)
            {
                return tl::unexpected(data.error());
            }
            return SqrtNonnegative(data->planeDistanceSquared + Square(data->radialLength - Exact(circle.radiusMm))).low;
        }
    );
}
Result<double> SegmentDistanceLower(const Point& pointMm, const Point& firstMm, const Point& lastMm)
{
    return NumericEvidence(
        [&]() -> Result<double>
        {
            const auto first = Exact(firstMm), direction = Subtract(Exact(lastMm), first), point = Exact(pointMm);
            const auto norm = NormSquared(direction);
            if (!(norm.low > 0))
            {
                // A zero-length represented segment is a point; an uncertain
                // small norm must not manufacture positive clearance.
                return firstMm == lastMm ? NormLower(Subtract(point, first)) : 0.;
            }
            const auto t = Dot(Subtract(point, first), direction) / norm;
            const Interval clipped{ std::clamp(t.low, 0., 1.), std::clamp(t.high, 0., 1.) };
            return NormLower(Subtract(point, Add(first, Times(direction, clipped))));
        }
    );
}
Result<double> LineDistanceLower(const Point& pointMm, const Point& originMm, const Point& direction)
{
    return NumericEvidence(
        [&]() -> Result<double>
        {
            const auto vector = Exact(direction);
            const auto norm = NormSquared(vector);
            if (!(norm.low > 0))
            {
                return tl::unexpected(Refusal("CircleVertexProtectedLineUnestablished"));
            }
            return SqrtNonnegative(NormSquared(Cross(Subtract(Exact(pointMm), Exact(originMm)), vector)) / norm).low;
        }
    );
}
Result<double> BoxDistanceLower(const Point& pointMm, const Bounds& boxMm)
{
    return NumericEvidence(
        [&]() -> Result<double>
        {
            IntervalPoint difference;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const auto value = Exact(pointMm[axis]), low = Exact(boxMm[0][axis]), high = Exact(boxMm[1][axis]);
                if (low.low > high.high)
                {
                    return tl::unexpected(Refusal("CircleVertexProtectedHullUnestablished"));
                }
                difference[axis] = value.high < low.low ? low - value : value.low > high.high ? value - high : Exact(0.);
            }
            return NormLower(difference);
        }
    );
}
Result<double> PlaneDistanceLower(const Point& pointMm, const Point& originMm, const Point& normal)
{
    return NumericEvidence(
        [&]() -> Result<double>
        {
            const auto norm = NormSquared(Exact(normal));
            if (!(norm.low > 0))
            {
                return tl::unexpected(Refusal("CircleVertexProtectedPlaneUnestablished"));
            }
            return SqrtNonnegative(Square(Dot(Subtract(Exact(pointMm), Exact(originMm)), Exact(normal))) / norm).low;
        }
    );
}
Result<double> CylinderDistanceLower(const Point& pointMm, const CircleProjectionGeometry& cylinder)
{
    return NumericEvidence(
        [&]() -> Result<double>
        {
            const auto data = Coordinates(pointMm, cylinder);
            if (!data)
            {
                return tl::unexpected(data.error());
            }
            return SqrtNonnegative(Square(data->radialLength - Exact(cylinder.radiusMm))).low;
        }
    );
}
Result<double> SphereDistanceLower(const Point& pointMm, const Point& centerMm, double radiusMm)
{
    return NumericEvidence(
        [&]() -> Result<double>
        {
            if (!(radiusMm > 0))
            {
                return tl::unexpected(Refusal("CircleVertexProtectedSphereUnestablished"));
            }
            return SqrtNonnegative(Square(SqrtNonnegative(NormSquared(Subtract(Exact(pointMm), Exact(centerMm)))) - Exact(radiusMm))).low;
        }
    );
}
Result<double> TorusDistanceLower(const Point& pointMm, const CircleProjectionGeometry& majorCircle, double minorRadiusMm)
{
    return NumericEvidence(
        [&]() -> Result<double>
        {
            const auto data = Coordinates(pointMm, majorCircle);
            if (!data)
            {
                return tl::unexpected(data.error());
            }
            if (!(minorRadiusMm > 0))
            {
                return tl::unexpected(Refusal("CircleVertexProtectedTorusUnestablished"));
            }
            // Distance-to-center-circle is 1-Lipschitz. Its difference from the
            // tube radius is a lower bound even for a self-intersecting support.
            const auto centerDistance = SqrtNonnegative(data->planeDistanceSquared + Square(data->radialLength - Exact(majorCircle.radiusMm)));
            return SqrtNonnegative(Square(centerDistance - Exact(minorRadiusMm))).low;
        }
    );
}
} // namespace step_import::detail
