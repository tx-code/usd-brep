// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportAnalyticLinePole.h"
#include "StepImportTerminalSpline.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
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
BinaryNet Polyline(const std::vector<std::array<double, 3>>& points)
{
    BinaryNet net;
    net.basis = { 1, 0, points.size(), 1, { 0, 0 }, {} };
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        net.basis.knotsU.push_back(double(i));
    }
    net.basis.knotsU.push_back(double(points.size() - 1));
    for (const auto& p : points)
    {
        net.homogeneous.push_back({ p[0], p[1], p[2], 1 });
    }
    return net;
}
void PoleBallVisits()
{
    const auto check = [](const BinaryNet& net, PoleVisits query, bool expected, const char* reason)
    {
        const auto before = net.homogeneous;
        const auto proof = CheckPoleVisits(net, query, Capacity(), Work);
        Require(proof && proof->established == expected, reason);
        Require(net.homogeneous == before, "Pole proof cannot edit actual native coefficients");
        if (expected)
        {
            Require(proof->patches > 0, "Established pole proof covers exact Bezier pieces");
        }
    };
    const auto line = Polyline({ { 0, 0, 0 }, { 2, 0, 0 } });
    check(line, { { 0, 1 }, { 0, 0, 0 }, .25, true, false }, true, "Allowed first pole neighborhood exits once");
    check(line, { { 0, 1 }, { 0, 0, 0 }, .25 }, false, "Unallowed endpoint is not outside the closed pole ball");
    check(line, { { 0, 1 }, { 2, 0, 0 }, .25, false, true }, true, "Allowed last pole neighborhood enters once");
    check(line, { { 0, 1 }, { 0, 2, 0 }, .25 }, true, "No endpoint allowance needed for an entirely outside curve");
    check(line, { { 0, 1 }, { 0, 0, 0 }, 4, true, true }, false, "An edge wholly in the pole ball must not qualify");
    auto rational = line;
    rational.homogeneous.back() = { 4, 0, 0, 2 };
    check(rational, { { 0, 1 }, { 0, 0, 0 }, .25, true, false }, true, "Nonaffine rational parameter retains a continuous prefix proof");
    const auto translated = Polyline({ { 4, -3, 7 }, { 4, -3, 9 } });
    check(translated, { { 0, 1 }, { 4, -3, 7 }, .25, true, false }, true, "All three physical axes and a translated pole enter the continuous norm");
    check(translated, { { 0, 1 }, { 4, -3, 7 }, .25 }, false, "A translated native pole cannot be mistaken for the origin");

    // Exact binary homogeneous circle: C(s)=((1-s*s)/(1+s*s),2*s/(1+s*s),0).
    // Its weights are (1,1,2); no rounded sqrt(1/2) or sampled circle assumption.
    const BinaryNet arc{ { 2, 0, 3, 1, { 0, 0, 0, 1, 1, 1 }, {} }, { { 1, 0, 0, 1 }, { 1, 1, 0, 1 }, { 0, 2, 0, 2 } } };
    check(arc, { { 0, 1 }, { 1, 0, 0 }, .125, true, false }, true, "Exact rational arc leaves its first pole ball");
    check(arc, { { 0, 1 }, { 0, 1, 0 }, .125, false, true }, true, "Exact rational arc enters its last pole ball");
    check(arc, { { 0, 1 }, { 0, 1, 0 }, .125 }, false, "Pole translation uses P*W on the rational weighted endpoint");
    check(arc, { { 0, 1 }, { 0, 0, 0 }, .5 }, true, "Exact rational arc is continuously outside its concentric smaller ball");
    check(arc, { { 0, 1 }, { 0, 0, 0 }, 1, true, true }, false, "A boundary-coincident circle never leaves the closed ball");
    const double weight = std::sqrt(.5);
    const BinaryNet roundedArc{ { 2, 0, 3, 1, { 0, 0, 0, 1, 1, 1 }, {} },
                                { { 3, 0, 0, 1 }, { 3 * weight, 0, 3 * weight, weight }, { -std::ldexp(1., -50), 0, 3, 1 } } };
    check(
        roundedArc,
        { { 0, 1 }, { 0, 0, 3 }, .001, false, true },
        true,
        "A binary rounded arc can have a nonmonotone final distance wholly inside the allowed native pole ball"
    );
    const auto ends = Polyline({ { 0, 0, 0 }, { 2, 0, 0 }, { 0, 0, 0 } });
    check(ends, { { 0, 2 }, { 0, 0, 0 }, .25, true, true }, true, "Both original endpoint visits can be allowed with an outside region");
    check(ends, { { 0, 2 }, { 0, 0, 0 }, .25, true, false }, false, "An allowed first endpoint cannot excuse an unallowed last return");
    check(ends, { { 0, 2 }, { 0, 0, 0 }, .25, false, true }, false, "An allowed last endpoint cannot excuse an unallowed first visit");
    const auto returning = Polyline({ { 0, 0, 0 }, { 2, 0, 0 }, { 0, 0, 0 }, { 2, 0, 0 }, { 4, 0, 0 } });
    check(returning, { { 0, 4 }, { 0, 0, 0 }, .25, true, false }, false, "An existing endpoint pole cannot hide an interior return");
    check(returning, { { 0, 4 }, { 0, 0, 0 }, .25, true, true }, false, "Allowing both ends does not allow disconnected interior visits");
    check(returning, { { 2.25, 3.75 }, { 0, 0, 0 }, .25 }, true, "Only the selected interval is part of this edgeuse proof");
    check(returning, { { 1, 2 }, { 0, 0, 0 }, .25, false, true }, true, "A selected knot-bounded interval has its own permitted last endpoint");
    check(returning, { { 0, 4 }, { 0, 0, 0 }, .25 }, false, "No endpoint allowance rejects every visit");
    const auto through = Polyline({ { -2, 0, 0 }, { 2, 0, 0 } });
    check(through, { { 0, 1 }, { 0, 0, 0 }, .25, true, true }, false, "Endpoint flags cannot authorize a wholly interior ball crossing");
    const auto tangent = Polyline({ { -2, .25, 0 }, { 2, .25, 0 } });
    check(tangent, { { 0, 1 }, { 0, 0, 0 }, .25, true, true }, false, "Isolated interior tangency belongs to the closed ball");
    // C(s)=3s/(1+192s*s*(1-s)), with positive exact weights (1,1,65,1).
    // C(1/8)=3/29>1/16, C(1/2)=3/50<1/16, C(1)=3>1/16.
    // Omitting -2*Q*W' in the rational distance derivative falsely makes the
    // whole curve look increasing, and would miss this middle return.
    const BinaryNet weightedReturn{ { 3, 0, 4, 1, { 0, 0, 0, 0, 1, 1, 1, 1 }, {} },
                                    { { 0, 0, 0, 1 }, { 1, 0, 0, 1 }, { 2, 0, 0, 65 }, { 3, 0, 0, 1 } } };
    check(weightedReturn, { { 0, 1 }, { 0, 0, 0 }, .0625, true, false }, false, "Rational weight derivative is necessary to exclude a middle return");
    auto shifted = arc;
    for (auto& knot : shifted.basis.knotsU)
    {
        knot = -3 + 12 * knot;
    }
    check(shifted, { { -3, 9 }, { 1, 0, 0 }, .125, true, false }, true, "Original negative and nonunit t domain is retained");
    check(shifted, { { 0, 6 }, { 1, 0, 0 }, .125 }, true, "Boehm extraction proves selected bounds inside one rational span");
}
void PoleBallLimits()
{
    const auto line = Polyline({ { 0, 0, 0 }, { 2, 0, 0 } });
    const PoleVisits query{ { 0, 1 }, { 0, 0, 0 }, .25, true, false };
    auto invalid = query;
    invalid.interval = { -1, 1 };
    auto proof = CheckPoleVisits(line, invalid, Capacity(), Work);
    Require(!proof && proof.error().status == Status::Indeterminate, "No extrapolation outside the actual native natural interval");
    invalid = query;
    invalid.radiusMm = 0;
    proof = CheckPoleVisits(line, invalid, Capacity(), Work);
    Require(!proof && proof.error().status == Status::Indeterminate, "Native pole ball has an explicit positive radius");
    auto bad = line;
    bad.homogeneous.back()[3] = -1;
    proof = CheckPoleVisits(bad, query, Capacity(), Work);
    Require(!proof && proof.error().status == Status::Indeterminate, "Negative native rational weights cannot acquire a proof");
    auto capacity = Capacity();
    capacity.controlSlots = 1;
    proof = CheckPoleVisits(line, query, capacity, Work);
    Require(!proof && proof.error().status == Status::ResourceLimit, "Pole proof includes resident binary controls in capacity");
    capacity = Capacity();
    capacity.integerBits = 16;
    invalid = query;
    invalid.poleMm[0] = 1e100;
    proof = CheckPoleVisits(line, invalid, capacity, Work);
    Require(!proof && proof.error().key == "TerminalSplineIntegerCapacity", "Pole proof integer capacity is distinct from geometric refusal");
    const Failure stopped{ Status::Cancelled, "PoleCallerCancel", "original caller", 23, "CallerPoleStage" };
    std::size_t calls = 0;
    proof = CheckPoleVisits(
        line,
        query,
        Capacity(),
        [&](std::size_t) -> Result<void>
        {
            if (++calls == 17)
            {
                return tl::unexpected(stopped);
            }
            return {};
        }
    );
    Require(
        !proof && proof.error().key == stopped.key && proof.error().record == stopped.record && proof.error().stage == stopped.stage,
        "Pole proof preserves cancellation at exact arithmetic boundaries"
    );
    std::size_t spent = 0;
    const Failure workLimit{ Status::ResourceLimit, "PoleCallerWorkLimit", "original caller capacity", 29, "CallerPoleWork" };
    proof = CheckPoleVisits(
        line,
        query,
        Capacity(),
        [&](std::size_t units) -> Result<void>
        {
            if (units > 50 - spent)
            {
                return tl::unexpected(workLimit);
            }
            spent += units;
            return {};
        }
    );
    Require(
        !proof && proof.error().key == workLimit.key && proof.error().record == workLimit.record && spent <= 50,
        "Caller cumulative work limit is retained without overspending"
    );
    bool threw = false;
    try
    {
        (void)CheckPoleVisits(
            line,
            query,
            Capacity(),
            [](std::size_t) -> Result<void>
            {
                throw std::overflow_error("pole caller");
            }
        );
    }
    catch (const std::overflow_error& e)
    {
        threw = std::string_view(e.what()) == "pole caller";
    }
    Require(threw, "Pole caller exceptions retain their meaning");
}
void AnalyticLinePoleVisits()
{
    const auto check = [](const AffineLineDefinition& line, const PoleVisits& visits, bool expected, const char* reason)
    {
        const auto proof = CheckLinePoleVisits(line, visits, Capacity(), Work);
        Require(proof && proof->established == expected, reason);
        if (expected)
        {
            Require(proof->patches > 0, "Analytic line uses the shared exact pole patch proof");
        }
    };
    // These are the actual stored analytic fields, not evaluated endpoints.
    // The real endpoint 2^53+1 cannot be represented by one double, whereas
    // rounded endpoint evaluation/cache construction produces 2^53 twice.
    AffineLineDefinition large;
    large.originMm = { std::ldexp(1., 53), 0, 0 };
    large.direction = { 1, 0, 0 };
    large.naturalInterval = large.stepAtNaturalEnds = { 0, 1 };
    const PoleVisits touching{ { 0, 1 }, { std::ldexp(1., 53) + 2, 0, 0 }, 1 };
    check(large, touching, false, "Actual analytic endpoint touches the closed ball despite the rounded cache");
    const auto cached = Polyline({ large.originMm, large.originMm });
    const auto wrongProof = CheckPoleVisits(cached, touching, Capacity(), Work);
    Require(wrongProof && wrongProof->established, "Rounded stored endpoints would incorrectly prove the different cached line outside");

    AffineLineDefinition line;
    line.originMm = { 1, -2, 3 };
    line.direction = { .5, .25, -.125 };
    line.scale = 1.5;
    line.naturalInterval = { -3, 9 };
    line.stepAtNaturalEnds = { -2, 6 };
    check(line, { { 0, 3 }, line.originMm, .125, true, false }, true, "Selected analytic interval retains nontrivial exact origin/direction/scale");
    check(line, { { 0, 3 }, line.originMm, .125 }, false, "Analytic origin is part of the actual pole-ball definition");
    check(line, { { 0, 3 }, { 2.5, -1.25, 2.625 }, .125 }, false, "Analytic scale is not rounded into a guessed native chord");
    line.stepAtNaturalEnds = { 6, -2 }; // InsideOut, not sorted away.
    check(line, { { 3, 6 }, line.originMm, .125, false, true }, true, "InsideOut attaches the selected pole visit to the actual last endpoint");
    check(line, { { 3, 6 }, line.originMm, .125 }, false, "InsideOut does not grant an unallowed last endpoint");
    check(line, { { 0, 3 }, line.originMm, .125 }, true, "Reverse STEP ends cannot be silently sorted before selecting the interval");

    AffineLineDefinition crossing;
    crossing.direction = { 1, 0, 0 };
    crossing.naturalInterval = { 0, 1 };
    crossing.stepAtNaturalEnds = { -2, 2 };
    check(crossing, { { 0, 1 }, { 0, 0, 0 }, .25, true, true }, false, "Endpoint allowances cannot authorize an interior analytic line visit");
    crossing.stepAtNaturalEnds = { 0, 1 };
    crossing.scale = -2;
    check(crossing, { { 0, 1 }, { 0, 0, 0 }, .25, true, false }, true, "A finite nonzero negative analytic scale preserves its explicit direction");

    AffineLineDefinition tiny;
    tiny.direction = { 1, 0, 0 };
    tiny.scale = std::ldexp(1., 41);
    tiny.naturalInterval = { 0, std::ldexp(1., -41) };
    tiny.stepAtNaturalEnds = { 9, -7 }; // Deliberately irrelevant in the native tiny-domain branch.
    tiny.directParameter = true;
    check(tiny, { tiny.naturalInterval, { 0, 0, 0 }, .125, true, false }, true, "Tiny-domain actual branch uses s=t and ignores reversed STEP ends");
    check(tiny, { tiny.naturalInterval, { 1, 0, 0 }, .125 }, false, "Tiny-domain analytic endpoint remains an exact ball contact");
    check(tiny, { { 0, std::ldexp(1., -42) }, { 1, 0, 0 }, .125 }, true, "Tiny direct branch retains the selected native subinterval");
    tiny.stepAtNaturalEnds = { 9, 9 };
    check(
        tiny,
        { tiny.naturalInterval, { 0, 0, 0 }, .125, true, false },
        true,
        "Unused STEP ends do not replace the actual direct-parameter native branch"
    );

    const PoleVisits query{ { 0, 1 }, { 0, 0, 0 }, .25, true, false };
    crossing.scale = 0;
    auto proof = CheckLinePoleVisits(crossing, query, Capacity(), Work);
    Require(!proof && proof.error().status == Status::Indeterminate, "Zero analytic line scale has no valid line proof");
    crossing.scale = std::numeric_limits<double>::infinity();
    Require(!CheckLinePoleVisits(crossing, query, Capacity(), Work), "Infinite analytic line scale is refused");
    crossing.scale = 2;
    crossing.direction = { 0, 0, 0 };
    Require(!CheckLinePoleVisits(crossing, query, Capacity(), Work), "Zero stored direction cannot masquerade as a line");
    crossing.direction = { 1, 0, 0 };
    crossing.stepAtNaturalEnds = { 1, 1 };
    Require(!CheckLinePoleVisits(crossing, query, Capacity(), Work), "Constant ordinary STEP mapping is unestablished");
    crossing.stepAtNaturalEnds = { 0, 1 };
    crossing.naturalInterval = { 1, 0 };
    Require(!CheckLinePoleVisits(crossing, query, Capacity(), Work), "Native natural interval must increase");
    crossing.naturalInterval = { 0, 1 };
    auto outside = query;
    outside.interval = { -1, 1 };
    Require(!CheckLinePoleVisits(crossing, outside, Capacity(), Work), "Selected analytic interval cannot extrapolate native bounds");
    auto small = Capacity();
    small.controlSlots = 1;
    proof = CheckLinePoleVisits(crossing, query, small, Work);
    Require(!proof && proof.error().status == Status::ResourceLimit, "Analytic homogeneous endpoints are included in control capacity");
    small = Capacity();
    small.integerBits = 16;
    proof = CheckLinePoleVisits(large, touching, small, Work);
    Require(!proof && proof.error().key == "TerminalSplineIntegerCapacity", "Analytic line cannot evade exact integer capacity");
    const Failure cancel{ Status::Cancelled, "AnalyticLineCallerCancel", "caller", 31, "AnalyticLineStage" };
    std::size_t calls = 0;
    proof = CheckLinePoleVisits(
        crossing,
        query,
        Capacity(),
        [&](std::size_t) -> Result<void>
        {
            if (++calls == 17)
            {
                return tl::unexpected(cancel);
            }
            return {};
        }
    );
    Require(
        !proof && proof.error().key == cancel.key && proof.error().record == cancel.record && proof.error().stage == cancel.stage,
        "Analytic field conversion preserves the caller cancellation"
    );
    bool threw = false;
    try
    {
        (void)CheckLinePoleVisits(
            crossing,
            query,
            Capacity(),
            [](std::size_t) -> Result<void>
            {
                throw std::overflow_error("analytic caller");
            }
        );
    }
    catch (const std::overflow_error& error)
    {
        threw = std::string_view(error.what()) == "analytic caller";
    }
    Require(threw, "Analytic caller exceptions are not arithmetic-capacity findings");
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
        PoleBallVisits();
        PoleBallLimits();
        AnalyticLinePoleVisits();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
