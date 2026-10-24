// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportTerminalSpline.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace step_import;
using namespace step_import::detail;
using namespace step_import::detail::terminal_spline;
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
step_import::detail::terminal_spline::Limits Capacity()
{
    return { 200000, 4096, 1234 };
}
Result<void> Work(std::size_t)
{
    return {};
}
Source RationalLine()
{
    Source source;
    source.u = { 1, { "-2", "8" }, { 2, 2 }, std::array<std::string, 2>{ "-2", "9" } };
    source.countU = 2;
    source.controls = { { "0", "3", "0" }, { "0", "3", "7" } };
    source.weights = { "1", "2" };
    return source;
}
BinaryNet Stored(const Canonical& target)
{
    BinaryNet result;
    result.basis = target.basis;
    for (std::size_t i = 0; i < target.controlsMm.size(); ++i)
    {
        const auto& p = target.controlsMm[i];
        const auto w = target.weights[i];
        result.homogeneous.push_back({ p[0] * w, p[1] * w, p[2] * w, w });
    }
    return result;
}
void Curve()
{
    const auto source = RationalLine();
    auto made = Extend(source, Capacity(), Work);
    Require(bool(made), "Exact source rational generator must extend");
    Require(made->target.basis.knotsU == std::vector<double>{ -2, -2, 9, 9 }, "Original V numbers survive extension");
    Require(made->target.weights == std::vector<double>{ 1, 2.1 }, "Analytic homogeneous endpoint weight");
    Require(std::abs(made->target.controlsMm[1][2] - 22. / 3.) < 1e-14, "Analytic rational generator endpoint");
    auto actual = Stored(made->target);
    auto proof = Certify(made->reference, actual, { "1", "1000000000" }, Capacity(), Work);
    Require(proof && proof->established && proof->maximumMm && *proof->maximumMm <= 1e-9, "Continuous entire-new-domain source certificate");
    actual.homogeneous.back()[2] += 1e-3;
    proof = Certify(made->reference, actual, { "1", "1000000000" }, Capacity(), Work);
    Require(proof && !proof->established && proof->maximumMm && *proof->maximumMm > 1e-9, "Actual native coefficient corruption is detected");
}
std::array<double, 4> DeBoor(unsigned degree, const std::vector<double>& knots, const std::vector<std::array<double, 4>>& cp, double t)
{
    const auto count = cp.size();
    std::size_t span = count - 1;
    if (t < knots[degree])
    {
        span = degree;
    }
    else if (t < knots[count])
    {
        span = std::size_t(std::upper_bound(knots.begin(), knots.end(), t) - knots.begin() - 1);
    }
    std::vector<std::array<double, 4>> work(cp.begin() + std::ptrdiff_t(span - degree), cp.begin() + std::ptrdiff_t(span + 1));
    for (unsigned level = 1; level <= degree; ++level)
    {
        for (unsigned j = degree; j >= level; --j)
        {
            const auto index = span - degree + j;
            const double alpha = (t - knots[index]) / (knots[index + degree - level + 1] - knots[index]);
            for (std::size_t axis = 0; axis < 4; ++axis)
            {
                work[j][axis] = (1 - alpha) * work[j - 1][axis] + alpha * work[j][axis];
            }
        }
    }
    return work[degree];
}
std::array<double, 3> Evaluate(const BinaryNet& net, double u, double v = 0)
{
    std::vector<std::array<double, 4>> columns;
    for (std::size_t j = 0; j < net.basis.countV; ++j)
    {
        std::vector<std::array<double, 4>> cp;
        for (std::size_t i = 0; i < net.basis.countU; ++i)
        {
            cp.push_back(net.homogeneous[i * net.basis.countV + j]);
        }
        columns.push_back(DeBoor(net.basis.degreeU, net.basis.knotsU, cp, u));
    }
    const auto point = net.basis.degreeV ? DeBoor(net.basis.degreeV, net.basis.knotsV, columns, v) : columns.front();
    return { point[0] / point[3], point[1] / point[3], point[2] / point[3] };
}
void MultiKnot()
{
    Source source;
    source.u = { 2, { "-2", "-1", "1", "3" }, { 3, 1, 1, 3 }, std::array<std::string, 2>{ "-2.5", "3.5" } };
    source.countU = 5;
    source.controls = { { "0", "0", "0" }, { "1", "2", "0" }, { "3", "-1", "1" }, { "6", "3", "2" }, { "9", "0", "4" } };
    source.weights = { "1", "1.1", "1.2", "1.3", "1.4" };
    const BinaryNet original{ { 2, 0, 5, 1, { -2, -2, -2, -1, 1, 3, 3, 3 }, {} },
                              { { 0, 0, 0, 1 }, { 1.1, 2.2, 0, 1.1 }, { 3.6, -1.2, 1.2, 1.2 }, { 7.8, 3.9, 2.6, 1.3 }, { 12.6, 0, 5.6, 1.4 } } };
    auto made = Extend(source, Capacity(), Work);
    Require(bool(made), "Both terminal spans of a multiknot rational source extend");
    Require(made->target.basis.countU == 7, "Boehm isolates both end spans before extrapolation");
    const auto actual = Stored(made->target);
    for (double t : { -2.5, -2., -1.5, -1., 0., 1., 2., 3., 3.5 })
    {
        const auto a = Evaluate(actual, t), b = Evaluate(original, t);
        for (std::size_t i = 0; i < 3; ++i)
        {
            Require(std::abs(a[i] - b[i]) < 1e-10, "Independent de Boor oracle, old domain and terminal formulas");
        }
    }
    auto proof = Certify(made->reference, actual, { "1", "1000000000" }, Capacity(), Work);
    Require(proof && proof->established && proof->patches == 3, "Whole extended multiknot certificate, without sampled acceptance");
    source.u.multiplicities = { 3, 2, 2, 3 };
    source.countU = 7;
    source.controls = { { "0", "0", "0" }, { "1", "1", "1" }, { "2", "0", "2" }, { "3", "1", "3" },
                        { "4", "0", "4" }, { "5", "1", "5" }, { "6", "0", "6" } };
    source.weights.clear();
    made = Extend(source, Capacity(), Work);
    Require(bool(made), "Repeated degree-multiplicity knots retain continuous C0 pieces");
    proof = Certify(made->reference, Stored(made->target), { "1", "1000000000" }, Capacity(), Work);
    Require(proof && proof->established, "Repeated-knot exact source certificate");
}
void Tensor()
{
    Source source;
    source.u = { 2, { "-2", "0.1", "3" }, { 3, 1, 3 }, std::array<std::string, 2>{ "-3", "4" } };
    source.v = SourceAxis{ 1, { "0", "1" }, { 2, 2 }, std::array<std::string, 2>{ "-1", "2" } };
    source.countU = 4;
    source.countV = 2;
    source.controls = { { "-2", "0", "0" },   { "-2", "1", "-2" },     { "-0.95", "0", "0" }, { "-0.95", "1", "-0.95" },
                        { "1.55", "0", "0" }, { "1.55", "1", "1.55" }, { "3", "0", "0" },     { "3", "1", "3" } };
    auto made = Extend(source, Capacity(), Work);
    Require(bool(made), "Multiknot tensor source extends in both directions");
    auto actual = Stored(made->target);
    for (double u : { -3., -2., 0., 0.1, 2., 3., 4. })
    {
        for (double v : { -1., 0., 0.5, 1., 2. })
        {
            const auto p = Evaluate(actual, u, v);
            Require(
                std::abs(p[0] - u) < 1e-10 && std::abs(p[1] - v) < 1e-10 && std::abs(p[2] - u * v) < 1e-10,
                "Independent S(u,v)=(u,v,u*v) oracle including corners"
            );
        }
    }
    auto proof = Certify(made->reference, actual, { "1", "100000000" }, Capacity(), Work);
    Require(proof && proof->established && proof->patches >= 3, "Exact decimal 0.1 source knot vs binary64 target knot uses common refinement");
    for (auto& knot : actual.basis.knotsU)
    {
        if (knot == 0.1)
        {
            knot = 0.2;
        }
    }
    proof = Certify(made->reference, actual, { "1", "100000000" }, Capacity(), Work);
    Require(proof && !proof->established, "A moved native interior knot cannot reuse the old basis certificate");
    actual = Stored(made->target);
    actual.homogeneous.back()[2] += 1;
    proof = Certify(made->reference, actual, { "1", "100000000" }, Capacity(), Work);
    Require(proof && !proof->established, "New corner control corruption is included in the certificate");
}
void Boundaries()
{
    auto covered = CheckScalarCoverage({ "0", "0.1" }, { "0", "0.1000000000000000001" }, Capacity(), Work);
    Require(covered && !*covered, "Exact source coverage rejects distinct decimals that round to one binary64");
    covered = CheckScalarCoverage({ "-1", "0.1" }, { "0.1", "0", "-1" }, Capacity(), Work);
    Require(covered && *covered, "Exact inclusive source hull control");
    auto error = SourcePointDifferenceUpper({ "0.1", "2", "0" }, { "25", "2" }, { 1.25, 25, 0 }, Capacity(), Work);
    Require(error && *error == 0, "Exact source point times exact length ratio has zero error");
    error = SourcePointDifferenceUpper({ "0.1", "0", "0" }, { "1", "1" }, { 0.1, 0, 0 }, Capacity(), Work);
    Require(error && *error > 0 && *error < 1e-17, "Binary64 0.1 is compared with original decimal, not itself");
    auto source = RationalLine();
    source.u = { 1, { "0", "1" }, { 2, 2 }, std::array<std::string, 2>{ "0", "1.0000000000000000001" } };
    source.weights.clear();
    source.controls = { { { "0", "0", "0" } }, { { "1", "0", "0" } } };
    auto made = Extend(source, Capacity(), Work);
    Require(
        made && std::bit_cast<std::uint64_t>(made->target.basis.knotsU.front()) == 0 && made->target.basis.knotsU.back() > 1,
        "Exact zero stays zero while nonrepresentable upper endpoint encloses outward"
    );
    source = RationalLine();
    source.u = { 1, { "0", "1" }, { 2, 2 }, std::array<std::string, 2>{ "0", "3" } };
    source.weights = { "2", "1" };
    made = Extend(source, Capacity(), Work);
    Require(!made && made.error().key == "TerminalSplineExtendedWeightUnestablished", "Old positive weights do not authorize a new rational pole");
    auto small = Capacity();
    small.controlSlots = 1;
    made = Extend(RationalLine(), small, Work);
    Require(!made && made.error().status == Status::ResourceLimit, "Control capacity is an execution limit");
    small = Capacity();
    small.integerBits = 32;
    source = RationalLine();
    source.controls[1][2] = "1E100";
    made = Extend(source, small, Work);
    Require(!made && made.error().status == Status::ResourceLimit, "Exact integer capacity refuses rather than wraps");
    const Failure stopped{ Status::ResourceLimit, "IndependentCallerWorkLimit", "caller work limit", 42, "CallerStage" };
    made = Extend(
        RationalLine(),
        Capacity(),
        [&](std::size_t) -> Result<void>
        {
            return tl::unexpected(stopped);
        }
    );
    Require(
        !made && made.error().key == stopped.key && made.error().record == stopped.record && made.error().stage == stopped.stage,
        "Caller failures propagate unchanged"
    );
    bool threw = false;
    try
    {
        (void)Extend(
            RationalLine(),
            Capacity(),
            [](std::size_t) -> Result<void>
            {
                throw std::overflow_error("caller");
            }
        );
    }
    catch (const std::overflow_error& e)
    {
        threw = std::string_view(e.what()) == "caller";
    }
    Require(threw, "Actual caller exceptions are not relabelled as proof capacity");
}
} // namespace
int main()
{
    try
    {
        Curve();
        MultiKnot();
        Tensor();
        Boundaries();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
