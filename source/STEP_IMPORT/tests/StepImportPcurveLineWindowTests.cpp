// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPcurveLineWindow.h"

#include <algorithm>
#include <array>
#include <bit>
#include <boost/multiprecision/cpp_int.hpp>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#    include <xmmintrin.h>
#endif

namespace
{
using namespace step_import;
using namespace step_import::detail;
// Test-only unbounded exact arithmetic is independent of the planner's fixed
// scalar engine and is used as an analytic oracle, not a sampled certificate.
using Integer = boost::multiprecision::cpp_int;
using Exact = boost::multiprecision::cpp_rational;
using Pair = std::array<double, 2>;
using Rectangle = std::array<double, 4>;
using ExactPair = std::array<Exact, 2>;

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
PcurveLineWindowLimits Capacity()
{
    return { 4096, 200000, 32768, 1234 };
}
Failure CallerFailure(Status status, const char* key)
{
    return { status, key, "public caller sentinel", 197, "PublicWindowCaller" };
}
struct Ledger
{
    std::size_t work = 0, checks = 0;
    std::size_t maximum = std::numeric_limits<std::size_t>::max(), cancelAt = 0;
    PcurveLineWindowCallbacks Callbacks()
    {
        return { [this](std::size_t units) -> Result<void>
                 {
                     if (units > maximum - work)
                     {
                         return tl::unexpected(CallerFailure(Status::ResourceLimit, "PublicWorkSentinel"));
                     }
                     work += units;
                     return {};
                 },
                 [this]() -> Result<void>
                 {
                     ++checks;
                     if (checks == cancelAt)
                     {
                         return tl::unexpected(CallerFailure(Status::Cancelled, "PublicCancelSentinel"));
                     }
                     return {};
                 } };
    }
};
Exact Canonical(const Integer& numerator, const Integer& denominator)
{
    Require(denominator != 0, "oracle denominator must be nonzero");
    if (numerator == 0)
    {
        return Exact(0);
    }
    // The pinned rational_adaptor.hpp mixed rational/cpp_int divide path
    // (eval_divide, a.num()==0) retains the supplied denominator for zero.
    // Build components with its explicit GCD-normalizing entry instead of
    // creating noncanonical oracle values through mixed integer division.
    Exact result;
    boost::multiprecision::backends::assign_components(result.backend(), numerator.backend(), denominator.backend());
    return result;
}
Exact Read(const PcurveLineWindowRatio& ratio)
{
    return Canonical(Integer(ratio.numerator), Integer(ratio.denominator));
}
Exact Binary(double value)
{
    const auto bits = std::bit_cast<std::uint64_t>(value);
    const unsigned exponent = unsigned((bits >> 52) & 2047);
    Require(exponent != 2047, "oracle requires a finite binary scalar");
    Integer n = (bits & 0xfffffffffffffULL) | (exponent ? 0x10000000000000ULL : 0), d = 1;
    if (n == 0)
    {
        return Exact(0);
    }
    const int power = exponent ? int(exponent) - 1075 : -1074;
    if (power >= 0)
    {
        n <<= power;
    }
    else
    {
        d <<= -power;
    }
    if (bits >> 63)
    {
        n = -n;
    }
    return Canonical(n, d);
}
void OracleCanonicalization()
{
    for (double zero : { 0., -0. })
    {
        const auto value = Binary(zero);
        Require(
            boost::multiprecision::numerator(value) == 0 && boost::multiprecision::denominator(value) == 1,
            "signed binary zeros must enter the independent oracle as canonical 0/1"
        );
    }
    const auto zero = Read({ "0", "123456789012345678901234567890" });
    const auto half = Read({ "123456789012345678901234567890", "246913578024691357802469135780" });
    Require(
        boost::multiprecision::numerator(zero) == 0 && boost::multiprecision::denominator(zero) == 1,
        "ratio-text zero is canonical independently of its denominator"
    );
    Require(
        boost::multiprecision::numerator(half) == 1 && boost::multiprecision::denominator(half) == 2,
        "ratio-text oracle uses exact GCD component normalization"
    );
    Require(Binary(2.) + Binary(0.) * Binary(2.) == Exact(2), "canonical zero cannot corrupt the independent affine endpoint oracle");
}
ExactPair BinaryPair(const Pair& values)
{
    return { Binary(values[0]), Binary(values[1]) };
}
void RatioIs(const PcurveLineWindowRatio& actual, const char* n, const char* d)
{
    Require(actual.numerator == n && actual.denominator == d, "canonical exact ratio differs from independent analytic value");
}
PcurveLineWindow MustPlan(const Pair& point, const Pair& velocity, const Rectangle& domain)
{
    Ledger ledger;
    auto made = PlanPcurveLineWindow(point, velocity, domain, Capacity(), ledger.Callbacks());
    if (!made)
    {
        throw std::runtime_error("public window unexpectedly failed: " + made.error().key);
    }
    Require(made->has_value(), "public window must have a nonempty segment");
    return std::move(**made);
}
PcurveLineWindow MustPlan(const ExactDecimalPcurveLineGenerator& generator, const Rectangle& domain)
{
    Ledger ledger;
    auto made = PlanPcurveLineWindow(generator, domain, Capacity(), ledger.Callbacks());
    if (!made)
    {
        throw std::runtime_error("public decimal window unexpectedly failed: " + made.error().key);
    }
    Require(made->has_value(), "public decimal window must have a nonempty segment");
    return std::move(**made);
}

void CheckWholeIntersection(const PcurveLineWindow& plan, const ExactPair& point, const ExactPair& velocity, const Rectangle& domain)
{
    const Exact lo = Read(plan.generatorWindowExact[0]), hi = Read(plan.generatorWindowExact[1]);
    Require(lo < hi && Read(plan.generatorSpanExact) == hi - lo, "positive complete generator window and exact tau mapping");
    bool lowerBlocked = false, upperBlocked = false;
    std::array<Exact, 2> maximumErrors{ Exact(0), Exact(0) };
    for (std::size_t end = 0; end < 2; ++end)
    {
        const Exact q = end ? hi : lo;
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            const Exact uv = point[axis] + q * velocity[axis];
            const Exact minimum = Binary(domain[axis]), maximum = Binary(domain[axis + 2]);
            if (Read(plan.uvEndsExact[end][axis]) != uv)
            {
                std::ostringstream detail;
                detail << "exact endpoint retains original generator, without projection or fitting: end=" << end << ", axis=" << axis << ", q=" << q
                       << ", P=" << point[axis] << ", D=" << velocity[axis] << ", expected=" << uv
                       << ", actual=" << plan.uvEndsExact[end][axis].numerator << '/' << plan.uvEndsExact[end][axis].denominator;
                throw std::runtime_error(detail.str());
            }
            Require(uv >= minimum && uv <= maximum, "entire exact affine interval lies inside convex rectangle");
            const Exact stored = Binary(plan.uvEnds[end][axis]);
            Require(stored >= minimum && stored <= maximum, "nearest native endpoint stays inside original binary rectangle");
            Exact error = uv - stored;
            if (error < 0)
            {
                error = -error;
            }
            const Exact bound = Binary(plan.endpointRoundingUpperUv[end][axis]);
            Require(bound >= error && bound >= 0, "outward endpoint bound covers exact coordinate rounding");
            maximumErrors[axis] = std::max(maximumErrors[axis], error);
            // This boundary/sign test proves maximality of both q endpoints;
            // no smaller floating window or finite point sampling can satisfy
            // it in place of the full exact rectangle intersection.
            if (!end && ((uv == minimum && velocity[axis] > 0) || (uv == maximum && velocity[axis] < 0)))
            {
                lowerBlocked = true;
            }
            if (end && ((uv == minimum && velocity[axis] < 0) || (uv == maximum && velocity[axis] > 0)))
            {
                upperBlocked = true;
            }
        }
    }
    Require(lowerBlocked && upperBlocked, "both generator endpoints are exact outward-blocking rectangle boundaries");
    for (std::size_t axis = 0; axis < 2; ++axis)
    {
        Require(Binary(plan.maximumRoundingUpperUv[axis]) >= maximumErrors[axis], "convex whole-segment UV bound covers both exact endpoint errors");
    }
    Require(plan.hasUvRounding == (maximumErrors[0] != 0 || maximumErrors[1] != 0), "rounding flag is exact, including subnormal errors");
}

