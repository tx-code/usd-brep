// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSourceNumber.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <gsl/assert>
#include <string>

namespace step_semantics
{
namespace
{
struct Exponent
{
    bool negative;
    std::string_view digits;
};
Exponent ReadExponent(std::string_view text)
{
    bool negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-'))
    {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    const auto nonzero = text.find_first_not_of('0');
    if (text.empty() || nonzero == std::string_view::npos)
    {
        return { false, "0" };
    }
    return { negative, text.substr(nonzero) };
}
int MagnitudeOrder(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
    {
        return a.size() > b.size() ? 1 : -1;
    }
    const auto order = a.compare(b);
    return (order > 0) - (order < 0);
}
std::int64_t CappedMagnitude(std::string_view digits, std::int64_t cap)
{
    std::int64_t value = 0;
    for (const auto digit : digits)
    {
        const auto next = digit - '0';
        if (next > cap || value > (cap - next) / 10)
        {
            return cap;
        }
        value = value * 10 + next;
    }
    return std::min(value, cap);
}
std::int64_t MagnitudeDifference(std::string_view high, std::string_view low, std::int64_t cap)
{
    std::int64_t result = 0, place = 1;
    int borrow = 0;
    auto j = low.size();
    // Subtract arbitrary-length decimal exponents in one pass, but retain only a
    // bounded difference. Even nearly equal huge exponents must keep the borrow;
    // independently saturating both exponents would lose that ordering.
    for (auto i = high.size(); i > 0;)
    {
        int digit = high[--i] - '0' - borrow - (j ? low[--j] - '0' : 0);
        borrow = digit < 0;
        if (borrow)
        {
            digit += 10;
        }
        if (digit && place > (cap - result) / digit)
        {
            result = cap;
        }
        else
        {
            result += digit * place;
        }
        place = place > cap / 10 ? cap : place * 10;
    }
    Expects(borrow == 0 && j == 0);
    return result;
}
std::int64_t ExponentDifference(Exponent a, Exponent b, std::int64_t cap)
{
    if (a.negative != b.negative)
    {
        const auto sum = std::min(cap, CappedMagnitude(a.digits, cap) + CappedMagnitude(b.digits, cap));
        return a.negative ? -sum : sum;
    }
    const auto order = MagnitudeOrder(a.digits, b.digits);
    if (!order)
    {
        return 0;
    }
    const auto magnitude = order > 0 ? MagnitudeDifference(a.digits, b.digits, cap) : MagnitudeDifference(b.digits, a.digits, cap);
    return (a.negative ? -order : order) * magnitude;
}
struct Decimal
{
    int sign = 0;
    std::string digits;
    std::int64_t offset = 0;
    Exponent exponent{ false, "0" };
};
Decimal ReadDecimal(std::string_view text)
{
    Expects(!text.empty()); // STEP_SOURCE has already validated the numeric token grammar.
    Decimal value;
    value.sign = text.front() == '-' ? -1 : 1;
    if (text.front() == '+' || text.front() == '-')
    {
        text.remove_prefix(1);
    }
    const auto e = text.find_first_of("Ee");
    const auto mantissa = text.substr(0, e);
    if (e != std::string_view::npos)
    {
        value.exponent = ReadExponent(text.substr(e + 1));
    }
    for (const auto c : mantissa)
    {
        if (c != '.')
        {
            value.digits.push_back(c);
        }
    }
    const auto first = value.digits.find_first_not_of('0');
    if (first == std::string::npos)
    {
        value.sign = 0;
        return value;
    }
    const auto point = mantissa.find('.');
    // Source hard byte limits bound both offsets; no machine conversion of a
    // potentially enormous source exponent is needed to establish decimal order.
    value.offset = static_cast<std::int64_t>(point == std::string_view::npos ? mantissa.size() : point) - static_cast<std::int64_t>(first);
    const auto last = value.digits.find_last_not_of('0');
    value.digits = value.digits.substr(first, last - first + 1);
    return value;
}
} // namespace

int CompareSourceReals(std::string_view left, std::string_view right)
{
    const auto a = ReadDecimal(left), b = ReadDecimal(right);
    if (a.sign != b.sign)
    {
        return (a.sign > b.sign) - (a.sign < b.sign);
    }
    if (!a.sign)
    {
        return 0;
    }
    const auto absolute = [](std::int64_t value)
    {
        return value < 0 ? -value : value;
    };
    // A difference beyond this cap dominates every possible mantissa offset.
    // Thus bounded exponent subtraction is exact for ordering, not an approximation.
    const auto cap = absolute(a.offset) + absolute(b.offset) + 3;
    const auto order = ExponentDifference(a.exponent, b.exponent, cap) + a.offset - b.offset;
    if (order)
    {
        return a.sign * ((order > 0) - (order < 0));
    }
    for (std::size_t i = 0; i < std::max(a.digits.size(), b.digits.size()); ++i)
    {
        const auto x = i < a.digits.size() ? a.digits[i] : '0';
        const auto y = i < b.digits.size() ? b.digits[i] : '0';
        if (x != y)
        {
            return a.sign * ((x > y) - (x < y));
        }
    }
    return 0;
}
std::optional<std::size_t> SourceIndexAtMost(std::string_view text, std::size_t maximum)
{
    Expects(!text.empty());
    const bool negative = text.front() == '-';
    if (text.front() == '+' || text.front() == '-')
    {
        text.remove_prefix(1);
    }
    const auto first = text.find_first_not_of('0');
    if (first == std::string_view::npos)
    {
        return 0;
    }
    text.remove_prefix(first);
    if (negative || MagnitudeOrder(text, std::to_string(maximum)) > 0)
    {
        return {};
    }
    std::size_t result = 0;
    const auto converted = std::from_chars(text.data(), text.data() + text.size(), result);
    Expects(converted.ec == std::errc{} && converted.ptr == text.data() + text.size());
    return result;
}
} // namespace step_semantics
