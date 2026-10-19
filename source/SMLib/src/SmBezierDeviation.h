// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <optional>
class SmCurve;
class SmBSplineCurve;
class SmExtent1d;
namespace sm_bezier_deviation
{
class QuadraticReference
{
public:

    QuadraticReference(const QuadraticReference&) = default;
    QuadraticReference(QuadraticReference&&) = default;
    ~QuadraticReference() = default;

private:

    QuadraticReference() = default;
    [[maybe_unused]] int dimension = 0, count = 0;
    [[maybe_unused]] double rounding = 0, scale = 0;
    [[maybe_unused]] std::array<double, 36> knots{};
    [[maybe_unused]] std::array<std::array<double, 4>, 33> coefficients{};
    friend std::optional<QuadraticReference> Prepare(const SmCurve&);
    friend std::optional<double> Bound(const QuadraticReference&, const SmBSplineCurve&, const SmExtent1d&, double);
};
// Owns bounded normalized coefficient data and native knots. Normalization
// rounding is covered by Bound; no borrowed geometry is retained.
std::optional<QuadraticReference> Prepare(const SmCurve& source);
std::optional<double> Bound(const QuadraticReference& source, const SmBSplineCurve& leaf, const SmExtent1d& interval, double tolerance);
// No mutation or lazy representation creation. A missing bound means that the
// caller must retain its numerical deviation check, not a failed native query.
std::optional<double> Bound(const SmCurve& source, const SmBSplineCurve& leaf, const SmExtent1d& interval, double tolerance);
} // namespace sm_bezier_deviation