void AxisAndDirection()
{
    const Rectangle rectangle{ -1., -1., 2., 3. };
    const Pair point{ -7., 2. }, velocity{ 3., 0. };
    const auto axis = MustPlan(point, velocity, rectangle);
    RatioIs(axis.generatorWindowExact[0], "2", "1");
    RatioIs(axis.generatorWindowExact[1], "3", "1");
    Require(
        axis.uvEnds == std::array<Pair, 2>{ Pair{ -1., 2. }, Pair{ 2., 2. } } && !axis.hasUvRounding,
        "exact axis endpoints, origin may lie outside carrier"
    );
    CheckWholeIntersection(axis, BinaryPair(point), BinaryPair(velocity), rectangle);

    const Pair diagonalPoint{ -7., -9. }, diagonal{ 3., 4. };
    const auto forward = MustPlan(diagonalPoint, diagonal, rectangle);
    const auto reverse = MustPlan(diagonalPoint, { -3., -4. }, rectangle);
    RatioIs(forward.generatorWindowExact[0], "2", "1");
    RatioIs(forward.generatorWindowExact[1], "3", "1");
    RatioIs(reverse.generatorWindowExact[0], "-3", "1");
    RatioIs(reverse.generatorWindowExact[1], "-2", "1");
    Require(
        forward.uvEnds[0] == reverse.uvEnds[1] && forward.uvEnds[1] == reverse.uvEnds[0],
        "negative direction preserves increasing q and reverses UV travel"
    );
    CheckWholeIntersection(forward, BinaryPair(diagonalPoint), BinaryPair(diagonal), rectangle);
    CheckWholeIntersection(reverse, BinaryPair(diagonalPoint), { Exact(-3), Exact(-4) }, rectangle);

    // Independent axis powers show that the planner imposes no arbitrary
    // physical-size or unit-axis restrictions on finite UV values.
    for (int exponent : { -500, 500 })
    {
        const double scale = std::ldexp(1., exponent);
        const Pair p{ -7. * scale, -9. * scale }, v{ 3. * scale, 4. * scale };
        const Rectangle d{ -scale, -scale, 2. * scale, 3. * scale };
        const auto scaled = MustPlan(p, v, d);
        RatioIs(scaled.generatorWindowExact[0], "2", "1");
        RatioIs(scaled.generatorWindowExact[1], "3", "1");
        CheckWholeIntersection(scaled, BinaryPair(p), BinaryPair(v), d);
    }
}

