// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <string_view>

namespace step_semantics::unit_dimensions
{
struct Capacity
{
};
// Exact bounded arithmetic, not a tolerance. Capacity failure means unknown /
// ResourceLimit, never a rounded exponent or proof that a STEP value is invalid.
struct Rational
{
    std::int64_t n = 0, d = 1;
    Rational() = default;
    Rational(std::int64_t numerator, std::int64_t denominator = 1);
    static Rational Decimal(std::string_view validatedReal);
    friend bool operator==(const Rational&, const Rational&) = default;
};
Rational Add(Rational a, Rational b);
Rational Multiply(Rational a, Rational b);
} // namespace step_semantics::unit_dimensions
