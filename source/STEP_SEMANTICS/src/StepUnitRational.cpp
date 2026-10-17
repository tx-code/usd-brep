// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepUnitRational.h"

#include "StepSourceNumber.h"

#include <gsl/assert>
#include <limits>
#include <numeric>
#include <string>

namespace step_semantics::unit_dimensions
{
namespace
{
constexpr auto Maximum = std::numeric_limits<std::int64_t>::max();
std::int64_t Abs(std::int64_t value)
{
    return value < 0 ? -value : value;
}
std::int64_t Product(std::int64_t a, std::int64_t b)
{
    if (a && Abs(b) > Maximum / Abs(a))
    {
        throw Capacity{};
    }
    return a * b;
}
std::int64_t Sum(std::int64_t a, std::int64_t b)
{
    if ((b > 0 && a > Maximum - b) || (b < 0 && a < -Maximum - b))
    {
        throw Capacity{};
    }
    return a + b;
}
} // namespace
Rational::Rational(std::int64_t numerator, std::int64_t denominator)
{
    Expects(numerator != std::numeric_limits<std::int64_t>::min() && denominator > 0);
    const auto divisor = std::gcd(Abs(numerator), denominator);
    n = numerator / divisor;
    d = denominator / divisor;
}
Rational Rational::Decimal(std::string_view text)
{
    Expects(!text.empty()); // Only STEP_SOURCE-validated REAL tokens enter here.
    const bool negative = text.front() == '-';
    if (text.front() == '-' || text.front() == '+')
    {
        text.remove_prefix(1);
    }
    const auto e = text.find_first_of("Ee");
    const auto mantissa = text.substr(0, e);
    const auto point = mantissa.find('.');
    std::string digits;
    for (const auto c : mantissa)
    {
        if (c != '.')
        {
            digits.push_back(c);
        }
    }
    const auto first = digits.find_first_not_of('0');
    if (first == std::string::npos)
    {
        return {};
    } // Zero stays exact even with a huge exponent.
    const auto last = digits.find_last_not_of('0');
    const auto trailing = digits.size() - last - 1;
    const auto coefficient = SourceIndexAtMost(std::string_view(digits).substr(first, last - first + 1), static_cast<std::size_t>(Maximum));
    if (!coefficient)
    {
        throw Capacity{};
    }
    std::int64_t exponent = 0;
    if (e != std::string_view::npos)
    {
        auto field = text.substr(e + 1);
        const bool minus = field.front() == '-';
        if (field.front() == '-' || field.front() == '+')
        {
            field.remove_prefix(1);
        }
        // An exponent beyond the token's fractional/trailing-digit offsets cannot
        // cancel into this arithmetic capacity. Do not convert it or expand 10^E.
        const auto magnitude = SourceIndexAtMost(field, text.size() + 20);
        if (!magnitude)
        {
            throw Capacity{};
        }
        exponent = minus ? -static_cast<std::int64_t>(*magnitude) : static_cast<std::int64_t>(*magnitude);
    }
    exponent += static_cast<std::int64_t>(trailing) - static_cast<std::int64_t>(point == std::string_view::npos ? 0 : mantissa.size() - point - 1);
    Rational value(negative ? -static_cast<std::int64_t>(*coefficient) : static_cast<std::int64_t>(*coefficient));
    // Reduction before every decimal step also admits values such as5E-19 whose
    // reduced denominator fits even though10^19 itself would not fit.
    while (exponent > 0)
    {
        value.n = Product(value.n, 10);
        --exponent;
    }
    while (exponent < 0)
    {
        const auto divisor = std::gcd(Abs(value.n), std::int64_t{ 10 });
        value.n /= divisor;
        value.d = Product(value.d, 10 / divisor);
        ++exponent;
    }
    return value;
}
Rational Multiply(Rational a, Rational b)
{
    // Cross-cancel before products to retain exact small results without requiring
    // larger intermediate integers. Any remaining overflow is a capacity stop.
    const auto x = std::gcd(Abs(a.n), b.d), y = std::gcd(Abs(b.n), a.d);
    return { Product(a.n / x, b.n / y), Product(a.d / y, b.d / x) };
}
Rational Add(Rational a, Rational b)
{
    // Use the denominator GCD before forming the common denominator; no rounding
    // is allowed when a valid source exponent needs a larger arithmetic domain.
    const auto divisor = std::gcd(a.d, b.d);
    return { Sum(Product(a.n, b.d / divisor), Product(b.n, a.d / divisor)), Product(a.d, b.d / divisor) };
}
} // namespace step_semantics::unit_dimensions