void NonDyadicAndDecimal()
{
    const Pair p{ 0., 0. }, v{ 3., 4. };
    const Rectangle rectangle{ 0., 0., 1., 2. };
    const auto plan = MustPlan(p, v, rectangle);
    RatioIs(plan.generatorWindowExact[0], "0", "1");
    RatioIs(plan.generatorWindowExact[1], "1", "3");
    RatioIs(plan.uvEndsExact[1][0], "1", "1");
    RatioIs(plan.uvEndsExact[1][1], "4", "3");
    Require(
        std::bit_cast<std::uint64_t>(plan.uvEnds[1][1]) == 0x3ff5555555555555ULL,
        "4/3 rounds nearest-even, without cropping exact generator endpoint"
    );
    CheckWholeIntersection(plan, BinaryPair(p), BinaryPair(v), rectangle);

    const auto oblique = MustPlan({ -1., 0. }, { 3., 2. }, { 0., 0., 2., 1. });
    RatioIs(oblique.generatorWindowExact[0], "1", "3");
    RatioIs(oblique.generatorWindowExact[1], "1", "2");
    RatioIs(oblique.uvEndsExact[0][1], "2", "3");
    RatioIs(oblique.uvEndsExact[1][0], "1", "2");
    CheckWholeIntersection(oblique, { Exact(-1), Exact(0) }, { Exact(3), Exact(2) }, { 0., 0., 2., 1. });

    const ExactDecimalPcurveLineGenerator decimal{ { "+.1", "2.E-1" }, { "0.30", "4e-1" } };
    const auto exact = MustPlan(decimal, rectangle);
    RatioIs(exact.generatorWindowExact[0], "-1", "3");
    RatioIs(exact.generatorWindowExact[1], "3", "1");
    RatioIs(exact.uvEndsExact[0][1], "1", "15");
    RatioIs(exact.uvEndsExact[1][1], "7", "5");
    CheckWholeIntersection(exact, { Exact(1) / 10, Exact(1) / 5 }, { Exact(3) / 10, Exact(2) / 5 }, rectangle);
    const auto rescaled = MustPlan(ExactDecimalPcurveLineGenerator{ { ".1", ".2" }, { ".9", "1.2" } }, rectangle);
    RatioIs(rescaled.generatorWindowExact[0], "-1", "9");
    RatioIs(rescaled.generatorWindowExact[1], "1", "1");
    Require(
        exact.uvEnds == rescaled.uvEnds && exact.endpointRoundingUpperUv == rescaled.endpointRoundingUpperUv,
        "positive generator rescaling changes q, not the complete represented UV segment"
    );

    // Source decimal scalars need not first fit binary64. The exact generator
    // cancels a huge offset while its finite UV rectangle remains ordinary.
    const auto huge = MustPlan(ExactDecimalPcurveLineGenerator{ { "1.E400", "0." }, { "1E400", "0" } }, { 0., -1., 1., 1. });
    Integer power = 1;
    for (int i = 0; i < 400; ++i)
    {
        power *= 10;
    }
    Require(
        Read(huge.generatorWindowExact[0]) == -1 && Read(huge.generatorWindowExact[1]) == Exact(1 - power) / power,
        "huge decimal point is not silently rounded before cancellation"
    );
    Require(
        huge.generatorRoundingCollapsed && huge.uvEnds[0][0] == 0. && huge.uvEnds[1][0] == 1.,
        "exact q mapping survives collapsed rounded q values"
    );
    CheckWholeIntersection(huge, { Exact(power), Exact(0) }, { Exact(power), Exact(0) }, { 0., -1., 1., 1. });
}

