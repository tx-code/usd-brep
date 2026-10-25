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
#include <type_traits>
#include <utility>
#include <vector>
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
static_assert(!std::is_default_constructible_v<PendingBoundaryPcurveLineWindow>);
static_assert(!std::is_constructible_v<PendingBoundaryPcurveLineWindow, PcurveLineWindow, std::size_t, unsigned>);
static_assert(!std::is_convertible_v<PendingBoundaryPcurveLineWindow, PcurveLineWindow>);
static_assert(std::is_copy_constructible_v<PendingBoundaryPcurveLineWindow>);
static_assert(std::is_same_v<decltype(std::declval<const PendingBoundaryPcurveLineWindow&>().ProposedWindow()), const PcurveLineWindow&>);

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
    text << ';' << bool(plan.affineSourceChart);
    if (plan.affineSourceChart)
    {
        for (const auto& axis : plan.affineSourceChart->axes)
        {
            ratio(axis.scale);
            ratio(axis.offset);
            ratio(axis.sourceDomainExact[0]);
            ratio(axis.sourceDomainExact[1]);
            text << axis.knotsChecked << ';';
            text << bool(axis.fixedSourceBoundary) << ';';
            text << axis.requiresBoundaryCurveCertificate << ';';
            if (axis.fixedSourceBoundary)
            {
                text << *axis.fixedSourceBoundary << ';';
            }
            for (double bound : axis.targetDomain)
            {
                text << std::bit_cast<std::uint64_t>(bound) << ';';
            }
        }
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
    if (plan.affineSourceChart)
    {
        for (const auto& axis : plan.affineSourceChart->axes)
        {
            count += axis.scale.numerator.size() + axis.scale.denominator.size();
            count += axis.offset.numerator.size() + axis.offset.denominator.size();
            for (const auto& sourceEnd : axis.sourceDomainExact)
            {
                count += sourceEnd.numerator.size() + sourceEnd.denominator.size();
            }
        }
    }
    return count;
}
void WholeKnotAffineChart()
{
    const std::array<std::string_view, 4> sourceU{ ".1", ".1", ".3", ".3" }, sourceV{ "-1", "-1", "3", "3" };
    const std::array<double, 4> nativeU{ .1, .1, .3, .3 }, nativeV{ -1., -1., 3., 3. };
    const Rectangle rectangle{ .1, -1., .3, 3. };
    const PcurveLineKnotChartInput input{ { sourceU, sourceV }, { nativeU, nativeV } };
    const ExactDecimalPcurveLineGenerator generator{ { ".1", "11" }, { "0", "-4" } };
    Ledger naked;
    auto absent = PlanPcurveLineWindow(generator, rectangle, Capacity(), naked.Callbacks());
    Require(absent && !absent->has_value(), "raw exact decimal .1 is outside a binary rectangle whose minimum rounds upward");
    const auto binary = MustPlan({ .1, 11. }, { 0., -4. }, rectangle);
    Require(!binary.affineSourceChart, "preexisting pure binary overload retains its own semantics without inventing source chart evidence");
    Ledger baseline;
    auto made = PlanPcurveLineWindow(generator, input, rectangle, Capacity(), baseline.Callbacks());
    Require(made && made->has_value() && (**made).affineSourceChart, "exact full-knot chart establishes the source boundary without inward movement");
    const auto& plan = **made;
    const auto& chart = *plan.affineSourceChart;
    RatioIs(plan.generatorWindowExact[0], "2", "1");
    RatioIs(plan.generatorWindowExact[1], "3", "1");
    Require(
        plan.uvEnds == std::array<Pair, 2>{ Pair{ .1, 3. }, Pair{ .1, -1. } } && !plan.hasUvRounding,
        "chart maps exact source boundary to exact native boundary, with original q and direction"
    );
    RatioIs(chart.axes[0].scale, "36028797018963965", "36028797018963968");
    RatioIs(chart.axes[0].offset, "1", "72057594037927936");
    RatioIs(chart.axes[0].sourceDomainExact[0], "1", "10");
    RatioIs(chart.axes[0].sourceDomainExact[1], "3", "10");
    RatioIs(chart.axes[1].scale, "1", "1");
    RatioIs(chart.axes[1].offset, "0", "1");
    Require(
        chart.axes[0].targetDomain == Pair{ .1, .3 } && chart.axes[1].targetDomain == Pair{ -1., 3. } && chart.axes[0].knotsChecked == 4 &&
            chart.axes[1].knotsChecked == 4,
        "receipt identifies complete expanded knot counts and both actual native domains"
    );
    for (std::size_t i = 0; i < sourceU.size(); ++i)
    {
        const Exact source = i < 2 ? Canonical(Integer(1), Integer(10)) : Canonical(Integer(3), Integer(10));
        Require(
            Read(chart.axes[0].scale) * source + Read(chart.axes[0].offset) == Binary(nativeU[i]),
            "independent exact oracle checks every affine U knot, including multiplicity"
        );
    }
    CheckWholeIntersection(plan, { Binary(.1), Exact(11) }, { Exact(0), Exact(-4) }, rectangle);

    // Interior source .2 has no common affine image at nearest-double(.2)
    // under the endpoint-determined chart above. A boundary LINE does not
    // justify claiming an unproved whole-axis chart in its place.
    const std::array<std::string_view, 5> interiorSource{ ".1", ".1", ".2", ".3", ".3" };
    const std::array<double, 5> interiorNative{ .1, .1, .2, .3, .3 };
    Ledger notAffine;
    auto refused = PlanPcurveLineWindow(
        generator,
        PcurveLineKnotChartInput{ { interiorSource, sourceV }, { interiorNative, nativeV } },
        rectangle,
        Capacity(),
        notAffine.Callbacks()
    );
    Require(
        !refused && refused.error().status == Status::Indeterminate && refused.error().key == "PcurveLineWindowKnotChartUnestablished",
        "full expanded knot proof never fits or silently disregards a nonaffine interior knot"
    );
    const std::array<std::string_view, 4> unorderedSource{ ".1", ".3", ".2", ".3" };
    const std::array<double, 4> unorderedNative{ .1, .3, .2, .3 };
    Ledger unordered;
    refused = PlanPcurveLineWindow(
        generator,
        PcurveLineKnotChartInput{ { unorderedSource, sourceV }, { unorderedNative, nativeV } },
        rectangle,
        Capacity(),
        unordered.Callbacks()
    );
    Require(!refused && refused.error().key == "PcurveLineWindowKnotChartOrder", "full knot sequence must be nondecreasing on both sides");
    Ledger countMismatch;
    refused = PlanPcurveLineWindow(
        generator,
        PcurveLineKnotChartInput{ { interiorSource, sourceV }, { nativeU, nativeV } },
        rectangle,
        Capacity(),
        countMismatch.Callbacks()
    );
    Require(
        !refused && refused.error().key == "PcurveLineWindowKnotChartStructure",
        "expanded knot lengths and multiplicities cannot be silently changed"
    );
    Ledger domainMismatch;
    refused = PlanPcurveLineWindow(generator, input, { .1, -1., .4, 3. }, Capacity(), domainMismatch.Callbacks());
    Require(!refused && refused.error().key == "PcurveLineWindowKnotChartDomain", "native rectangle must match actual expanded knot endpoints");

    const auto fingerprint = Fingerprint(plan);
    for (std::size_t work : { baseline.work, baseline.work - 1 })
    {
        auto limits = Capacity();
        limits.workUnits = work;
        Ledger ledger;
        auto bounded = PlanPcurveLineWindow(generator, input, rectangle, limits, ledger.Callbacks());
        if (work == baseline.work)
        {
            Require(
                bounded && bounded->has_value() && Fingerprint(**bounded) == fingerprint,
                "all chart scalar work is bounded and succeeds at exact N"
            );
        }
        else
        {
            Require(!bounded && bounded.error().key == "PcurveLineWindowWorkLimit", "full chart plus window fails at N-1 without a partial receipt");
        }
    }
    for (std::size_t at : { std::size_t(1), baseline.checks / 2, baseline.checks })
    {
        Ledger cancelled;
        cancelled.cancelAt = at;
        auto result = PlanPcurveLineWindow(generator, input, rectangle, Capacity(), cancelled.Callbacks());
        Require(
            !result && result.error().key == "PublicCancelSentinel" && result.error().record == 197 && result.error().stage == "PublicWindowCaller",
            "source knot/chart/window cancellation is propagated without reclassification"
        );
    }
    Ledger shared;
    shared.maximum = baseline.work - 1;
    auto sharedFailure = PlanPcurveLineWindow(generator, input, rectangle, Capacity(), shared.Callbacks());
    Require(
        !sharedFailure && sharedFailure.error().key == "PublicWorkSentinel" && sharedFailure.error().record == 197,
        "full knot chart also obeys the unchanged caller-wide N-1 ledger"
    );
    const auto characters = OutputCharacters(plan);
    for (std::size_t available : { characters, characters - 1 })
    {
        auto limits = Capacity();
        limits.outputCharacters = available;
        Ledger ledger;
        auto bounded = PlanPcurveLineWindow(generator, input, rectangle, limits, ledger.Callbacks());
        if (available == characters)
        {
            Require(
                bounded && bounded->has_value() && Fingerprint(**bounded) == fingerprint,
                "chart ratios and window ratios share one exact output byte allowance at N"
            );
        }
        else
        {
            Require(
                !bounded && bounded.error().key == "PcurveLineWindowOutputCapacity",
                "chart output must not allocate past shared N-1 ratio capacity"
            );
        }
    }
    {
        struct RoundingRestore
        {
            int original = std::fegetround();
            ~RoundingRestore()
            {
                std::fesetround(original);
            }
        } restore;
        Ledger changed;
        auto callbacks = changed.Callbacks();
        const auto original = callbacks.checkCancelled;
        callbacks.checkCancelled = [&, original]() -> Result<void>
        {
            auto checked = original();
            if (changed.checks == baseline.checks / 2)
            {
                std::fesetround(FE_UPWARD);
            }
            return checked;
        };
        auto environment = PlanPcurveLineWindow(generator, input, rectangle, Capacity(), callbacks);
        Require(
            !environment && environment.error().key == "PcurveLineWindowNumericEnvironment" && std::fegetround() == FE_UPWARD,
            "chart proof cannot continue when a successful callback changes its declared numeric environment"
        );
    }

    // A maximum-size paired sequence is an identity chart. This is a pure
    // knot-coordinate control, not a claim that these multiplicities describe
    // a qualified B-spline surface of any particular degree.
    std::vector<std::string_view> manySource(8190, "0");
    std::vector<double> manyNative(8190, 0.);
    std::fill(manySource.begin() + 4095, manySource.end(), "1");
    std::fill(manyNative.begin() + 4095, manyNative.end(), 1.);
    const std::array<std::string_view, 2> smallSourceV{ "-1", "3" };
    const Pair smallNativeV{ -1., 3. };
    const ExactDecimalPcurveLineGenerator identity{ { "0", "0" }, { "1", "1" } };
    Ledger maximum;
    auto full = PlanPcurveLineWindow(
        identity,
        PcurveLineKnotChartInput{ { manySource, smallSourceV }, { manyNative, smallNativeV } },
        { 0., -1., 1., 3. },
        Capacity(),
        maximum.Callbacks()
    );
    Require(
        full && full->has_value() && (**full).affineSourceChart->axes[0].knotsChecked == 8190,
        "exactly 8192 paired source/native knots can be checked within explicit scalar work limits"
    );
    manySource.push_back("1");
    manyNative.push_back(1.);
    Ledger overflow;
    auto over = PlanPcurveLineWindow(
        identity,
        PcurveLineKnotChartInput{ { manySource, smallSourceV }, { manyNative, smallNativeV } },
        { 0., -1., 1., 3. },
        Capacity(),
        overflow.Callbacks()
    );
    Require(
        !over && over.error().status == Status::ResourceLimit && over.error().key == "PcurveLineWindowKnotCapacity",
        "8193 paired expanded knots exceed capacity before traversal"
    );
}
void ClampedBoundaryRestriction()
{
    const std::array<std::string_view, 5> sourceU{ ".1", ".1", ".2", ".3", ".3" };
    const std::array<double, 5> nativeU{ .1, .1, .2, .3, .3 };
    const std::array<std::string_view, 4> sourceV{ "-1", "-1", "3", "3" };
    const std::array<double, 4> nativeV{ -1., -1., 3., 3. };
    const PcurveLineKnotChartInput input{ { sourceU, sourceV }, { nativeU, nativeV }, { 1, 1 } };
    const Rectangle rectangle{ .1, -1., .3, 3. };
    const ExactDecimalPcurveLineGenerator lower{ { ".1", "11" }, { "0", "-4" } };
    Ledger baseline;
    auto made = PlanPcurveLineWindow(lower, input, rectangle, Capacity(), baseline.Callbacks());
    if (!made)
    {
        throw std::runtime_error("public clamped boundary restriction unexpectedly failed: " + made.error().key);
    }
    Require(made->has_value() && (**made).affineSourceChart, "a clamped fixed boundary can be established independently of nonaffine interior knots");
    const auto& plan = **made;
    const auto& chart = *plan.affineSourceChart;
    Require(
        chart.axes[0].fixedSourceBoundary == 0 && !chart.axes[1].fixedSourceBoundary,
        "fixed-min use is explicit, variable V retains its whole-axis proof"
    );
    RatioIs(chart.axes[0].scale, "1", "1");
    RatioIs(chart.axes[0].offset, "1", "180143985094819840");
    RatioIs(chart.axes[0].sourceDomainExact[0], "1", "10");
    RatioIs(chart.axes[0].sourceDomainExact[1], "3", "10");
    RatioIs(plan.generatorWindowExact[0], "2", "1");
    RatioIs(plan.generatorWindowExact[1], "3", "1");
    Require(
        plan.uvEnds == std::array<Pair, 2>{ Pair{ .1, 3. }, Pair{ .1, -1. } } && !plan.hasUvRounding && chart.axes[0].knotsChecked == 5,
        "source boundary maps to the exact native boundary without inward movement, UV rounding or omitted knots"
    );
    CheckWholeIntersection(plan, { Binary(.1), Exact(11) }, { Exact(0), Exact(-4) }, rectangle);
    // Independent degree-one basis identity: at u=the clamped minimum,
    // N_0=1 and every other U basis function is zero; at the maximum only
    // the final basis is one. Any unchanged tensor control net, including
    // positive rational weights, therefore selects the same control row in
    // either source/native chart, regardless of the changed interior U knot.
    // The coordinate module reports this restricted identity, not a claim
    // that an actual caller surface's degree/control net was inspected here.
    const Exact mappedInterior = Read(chart.axes[0].scale) * Canonical(Integer(1), Integer(5)) + Read(chart.axes[0].offset);
    Require(mappedInterior != Binary(.2), "boundary-only receipt must not be mistaken for a whole-axis knot correspondence");
    Ledger upperLedger;
    auto upper = PlanPcurveLineWindow(
        ExactDecimalPcurveLineGenerator{ { ".3", "11" }, { "0", "-4" } },
        input,
        rectangle,
        Capacity(),
        upperLedger.Callbacks()
    );
    Require(
        upper && upper->has_value() && (**upper).affineSourceChart->axes[0].fixedSourceBoundary == 1,
        "maximum boundary gets its own exact last-row restriction"
    );
    RatioIs((**upper).affineSourceChart->axes[0].offset, "-1", "90071992547409920");
    Require((**upper).uvEnds == std::array<Pair, 2>{ Pair{ .3, 3. }, Pair{ .3, -1. } }, "upper boundary keeps source orientation and original q");

    for (const auto& generator :
         std::array<ExactDecimalPcurveLineGenerator, 4>{ ExactDecimalPcurveLineGenerator{ { ".2", "11" }, { "0", "-4" } },
                                                         ExactDecimalPcurveLineGenerator{ { "0.10000000000000001", "11" }, { "0", "-4" } },
                                                         ExactDecimalPcurveLineGenerator{ { ".1", "11" }, { "1", "-4" } },
                                                         ExactDecimalPcurveLineGenerator{ { ".1", "11" }, { "1E-400", "-4" } } })
    {
        Ledger denied;
        auto refused = PlanPcurveLineWindow(generator, input, rectangle, Capacity(), denied.Callbacks());
        Require(
            !refused && refused.error().status == Status::Indeterminate && refused.error().key == "PcurveLineWindowKnotChartUnestablished",
            "interior/near-boundary or exact nonzero direction cannot borrow fixed-boundary evidence"
        );
    }
    Ledger notAuthorized;
    auto refused = PlanPcurveLineWindow(
        lower,
        PcurveLineKnotChartInput{ { sourceU, sourceV }, { nativeU, nativeV } },
        rectangle,
        Capacity(),
        notAuthorized.Callbacks()
    );
    Require(
        !refused && refused.error().key == "PcurveLineWindowKnotChartUnestablished",
        "default degree zero retains the previous whole-axis-only contract"
    );
    Ledger degreeCount;
    refused = PlanPcurveLineWindow(
        lower,
        PcurveLineKnotChartInput{ { sourceU, sourceV }, { nativeU, nativeV }, { 2, 1 } },
        rectangle,
        Capacity(),
        degreeCount.Callbacks()
    );
    Require(
        !refused && refused.error().key == "PcurveLineWindowBoundaryRestrictionUnestablished",
        "expanded count must admit p+1 controls and two p+1 clamped ends"
    );

    const std::array<std::string_view, 5> unclampedSource{ ".1", ".15", ".2", ".3", ".3" };
    const std::array<double, 5> unclampedNative{ .1, .15, .2, .3, .3 };
    for (bool changeSource : { false, true })
    {
        const auto& source = changeSource ? unclampedSource : sourceU;
        const auto& native = changeSource ? nativeU : unclampedNative;
        Ledger unclamped;
        refused = PlanPcurveLineWindow(
            lower,
            PcurveLineKnotChartInput{ { source, sourceV }, { native, nativeV }, { 1, 1 } },
            rectangle,
            Capacity(),
            unclamped.Callbacks()
        );
        Require(
            !refused && refused.error().key == "PcurveLineWindowBoundaryRestrictionUnestablished",
            "both source and native endpoint multiplicities must independently be exactly p+1"
        );
    }
    const std::array<std::string_view, 7> illegalSource{ ".1", ".1", ".2", ".2", ".2", ".3", ".3" };
    const std::array<double, 7> illegalNative{ .1, .1, .2, .2, .2, .3, .3 };
    Ledger multiplicity;
    refused = PlanPcurveLineWindow(
        lower,
        PcurveLineKnotChartInput{ { illegalSource, sourceV }, { illegalNative, nativeV }, { 1, 1 } },
        rectangle,
        Capacity(),
        multiplicity.Callbacks()
    );
    Require(
        !refused && refused.error().key == "PcurveLineWindowBoundaryRestrictionUnestablished",
        "invalid internal multiplicity cannot establish even restricted boundary evidence"
    );
    const std::array<std::string_view, 6> mergedSource{ ".1", ".1", ".199999999999999999999", ".2", ".3", ".3" };
    const std::array<double, 6> mergedNative{ .1, .1, .2, .2, .3, .3 };
    Ledger merged;
    refused = PlanPcurveLineWindow(
        lower,
        PcurveLineKnotChartInput{ { mergedSource, sourceV }, { mergedNative, nativeV }, { 1, 1 } },
        rectangle,
        Capacity(),
        merged.Callbacks()
    );
    Require(
        !refused && refused.error().key == "PcurveLineWindowBoundaryRestrictionUnestablished",
        "valid clamped boundary identity cannot authorize merging distinct exact source interior knots into one native knot group"
    );

    const std::array<std::string_view, 5> nonaffineV{ "-1", "-1", ".1", "3", "3" };
    const std::array<double, 5> nonaffineNativeV{ -1., -1., .1, 3., 3. };
    Ledger variable;
    refused = PlanPcurveLineWindow(
        lower,
        PcurveLineKnotChartInput{ { sourceU, nonaffineV }, { nativeU, nonaffineNativeV }, { 1, 1 } },
        rectangle,
        Capacity(),
        variable.Callbacks()
    );
    Require(
        !refused && refused.error().key == "PcurveLineWindowKnotChartUnestablished",
        "fixed U boundary never waives the complete variable-axis knot proof"
    );

    const auto fingerprint = Fingerprint(plan);
    for (std::size_t count : { baseline.work, baseline.work - 1 })
    {
        auto limits = Capacity();
        limits.workUnits = count;
        Ledger bounded;
        auto result = PlanPcurveLineWindow(lower, input, rectangle, limits, bounded.Callbacks());
        if (count == baseline.work)
        {
            Require(
                result && result->has_value() && Fingerprint(**result) == fingerprint,
                "full-node fallback verification and receipt text succeed at exact N work"
            );
        }
        else
        {
            Require(!result && result.error().key == "PcurveLineWindowWorkLimit", "boundary fallback does not evade N-1 resource limits");
        }
    }
    for (std::size_t at : { std::size_t(1), baseline.checks / 2, baseline.checks })
    {
        Ledger cancelled;
        cancelled.cancelAt = at;
        auto result = PlanPcurveLineWindow(lower, input, rectangle, Capacity(), cancelled.Callbacks());
        Require(
            !result && result.error().key == "PublicCancelSentinel" && result.error().status == Status::Cancelled,
            "boundary proof remains cancellable before, during and after its full node checks"
        );
    }
}
void PendingBoundaryProposal()
{
    const std::array<std::string_view, 5> sourceU{ ".1", ".1", ".2", ".3", ".3" };
    const std::array<double, 5> nativeU{ .1, .1, .2, .3, .3 };
    const std::array<std::string_view, 4> sourceV{ "-1", "-1", "3", "3" };
    const std::array<double, 4> nativeV{ -1., -1., 3., 3. };
    const PcurveLineKnotChartInput input{ { sourceU, sourceV }, { nativeU, nativeV }, { 1, 1 } };
    const Rectangle rectangle{ .1, -1., .3, 3. };
    const ExactDecimalPcurveLineGenerator generator{ { "-.3", "3" }, { ".2", "0" } };
    Ledger strict;
    auto notProven = PlanPcurveLineWindow(generator, input, rectangle, Capacity(), strict.Callbacks());
    Require(
        !notProven && notProven.error().key == "PcurveLineWindowKnotChartUnestablished",
        "strict third overload still refuses nonaffine knots on the varying axis"
    );
    Ledger oldBoundary;
    auto alreadyEstablished = PlanPcurveLineWindow(
        ExactDecimalPcurveLineGenerator{ { ".1", "11" }, { "0", "-4" } },
        input,
        rectangle,
        Capacity(),
        oldBoundary.Callbacks()
    );
    Require(
        alreadyEstablished && alreadyEstablished->has_value() &&
            !(**alreadyEstablished).affineSourceChart->axes[0].requiresBoundaryCurveCertificate &&
            !(**alreadyEstablished).affineSourceChart->axes[1].requiresBoundaryCurveCertificate,
        "existing strict fixed-boundary/full-variable-axis proof never acquires a pending marker"
    );
    Ledger baseline;
    auto proposed = PlanBoundaryPcurveLineWindow(generator, input, rectangle, Capacity(), baseline.Callbacks());
    Require(proposed && proposed->has_value(), "nearest converted boundary spline can produce an unapproved proposal");
    const auto& pending = **proposed;
    const auto& plan = pending.ProposedWindow();
    Require(pending.FixedAxis() == 1 && pending.FixedEnd() == 1 && plan.affineSourceChart, "explicit fixed V maximum");
    const auto& axes = plan.affineSourceChart->axes;
    Require(
        !axes[0].fixedSourceBoundary && axes[0].requiresBoundaryCurveCertificate && axes[1].fixedSourceBoundary == 1 &&
            !axes[1].requiresBoundaryCurveCertificate && axes[0].knotsChecked == 5 && axes[1].knotsChecked == 4,
        "only variable U requires the missing continuous source-boundary curve certificate"
    );
    RatioIs(plan.generatorWindowExact[0], "2", "1");
    RatioIs(plan.generatorWindowExact[1], "3", "1");
    RatioIs(plan.generatorSpanExact, "1", "1");
    RatioIs(axes[1].scale, "1", "1");
    RatioIs(axes[1].offset, "0", "1");
    Require(
        plan.uvEnds == std::array<Pair, 2>{ Pair{ .1, 3. }, Pair{ .3, 3. } } && !plan.hasUvRounding && plan.maximumRoundingUpperUv == Pair{ 0., 0. },
        "full endpoint-domain window uses exact native boundaries without inward movement or endpoint rounding"
    );
    const Exact a = Read(axes[0].scale), b = Read(axes[0].offset);
    Require(a * Canonical(Integer(1), Integer(5)) + b != Binary(.2), "pending endpoint map is not an interior knot equivalence");
    CheckWholeIntersection(
        plan,
        { a * Canonical(Integer(-3), Integer(10)) + b, Exact(3) },
        { a * Canonical(Integer(1), Integer(5)), Exact(0) },
        rectangle
    );
    for (const auto& endpoint : plan.endpointRoundingUpperUv)
    {
        Require(endpoint == Pair{ 0., 0. }, "both axes of both pending native endpoints are exact");
    }

    Ledger lowerLedger;
    auto lower = PlanBoundaryPcurveLineWindow(
        ExactDecimalPcurveLineGenerator{ { "-.3", "-1" }, { ".2", "0" } },
        input,
        rectangle,
        Capacity(),
        lowerLedger.Callbacks()
    );
    Require(lower && lower->has_value() && (**lower).FixedEnd() == 0, "minimum fixed boundary is separately identified");
    Require((**lower).ProposedWindow().uvEnds == std::array<Pair, 2>{ Pair{ .1, -1. }, Pair{ .3, -1. } }, "fixed V minimum keeps all endpoints");
    Ledger reverseLedger;
    auto reverse = PlanBoundaryPcurveLineWindow(
        ExactDecimalPcurveLineGenerator{ { ".7", "3" }, { "-.2", "0" } },
        input,
        rectangle,
        Capacity(),
        reverseLedger.Callbacks()
    );
    Require(reverse && reverse->has_value(), "negative source U direction retains its original increasing q");
    RatioIs((**reverse).ProposedWindow().generatorWindowExact[0], "2", "1");
    RatioIs((**reverse).ProposedWindow().generatorWindowExact[1], "3", "1");
    Require(
        (**reverse).ProposedWindow().uvEnds == std::array<Pair, 2>{ Pair{ .3, 3. }, Pair{ .1, 3. } },
        "negative raw direction is never normalized or flipped"
    );
    Ledger swappedLedger;
    auto swapped = PlanBoundaryPcurveLineWindow(
        ExactDecimalPcurveLineGenerator{ { "3", "-.3" }, { "0", ".2" } },
        PcurveLineKnotChartInput{ { sourceV, sourceU }, { nativeV, nativeU }, { 1, 1 } },
        Rectangle{ -1., .1, 3., .3 },
        Capacity(),
        swappedLedger.Callbacks()
    );
    Require(
        swapped && swapped->has_value() && (**swapped).FixedAxis() == 0 && (**swapped).FixedEnd() == 1 &&
            (**swapped).ProposedWindow().affineSourceChart->axes[1].requiresBoundaryCurveCertificate,
        "fixed U and variable V follow the same symmetric proposal contract"
    );
    for (const auto& ineligible :
         std::array<ExactDecimalPcurveLineGenerator, 5>{ ExactDecimalPcurveLineGenerator{ { "-.3", "3.000000000000000000000001" }, { ".2", "0" } },
                                                         ExactDecimalPcurveLineGenerator{ { "-.3", "2" }, { ".2", "0" } },
                                                         ExactDecimalPcurveLineGenerator{ { "-.3", "3" }, { ".2", "1E-400" } },
                                                         ExactDecimalPcurveLineGenerator{ { "-.3", "3" }, { ".2", "1" } },
                                                         ExactDecimalPcurveLineGenerator{ { "-.3", "3" }, { "0", "0" } } })
    {
        Ledger absent;
        auto result = PlanBoundaryPcurveLineWindow(ineligible, input, rectangle, Capacity(), absent.Callbacks());
        Require(result && !result->has_value(), "noneligible, near-boundary and nonzero-near-zero directions return absence without snapping");
    }
    Ledger unauthenticated;
    auto noDegrees = PlanBoundaryPcurveLineWindow(
        generator,
        PcurveLineKnotChartInput{ { sourceU, sourceV }, { nativeU, nativeV } },
        rectangle,
        Capacity(),
        unauthenticated.Callbacks()
    );
    Require(noDegrees && !noDegrees->has_value(), "missing shared-degree attestation cannot yield a pending boundary proposal");

    const auto rejectStructure = [&](const PcurveLineKnotChartInput& changed, const char* message)
    {
        Ledger invalid;
        auto result = PlanBoundaryPcurveLineWindow(generator, changed, rectangle, Capacity(), invalid.Callbacks());
        Require(!result && result.error().key == "PcurveLineWindowBoundaryPendingStructure", message);
    };
    const std::array<std::string_view, 6> mergeU{ ".1", ".1", ".199999999999999999999", ".2", ".3", ".3" };
    const std::array<double, 6> mergedU{ .1, .1, .2, .2, .3, .3 };
    rejectStructure({ { mergeU, sourceV }, { mergedU, nativeV }, { 1, 1 } }, "nearest conversion cannot merge variable-axis source knot groups");
    const std::array<std::string_view, 6> splitU{ ".1", ".1", ".2", ".2", ".3", ".3" };
    const std::array<double, 6> splitNativeU{ .1, .1, .2, std::nextafter(.2, 1.), .3, .3 };
    rejectStructure({ { splitU, sourceV }, { splitNativeU, nativeV }, { 1, 1 } }, "pending plans cannot split source knot groups");
    const std::array<std::string_view, 6> mergeV{ "-1", "-1", ".199999999999999999999", ".2", "3", "3" };
    const std::array<double, 6> mergedV{ -1., -1., .2, .2, 3., 3. };
    rejectStructure({ { sourceU, mergeV }, { nativeU, mergedV }, { 1, 1 } }, "fixed-axis source knot groups also remain intact");
    const std::array<std::string_view, 5> nonclampedSource{ ".1", ".15", ".2", ".3", ".3" };
    const std::array<double, 5> nonclampedNative{ .1, .15, .2, .3, .3 };
    rejectStructure({ { nonclampedSource, sourceV }, { nativeU, nativeV }, { 1, 1 } }, "source array must be exactly clamped");
    rejectStructure({ { sourceU, sourceV }, { nonclampedNative, nativeV }, { 1, 1 } }, "actual native array must be exactly clamped");
    rejectStructure({ { sourceU, sourceV }, { nativeU, nativeV }, { 2, 1 } }, "degree attestation must agree with full clamped counts");
    auto notNearest = nativeU;
    notNearest[2] = std::nextafter(.2, 1.);
    Ledger conversion;
    auto changedKnot = PlanBoundaryPcurveLineWindow(
        generator,
        PcurveLineKnotChartInput{ { sourceU, sourceV }, { notNearest, nativeV }, { 1, 1 } },
        rectangle,
        Capacity(),
        conversion.Callbacks()
    );
    Require(
        !changedKnot && changedKnot.error().key == "PcurveLineWindowBoundaryPendingKnotConversion",
        "well-structured but non-nearest native variable knot is a distinct error"
    );
    const std::array<double, 4> shortNative{ .1, .1, .3, .3 };
    Ledger shape;
    auto badShape = PlanBoundaryPcurveLineWindow(
        generator,
        PcurveLineKnotChartInput{ { sourceU, sourceV }, { shortNative, nativeV }, { 1, 1 } },
        rectangle,
        Capacity(),
        shape.Callbacks()
    );
    Require(!badShape && badShape.error().key == "PcurveLineWindowKnotChartStructure", "mismatched full arrays remain errors, not normal absence");

    const auto fingerprint = Fingerprint(plan);
    for (std::size_t count : { baseline.work, baseline.work - 1 })
    {
        auto limits = Capacity();
        limits.workUnits = count;
        Ledger bounded;
        auto result = PlanBoundaryPcurveLineWindow(generator, input, rectangle, limits, bounded.Callbacks());
        if (count == baseline.work)
        {
            Require(
                result && result->has_value() && Fingerprint((**result).ProposedWindow()) == fingerprint,
                "one exact ledger includes all pending full-node checks, rounding, intersection and receipt output"
            );
        }
        else
        {
            Require(!result && result.error().key == "PcurveLineWindowWorkLimit", "pending N-1 does not turn a resource failure into absence");
        }
    }
    for (std::size_t at : { std::size_t(1), baseline.checks / 2, baseline.checks })
    {
        Ledger cancelled;
        cancelled.cancelAt = at;
        auto result = PlanBoundaryPcurveLineWindow(generator, input, rectangle, Capacity(), cancelled.Callbacks());
        Require(
            !result && result.error().status == Status::Cancelled && result.error().key == "PublicCancelSentinel" && result.error().record == 197,
            "pending preparation, middle work and final handoff preserve cancellation identity"
        );
    }
    auto outputLimit = Capacity();
    outputLimit.outputCharacters = OutputCharacters(plan) - 1;
    Ledger output;
    auto noReceipt = PlanBoundaryPcurveLineWindow(generator, input, rectangle, outputLimit, output.Callbacks());
    Require(!noReceipt && noReceipt.error().key == "PcurveLineWindowOutputCapacity", "pending receipt shares the finite output allowance");
    Ledger exceptional;
    auto callbacks = exceptional.Callbacks();
    callbacks.chargeWork = [](std::size_t) -> Result<void>
    {
        throw std::overflow_error("pending caller overflow sentinel");
    };
    bool propagated = false;
    try
    {
        (void)PlanBoundaryPcurveLineWindow(generator, input, rectangle, Capacity(), callbacks);
    }
    catch (const std::overflow_error& error)
    {
        propagated = std::string(error.what()) == "pending caller overflow sentinel";
    }
    Require(propagated, "pending planning never translates a real callback exception into evidence refusal");
    auto owned = pending;
    proposed = {};
    Require(Fingerprint(owned.ProposedWindow()) == fingerprint, "pending value owns its complete proposal after result release");
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
        WholeKnotAffineChart();
        ClampedBoundaryRestriction();
        PendingBoundaryProposal();
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
