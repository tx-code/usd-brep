// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepPlanar.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace step_planar
{
namespace
{
constexpr auto Maximum = std::numeric_limits<std::int64_t>::max();
std::int64_t Abs(std::int64_t value)
{
    return value < 0 ? -value : value;
}
std::int64_t Multiply(std::int64_t a, std::int64_t b)
{
    if (a && Abs(b) > Maximum / Abs(a))
    {
        throw std::overflow_error("ExactArithmeticCapacityExceeded");
    }
    return a * b;
}
std::int64_t Add(std::int64_t a, std::int64_t b)
{
    if ((b > 0 && a > Maximum - b) || (b < 0 && a < -Maximum - b))
    {
        throw std::overflow_error("ExactArithmeticCapacityExceeded");
    }
    return a + b;
}
// Continued fractions compare nonnegative rationals without overflowing a cross product.
int ComparePositive(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d)
{
    int sense = 1;
    for (;;)
    {
        const auto x = a / b, y = c / d;
        if (x != y)
        {
            return (x < y ? -1 : 1) * sense;
        }
        const auto r = a % b, s = c % d;
        if (!r || !s)
        {
            return (r == s ? 0 : !r ? -1 : 1) * sense;
        }
        a = b;
        b = r;
        c = d;
        d = s;
        sense = -sense;
    }
}
} // namespace
Rational::Rational(std::int64_t n, std::int64_t d)
{
    if (n == std::numeric_limits<std::int64_t>::min() || d <= 0)
    {
        throw std::overflow_error("ExactArithmeticCapacityExceeded");
    }
    const auto divisor = std::gcd(Abs(n), d);
    m_numerator = n / divisor;
    m_denominator = d / divisor;
}
Rational Rational::Decimal(std::string_view text)
{
    bool negative = false;
    if (text.empty())
    {
        throw std::invalid_argument("Empty decimal");
    }
    if (text.front() == '-' || text.front() == '+')
    {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    std::string digits;
    int exponent = 0, fractional = 0;
    bool dot = false;
    std::size_t p = 0;
    for (; p < text.size() && text[p] != 'E' && text[p] != 'e'; ++p)
    {
        if (text[p] == '.')
        {
            if (dot)
            {
                throw std::invalid_argument("Invalid decimal");
            }
            dot = true;
            continue;
        }
        if (text[p] < '0' || text[p] > '9')
        {
            throw std::invalid_argument("Invalid decimal");
        }
        digits += text[p];
        if (dot)
        {
            ++fractional;
        }
    }
    if (digits.empty())
    {
        throw std::invalid_argument("Invalid decimal");
    }
    if (p < text.size())
    {
        ++p;
        bool minus = false;
        if (p < text.size() && (text[p] == '-' || text[p] == '+'))
        {
            minus = text[p] == '-';
            ++p;
        }
        if (p == text.size())
        {
            throw std::invalid_argument("Invalid exponent");
        }
        for (; p < text.size(); ++p)
        {
            if (text[p] < '0' || text[p] > '9')
            {
                throw std::invalid_argument("Invalid exponent");
            }
            if (exponent > 100000)
            {
                throw std::overflow_error("ExactArithmeticCapacityExceeded");
            }
            exponent = exponent * 10 + text[p] - '0';
        }
        if (minus)
        {
            exponent = -exponent;
        }
    }
    const auto first = digits.find_first_not_of('0');
    if (first == std::string::npos)
    {
        return {};
    }
    digits.erase(0, first);
    exponent -= fractional;
    while (digits.back() == '0')
    {
        digits.pop_back();
        ++exponent;
    }
    if (digits.size() > 19 || exponent < -18 || exponent > 18)
    {
        throw std::overflow_error("ExactArithmeticCapacityExceeded");
    }
    std::int64_t numerator = 0, denominator = 1;
    for (const char c : digits)
    {
        numerator = Add(Multiply(numerator, 10), c - '0');
    }
    for (int i = 0; i < std::max(exponent, 0); ++i)
    {
        numerator = Multiply(numerator, 10);
    }
    for (int i = 0; i < std::max(-exponent, 0); ++i)
    {
        denominator = Multiply(denominator, 10);
    }
    return { negative ? -numerator : numerator, denominator };
}
Rational operator+(Rational a, Rational b)
{
    const auto gcd = std::gcd(a.Denominator(), b.Denominator());
    return { Add(Multiply(a.Numerator(), b.Denominator() / gcd), Multiply(b.Numerator(), a.Denominator() / gcd)),
             Multiply(a.Denominator(), b.Denominator() / gcd) };
}
Rational operator-(Rational a)
{
    return { -a.Numerator(), a.Denominator() };
}
Rational operator-(Rational a, Rational b)
{
    return a + -b;
}
Rational operator*(Rational a, Rational b)
{
    const auto x = std::gcd(Abs(a.Numerator()), b.Denominator()), y = std::gcd(Abs(b.Numerator()), a.Denominator());
    return { Multiply(a.Numerator() / x, b.Numerator() / y), Multiply(a.Denominator() / y, b.Denominator() / x) };
}
Rational operator/(Rational a, Rational b)
{
    if (!b.Numerator())
    {
        throw std::domain_error("UndefinedExactDivision");
    }
    return a * Rational(b.Numerator() < 0 ? -b.Denominator() : b.Denominator(), Abs(b.Numerator()));
}
bool operator==(Rational a, Rational b)
{
    return a.Numerator() == b.Numerator() && a.Denominator() == b.Denominator();
}
bool operator!=(Rational a, Rational b)
{
    return !(a == b);
}
bool operator<(Rational a, Rational b)
{
    if ((a.Numerator() < 0) != (b.Numerator() < 0))
    {
        return a.Numerator() < 0;
    }
    const int comparison = ComparePositive(Abs(a.Numerator()), a.Denominator(), Abs(b.Numerator()), b.Denominator());
    return a.Numerator() < 0 ? comparison > 0 : comparison < 0;
}
bool operator>(Rational a, Rational b)
{
    return b < a;
}
bool operator<=(Rational a, Rational b)
{
    return !(b < a);
}
Quality Quality::PlanarFixture()
{
    return { "Q-P0-mm-01", Rational(1, 10000000) };
}
Logical Not(Logical value)
{
    return value == Logical::True ? Logical::False : value == Logical::False ? Logical::True : Logical::Unknown;
}
bool QuerySelects(Logical value)
{
    return value == Logical::True;
}
const char* LogicalName(Logical value)
{
    return value == Logical::True ? "TRUE" : value == Logical::False ? "FALSE" : "UNKNOWN";
}
} // namespace step_planar