void RoundingBoundaries()
{
    const double m = 0x1p53;
    const Pair p{ -m, 0. }, v{ 1., 1. };
    const Rectangle rectangle{ 0., m, 1., m + 2. };
    const auto plan = MustPlan(p, v, rectangle);
    RatioIs(plan.generatorWindowExact[0], "9007199254740992", "1");
    RatioIs(plan.generatorWindowExact[1], "9007199254740993", "1");
    RatioIs(plan.generatorSpanExact, "1", "1");
    Require(
        plan.generatorRoundingCollapsed && plan.generatorWindowRounded[0] == m && plan.generatorWindowRounded[1] == m,
        "nearest-even q tie is reported, not a reason to discard exact tau mapping"
    );
    Require(
        plan.uvEnds[0] == Pair{ 0., m } && plan.uvEnds[1] == Pair{ 1., m } && plan.maximumRoundingUpperUv[1] == 1.,
        "UV midpoint tie has an explicit full-segment error bound"
    );
    CheckWholeIntersection(plan, BinaryPair(p), BinaryPair(v), rectangle);

    const double least = std::numeric_limits<double>::denorm_min();
    const auto subnormal = MustPlan({ -0., -0. }, { least, 0. }, { 0., -1., least, 1. });
    RatioIs(subnormal.generatorWindowExact[0], "0", "1");
    RatioIs(subnormal.generatorWindowExact[1], "1", "1");
    Require(subnormal.uvEnds[1][0] == least && !subnormal.hasUvRounding, "subnormal direction and signed input zero retain exact nonempty coverage");
    CheckWholeIntersection(subnormal, { Exact(0), Exact(0) }, { Binary(least), Exact(0) }, { 0., -1., least, 1. });

    // Let M=2^52, P=(0,2-1/M), D=(M+1,-M), box=[0,1]^2.
    // The exact endpoints are (1-1/M^2,1) and
    // (1,1-1/(M(M+1))). Both round to (1,1), despite a positive
    // exact interval 1/(M^2(M+1)); such a native segment is unestablished.
    Ledger ledger;
    auto collapsed = PlanPcurveLineWindow({ 0., 2. - 0x1p-52 }, { 0x1p52 + 1., -0x1p52 }, { 0., 0., 1., 1. }, Capacity(), ledger.Callbacks());
    Require(
        !collapsed && collapsed.error().key == "PcurveLineWindowEndpointCollapse",
        "nonzero exact interval must not publish coincident rounded UV endpoints"
    );
}

