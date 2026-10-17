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
void DivideDecimal(std::string& digits, unsigned divisor)
{
    unsigned remainder = 0;
    for (auto& digit : digits)
    {
        const auto value = remainder * 10 + static_cast<unsigned>(digit - '0');
        digit = static_cast<char>('0' + value / divisor);
        remainder = value % divisor;
    }
    Ensures(remainder == 0); // The last decimal digit proves divisibility by 2 or 5.
    if (digits.front() == '0')
    {
        digits.erase(0, 1);
    }
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
    auto reduced = digits.substr(first, last - first + 1);
    std::int64_t denominator = 1;
    if (exponent < 0)
    {
        // Trailing decimal zeros were removed, so at least one of 2^k or 5^k
        // survives in the denominator. A fitting denominator therefore needs
        // k < 63; a fitting numerator cannot start with more than 19 + k digits.
        // These necessary bounds also limit the decimal division work on input.
        const auto places = static_cast<std::size_t>(-exponent);
        if (places >= std::numeric_limits<std::int64_t>::digits || reduced.size() > std::numeric_limits<std::int64_t>::digits10 + 1 + places)
        {
            throw Capacity{};
        }
        while (exponent < 0)
        {
            const auto lastDigit = reduced.back() - '0';
            const unsigned divisor = lastDigit % 2 == 0 ? 2 : lastDigit == 5 ? 5 : 1;
            denominator = Product(denominator, 10 / divisor);
            if (divisor != 1)
            {
                DivideDecimal(reduced, divisor);
            }
            ++exponent;
        }
    }
    // Cancel decimal factors before enforcing the numerator capacity: an
    // oversized source coefficient can still denote an exact fitting fraction.
    const auto coefficient = SourceIndexAtMost(reduced, static_cast<std::size_t>(Maximum));
    if (!coefficient)
    {
        throw Capacity{};
    }
    Rational value(negative ? -static_cast<std::int64_t>(*coefficient) : static_cast<std::int64_t>(*coefficient), denominator);
    while (exponent > 0)
    {
        value.n = Product(value.n, 10);
        --exponent;
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
