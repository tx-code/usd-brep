// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportPlan.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace step_import::detail
{
// Conservative enclosures for the supported IEEE binary64 / nearest-rounding execution configuration.
// Each elementary operation rounds outward explicitly; native stored doubles enter as exact singleton values.
struct Interval
{
    double low, high;
};
inline double Down(double value)
{
    const auto result = std::nextafter(value, -std::numeric_limits<double>::infinity());
    if (!std::isfinite(result))
    {
        throw std::overflow_error("NativeNumericEvidenceCapacityExceeded");
    }
    return result;
}
inline double Up(double value)
{
    return -Down(-value);
}
inline Interval Exact(double value)
{
    if (!std::isfinite(value))
    {
        throw std::overflow_error("NonfiniteNativeCoefficient");
    }
    return { value, value };
}
inline Interval operator+(Interval a, Interval b)
{
    return { Down(a.low + b.low), Up(a.high + b.high) };
}
inline Interval operator-(Interval a, Interval b)
{
    return { Down(a.low - b.high), Up(a.high - b.low) };
}
inline Interval operator*(Interval a, Interval b)
{
    const std::array<double, 4> products = { a.low * b.low, a.low * b.high, a.high * b.low, a.high * b.high };
    return { Down(*std::min_element(products.begin(), products.end())), Up(*std::max_element(products.begin(), products.end())) };
}
inline Interval operator/(Interval a, Interval b)
{
    if (b.low <= 0 && b.high >= 0)
    {
        throw std::domain_error("UnestablishedNativeDivision");
    }
    return a * Interval{ Down(1 / b.high), Up(1 / b.low) };
}
inline Interval Enclose(Fraction value)
{
    if (value.Numerator() == 0)
    {
        return Exact(0);
    }
    const auto numerator = static_cast<double>(value.Numerator());
    const auto denominator = static_cast<double>(value.Denominator());
    return Interval{ Down(numerator), Up(numerator) } / Interval{ Down(denominator), Up(denominator) };
}
inline double Value(Fraction value)
{
    return static_cast<double>(value.Numerator()) / static_cast<double>(value.Denominator());
}
inline Interval SqrtNonnegative(Interval value)
{
    if (value.high < 0)
    {
        throw std::domain_error("UnestablishedNativeSquareRoot");
    }
    return { std::max(0.0, Down(std::sqrt(std::max(0.0, value.low)))), Up(std::sqrt(value.high)) };
}
using IntervalPoint = std::array<Interval, 3>;
inline IntervalPoint Enclose(const ExactPoint& point)
{
    return { Enclose(point[0]), Enclose(point[1]), Enclose(point[2]) };
}
inline IntervalPoint Exact(const Point& point)
{
    return { Exact(point[0]), Exact(point[1]), Exact(point[2]) };
}
inline Point Value(const ExactPoint& point)
{
    return { Value(point[0]), Value(point[1]), Value(point[2]) };
}
inline IntervalPoint Add(IntervalPoint a, IntervalPoint b)
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        a[i] = a[i] + b[i];
    }
    return a;
}
inline IntervalPoint Subtract(IntervalPoint a, IntervalPoint b)
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        a[i] = a[i] - b[i];
    }
    return a;
}
inline IntervalPoint Times(IntervalPoint a, Interval b)
{
    for (auto& value : a)
    {
        value = value * b;
    }
    return a;
}
inline Interval Dot(IntervalPoint a, IntervalPoint b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline IntervalPoint Cross(IntervalPoint a, IntervalPoint b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
inline IntervalPoint Unit(const ExactPoint& vector, Fraction normSquared)
{
    return Times(Enclose(vector), Exact(1) / SqrtNonnegative(Enclose(normSquared)));
}
inline double NormUpper(IntervalPoint value)
{
    Interval sum = Exact(0);
    for (const auto component : value)
    {
        const auto maximum = std::max(std::abs(component.low), std::abs(component.high));
        sum = sum + Exact(maximum) * Exact(maximum);
    }
    return SqrtNonnegative(sum).high;
}
} // namespace step_import::detail