void AbsenceAndInvalid()
{
    const Rectangle rectangle{ 0., 0., 1., 1. };
    for (const auto& values : std::array<std::array<Pair, 2>, 3>{
             std::array<Pair, 2>{ Pair{ 0., 2. }, Pair{ 1., 0. } }, // Parallel outside.
             std::array<Pair, 2>{ Pair{ -1., 1. }, Pair{ 1., 1. } }, // Corner contact only.
             std::array<Pair, 2>{ Pair{ -2., 2. }, Pair{ 1., 1. } } // Disjoint nonparallel.
         })
    {
        Ledger ledger;
        auto absent = PlanPcurveLineWindow(values[0], values[1], rectangle, Capacity(), ledger.Callbacks());
        Require(absent && !absent->has_value(), "normal empty/contact window must not be a source or native failure");
    }
    Ledger zero;
    auto stopped = PlanPcurveLineWindow({ 0., 0. }, { 0., -0. }, rectangle, Capacity(), zero.Callbacks());
    Require(!stopped && stopped.error().key == "PcurveLineWindowZeroVelocity", "zero LINE generator is invalid");
    for (const auto domain : { Rectangle{ 1., 0., 0., 1. }, Rectangle{ 0., 1., 1., 1. } })
    {
        Ledger ledger;
        auto invalid = PlanPcurveLineWindow({ 0., 0. }, { 1., 1. }, domain, Capacity(), ledger.Callbacks());
        Require(!invalid && invalid.error().key == "PcurveLineWindowDomain", "rectangle axes must have positive finite width");
    }
    for (double nonfinite :
         { std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
    {
        for (std::size_t slot = 0; slot < 8; ++slot)
        {
            Pair p{ 0., 0. }, v{ 1., 1. };
            Rectangle d = rectangle;
            if (slot < 2)
            {
                p[slot] = nonfinite;
            }
            else if (slot < 4)
            {
                v[slot - 2] = nonfinite;
            }
            else
            {
                d[slot - 4] = nonfinite;
            }
            Ledger ledger;
            auto invalid = PlanPcurveLineWindow(p, v, d, Capacity(), ledger.Callbacks());
            Require(!invalid && invalid.error().key == "PcurveLineWindowNonFinite", "all nonfinite input locations must refuse before construction");
        }
    }
    Ledger range;
    auto unrepresentable = PlanPcurveLineWindow(
        { 0., 0. },
        { std::numeric_limits<double>::denorm_min(), 0. },
        rectangle,
        Capacity(),
        range.Callbacks()
    );
    Require(
        !unrepresentable && unrepresentable.error().key == "PcurveLineWindowParameterBinaryRange",
        "unrepresentable rounded generator q is explicit, never infinity"
    );

    for (std::string_view text : { "", "+", ".", "1..0", "1E", "1E+", "1e--2", "1E2E3", " 1", "1 ", "NaN", "INF", "1D2", "0x1p0" })
    {
        Ledger ledger;
        auto invalid = PlanPcurveLineWindow(ExactDecimalPcurveLineGenerator{ { text, "0" }, { "1", "1" } }, rectangle, Capacity(), ledger.Callbacks());
        Require(
            !invalid && invalid.error().key == "PcurveLineWindowDecimalSyntax",
            "numeric lexeme overload does not accept unrelated STEP/text syntax"
        );
    }
    Ledger exponent;
    auto excessive = PlanPcurveLineWindow(
        ExactDecimalPcurveLineGenerator{ { "1E2467", "0" }, { "1", "1" } },
        rectangle,
        Capacity(),
        exponent.Callbacks()
    );
    Require(
        !excessive && excessive.error().status == Status::ResourceLimit && excessive.error().key == "PcurveLineWindowDecimalExponent",
        "decimal power capacity is explicit"
    );
}

std::string Fingerprint(const PcurveLineWindow& plan)
{
    std::ostringstream text;
    const auto ratio = [&text](const PcurveLineWindowRatio& value)
    {
        text << value.numerator << '/' << value.denominator << ';';
    };
    for (std::size_t end = 0; end < 2; ++end)
    {
        ratio(plan.generatorWindowExact[end]);
        text << std::bit_cast<std::uint64_t>(plan.generatorWindowRounded[end]) << ';';
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            ratio(plan.uvEndsExact[end][axis]);
            text << std::bit_cast<std::uint64_t>(plan.uvEnds[end][axis]) << ';'
                 << std::bit_cast<std::uint64_t>(plan.endpointRoundingUpperUv[end][axis]) << ';';
        }
    }
    ratio(plan.generatorSpanExact);
    text << plan.generatorRoundingCollapsed << plan.hasUvRounding;
    for (double bound : plan.maximumRoundingUpperUv)
    {
        text << ';' << std::bit_cast<std::uint64_t>(bound);
    }
    return text.str();
}
std::size_t OutputCharacters(const PcurveLineWindow& plan)
{
    std::size_t count = plan.generatorSpanExact.numerator.size() + plan.generatorSpanExact.denominator.size();
    for (std::size_t end = 0; end < 2; ++end)
    {
        count += plan.generatorWindowExact[end].numerator.size() + plan.generatorWindowExact[end].denominator.size();
        for (const auto& ratio : plan.uvEndsExact[end])
        {
            count += ratio.numerator.size() + ratio.denominator.size();
        }
    }
    return count;
}
void BudgetsAndCancellation()
{
    const Pair p{ -1., 0. }, v{ 3., 2. };
    const Rectangle rectangle{ 0., 0., 2., 1. };
    const auto originalPoint = std::bit_cast<std::array<std::uint64_t, 2>>(p);
    const auto originalVelocity = std::bit_cast<std::array<std::uint64_t, 2>>(v);
    const auto originalDomain = std::bit_cast<std::array<std::uint64_t, 4>>(rectangle);
    Ledger reference;
    auto baseline = PlanPcurveLineWindow(p, v, rectangle, Capacity(), reference.Callbacks());
    Require(baseline && baseline->has_value() && reference.work > 10 && reference.checks > 10, "public exact ledger baseline completes");
    const auto fingerprint = Fingerprint(**baseline);

    for (std::size_t available : { reference.work, reference.work - 1 })
    {
        auto limits = Capacity();
        limits.workUnits = available;
        Ledger ledger;
        auto result = PlanPcurveLineWindow(p, v, rectangle, limits, ledger.Callbacks());
        if (available == reference.work)
        {
            Require(result && result->has_value() && Fingerprint(**result) == fingerprint, "exact N local work units preserves complete output");
        }
        else
        {
            Require(
                !result && result.error().status == Status::ResourceLimit && result.error().key == "PcurveLineWindowWorkLimit",
                "N-1 local work fails without partial plan"
            );
        }
    }
    for (std::size_t available : { reference.work, reference.work - 1 })
    {
        Ledger ledger;
        ledger.maximum = available;
        auto result = PlanPcurveLineWindow(p, v, rectangle, Capacity(), ledger.Callbacks());
        if (available == reference.work)
        {
            Require(result && result->has_value() && Fingerprint(**result) == fingerprint, "shared caller ledger at N succeeds");
        }
        else
        {
            Require(
                !result && result.error().key == "PublicWorkSentinel" && result.error().record == 197 && result.error().stage == "PublicWindowCaller",
                "shared N-1 resource failure propagates unchanged"
            );
        }
    }
    for (std::size_t at : { std::size_t(1), reference.checks / 2, reference.checks })
    {
        Ledger ledger;
        ledger.cancelAt = at;
        auto result = PlanPcurveLineWindow(p, v, rectangle, Capacity(), ledger.Callbacks());
        Require(
            !result && result.error().status == Status::Cancelled && result.error().key == "PublicCancelSentinel" && result.error().record == 197 &&
                result.error().stage == "PublicWindowCaller",
            "early/middle/final cancellation propagates without a partial plan"
        );
    }
    const auto characters = OutputCharacters(**baseline);
    for (std::size_t available : { characters, characters - 1 })
    {
        auto limits = Capacity();
        limits.outputCharacters = available;
        Ledger ledger;
        auto result = PlanPcurveLineWindow(p, v, rectangle, limits, ledger.Callbacks());
        if (available == characters)
        {
            Require(result && result->has_value() && Fingerprint(**result) == fingerprint, "exact ratio text capacity succeeds at N");
        }
        else
        {
            Require(
                !result && result.error().key == "PcurveLineWindowOutputCapacity",
                "ratio text capacity fails at N-1 before exceeding allocation allowance"
            );
        }
    }
    auto small = Capacity();
    small.integerBits = 16;
    Ledger bits;
    auto limited = PlanPcurveLineWindow({ 0x1p100, 0. }, { 1., 1. }, rectangle, small, bits.Callbacks());
    Require(
        !limited && limited.error().key == "PcurveLineWindowIntegerCapacity" && limited.error().status == Status::ResourceLimit,
        "fixed reduced scalar bit limit never silently rounds input"
    );
    small = Capacity();
    small.tokenCharacters = 2;
    Ledger tokens;
    limited = PlanPcurveLineWindow(ExactDecimalPcurveLineGenerator{ { "0.1", "0" }, { "1", "1" } }, rectangle, small, tokens.Callbacks());
    Require(!limited && limited.error().key == "PcurveLineWindowDecimalCapacity", "source numeric token length is bounded");

    Ledger after;
    auto repeated = PlanPcurveLineWindow(p, v, rectangle, Capacity(), after.Callbacks());
    Require(
        repeated && repeated->has_value() && Fingerprint(**repeated) == fingerprint && after.work == reference.work &&
            after.checks == reference.checks,
        "failed attempts leave no planner state or changed output/scheduling"
    );
    Require(
        std::bit_cast<std::array<std::uint64_t, 2>>(p) == originalPoint && std::bit_cast<std::array<std::uint64_t, 2>>(v) == originalVelocity &&
            std::bit_cast<std::array<std::uint64_t, 4>>(rectangle) == originalDomain,
        "input binary definitions remain bit-identical across success and failure"
    );

    bool overflowPropagated = false;
    auto throwing = after.Callbacks();
    throwing.chargeWork = [](std::size_t) -> Result<void>
    {
        throw std::overflow_error("public caller overflow sentinel");
    };
    try
    {
        (void)PlanPcurveLineWindow(p, v, rectangle, Capacity(), throwing);
    }
    catch (const std::overflow_error& error)
    {
        overflowPropagated = std::string(error.what()) == "public caller overflow sentinel";
    }
    Require(overflowPropagated, "caller exceptions are not reclassified as internal integer overflow");
}

void ConfigurationAndEnvironment()
{
    Ledger ledger;
    auto callbacks = ledger.Callbacks();
    callbacks.checkCancelled = {};
    auto absent = PlanPcurveLineWindow({ 0., 0. }, { 1., 1. }, { 0., 0., 1., 1. }, Capacity(), callbacks);
    Require(!absent && absent.error().key == "PcurveLineWindowCapacityConfiguration", "missing callbacks are rejected explicitly");
    for (std::size_t bad : { std::size_t(0), std::size_t(4097) })
    {
        auto limits = Capacity();
        limits.integerBits = bad;
        Ledger invalid;
        auto result = PlanPcurveLineWindow({ 0., 0. }, { 1., 1. }, { 0., 0., 1., 1. }, limits, invalid.Callbacks());
        Require(
            !result && result.error().key == "PcurveLineWindowCapacityConfiguration",
            "invalid bit limits cannot enlarge fixed intermediate storage"
        );
    }
    struct RoundingRestore
    {
        int original = std::fegetround();
        ~RoundingRestore()
        {
            std::fesetround(original);
        }
    } restore;
    Ledger baseline;
    auto ordinary = PlanPcurveLineWindow({ 0., 0. }, { 1., 1. }, { 0., 0., 1., 1. }, Capacity(), baseline.Callbacks());
    Require(ordinary && ordinary->has_value(), "callback environment controls have an unchanged baseline");
    for (bool alterCancel : { false, true })
    {
        for (std::size_t at : { std::size_t(1), baseline.checks / 2, baseline.checks })
        {
            Require(std::fesetround(FE_TONEAREST) == 0, "restore nearest-even before callback environment control");
            Ledger changed;
            auto changing = changed.Callbacks();
            std::size_t charges = 0;
            if (alterCancel)
            {
                const auto original = changing.checkCancelled;
                changing.checkCancelled = [&, original]() -> Result<void>
                {
                    auto checked = original();
                    if (changed.checks == at)
                    {
                        std::fesetround(FE_UPWARD);
                    }
                    return checked;
                };
            }
            else
            {
                const auto original = changing.chargeWork;
                changing.chargeWork = [&, original](std::size_t units) -> Result<void>
                {
                    auto charged = original(units);
                    if (++charges == at)
                    {
                        std::fesetround(FE_UPWARD);
                    }
                    return charged;
                };
            }
            auto changedResult = PlanPcurveLineWindow({ 0., 0. }, { 1., 1. }, { 0., 0., 1., 1. }, Capacity(), changing);
            Require(
                !changedResult && changedResult.error().key == "PcurveLineWindowNumericEnvironment" && std::fegetround() == FE_UPWARD,
                "every successful early/middle/final callback is followed by a rounding-mode check"
            );
        }
    }
    Require(std::fesetround(FE_UPWARD) == 0, "public test must be able to select an unsupported rounding environment");
    Ledger environment;
    auto result = PlanPcurveLineWindow({ 0., 0. }, { 1., 1. }, { 0., 0., 1., 1. }, Capacity(), environment.Callbacks());
    Require(
        !result && result.error().key == "PcurveLineWindowNumericEnvironment" && std::fegetround() == FE_UPWARD,
        "planner refuses and never modifies the caller's floating environment"
    );
}
void GradualUnderflowCallbacks()
{
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    // A malicious callback can enable DAZ/FTZ between proof operations and
    // restore it at handoff. Checking only the initial/final mode could then
    // lose a positive subnormal bound in std::max and publish a false zero.
    constexpr unsigned daz = 1u << 6, ftz = 1u << 15;
    struct ControlRestore
    {
        unsigned original = _mm_getcsr();
        ~ControlRestore()
        {
            _mm_setcsr(original);
        }
    } restore;
    const unsigned supported = restore.original & ~(daz | ftz);
    _mm_setcsr(supported);
    const double least = std::numeric_limits<double>::denorm_min();
    const Pair point{ 0., 0. }, velocity{ 3. * least, 2. * least };
    const Rectangle rectangle{ 0., 0., least, 2. * least };
    Ledger baseline;
    auto ordinary = PlanPcurveLineWindow(point, velocity, rectangle, Capacity(), baseline.Callbacks());
    Require(
        ordinary && ordinary->has_value() && (**ordinary).hasUvRounding && (**ordinary).maximumRoundingUpperUv[1] == least,
        "public non-dyadic subnormal endpoint error has a positive outward bound"
    );
    CheckWholeIntersection(**ordinary, BinaryPair(point), BinaryPair(velocity), rectangle);
    for (unsigned control : { daz, ftz, daz | ftz })
    {
        _mm_setcsr(supported | control);
        Ledger initial;
        auto rejected = PlanPcurveLineWindow(point, velocity, rectangle, Capacity(), initial.Callbacks());
        Require(
            !rejected && rejected.error().key == "PcurveLineWindowNumericEnvironment" && initial.checks == 0,
            "initial DAZ/FTZ refuses before invoking external callbacks"
        );
        for (bool alterCancel : { false, true })
        {
            _mm_setcsr(supported);
            Ledger changed;
            auto callbacks = changed.Callbacks();
            std::size_t calls = 0;
            const auto at = baseline.checks / 2;
            const auto changeMode = [&]()
            {
                if (++calls == at)
                {
                    _mm_setcsr(_mm_getcsr() | control);
                }
                if (calls == baseline.checks)
                {
                    _mm_setcsr(supported); // Must never reach this restoration.
                }
            };
            if (alterCancel)
            {
                const auto original = callbacks.checkCancelled;
                callbacks.checkCancelled = [&, original]() -> Result<void>
                {
                    auto result = original();
                    changeMode();
                    return result;
                };
            }
            else
            {
                const auto original = callbacks.chargeWork;
                callbacks.chargeWork = [&, original](std::size_t units) -> Result<void>
                {
                    auto result = original(units);
                    changeMode();
                    return result;
                };
            }
            auto altered = PlanPcurveLineWindow(point, velocity, rectangle, Capacity(), callbacks);
            Require(
                !altered && altered.error().key == "PcurveLineWindowNumericEnvironment" && calls == at && (_mm_getcsr() & control) == control,
                "mid-proof DAZ/FTZ is rejected immediately, before a handoff callback could restore it"
            );
        }
    }
#endif
}
} // namespace
int main()
{
    try
    {
        Require(std::fegetround() == FE_TONEAREST, "public contract tests require initial nearest-even rounding");
        OracleCanonicalization();
        AxisAndDirection();
        NonDyadicAndDecimal();
        RoundingBoundaries();
        AbsenceAndInvalid();
        BudgetsAndCancellation();
        ConfigurationAndEnvironment();
        GradualUnderflowCallbacks();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
