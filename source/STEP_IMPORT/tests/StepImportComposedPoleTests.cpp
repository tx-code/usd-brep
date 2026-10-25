// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportComposedPole.h"

#include <cfenv>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace
{
using namespace step_import;
using namespace step_import::detail;
using namespace step_import::detail::terminal_spline;
void Require(bool value, const char* why)
{
    if (!value)
    {
        throw std::runtime_error(why);
    }
}
terminal_spline::Limits Capacity()
{
    return { 200000, 4096, 1234 };
}
Result<void> Work(std::size_t)
{
    return {};
}
BinaryNet Line(std::array<double, 3> a, std::array<double, 3> b)
{
    return { { 1, 0, 2, 1, { 0, 0, 1, 1 }, {} }, { { a[0], a[1], a[2], 1 }, { b[0], b[1], b[2], 1 } } };
}
RevolutionCurveDefinition Cone()
{
    RevolutionCurveDefinition c;
    c.uv = Line({ 0, 0, 0 }, { 90, 1, 0 });
    c.generator = Line({ 1, 0, 0 }, { 0, 0, 1 });
    c.frame = { std::array{ 1., 0., 0. }, std::array{ 0., 1., 0. }, std::array{ 0., 0., 1. } };
    c.stepDomain = { 0, 0, 360, 1 };
    c.angleNumerator = std::numbers::pi;
    return c;
}
PoleVisits End()
{
    return { { 0, 1 }, { 0, 0, 1 }, .125, false, true };
}
void Check(const RevolutionCurveDefinition& curve, const PoleVisits& visits, bool expected, const char* why)
{
    const auto before = curve.uv.homogeneous, genBefore = curve.generator.homogeneous;
    auto proof = CheckComposedPoleVisits(curve, visits, Capacity(), Work);
    if (!proof)
    {
        std::cerr << why << ": " << proof.error().key << '\n';
    }
    Require(proof && proof->established == expected, why);
    Require(proof->regions > 0, "the complete defining composition is bounded");
    Require(curve.uv.homogeneous == before && curve.generator.homogeneous == genBefore, "no definition changes");
}
void GeometryCases()
{
    auto curve = Cone();
    Check(curve, End(), true, "nonconstant U and V reaches only the authorized last pole ball");
    auto query = End();
    query.lastAllowed = false;
    Check(curve, query, false, "an original endpoint marker is required");
    Check(curve, { { 0, .5 }, { 0, 0, 1 }, .125 }, true, "selected subinterval stays outside");
    Check(curve, { { .99, 1 }, { 0, 0, 1 }, .125, true, true }, false, "an edge wholly inside the ball is unestablished");
    curve.uv = Line({ 90, 1, 0 }, { 0, 0, 0 });
    Check(curve, { { 0, 1 }, { 0, 0, 1 }, .125, true, false }, true, "reverse departure preserves endpoint meaning");
    curve.uv = Line({ 0, 0, 0 }, { 360, 0, 0 });
    Check(curve, { { 0, 1 }, { 0, 0, 1 }, .125 }, true, "a complete circumferential image stays outside the pole ball");
    curve = Cone();
    curve.uv = { { 2, 0, 3, 1, { 0, 0, 0, 1, 1, 1 }, {} }, { { 0, 0, 0, 1 }, { 120, 1.5, 0, 2 }, { 90, 1, 0, 1 } } };
    Check(curve, End(), true, "unequal positive UV weights retain rational V and angle speed");
    curve.generator.homogeneous[0] = { 2, 0, 0, 2 };
    Check(curve, End(), true, "unequal positive generator weights are defining data");
    curve = Cone();
    curve.generator = { { 1, 0, 3, 1, { 0, 0, .5, 1, 1 }, {} }, { { 1, 0, 0, 1 }, { .5, 0, .5, 1 }, { 0, 0, 1, 1 } } };
    Check(curve, End(), true, "V crossing a generator knot leaves no unchecked interval");
    curve.uv = { { 1, 0, 3, 1, { 0, 0, .5, 1, 1 }, {} }, { { 0, 0, 0, 1 }, { 45, .5, 0, 1 }, { 90, 1, 0, 1 } } };
    Check(curve, End(), true, "selected UV knot spans retain ordered C0 continuity");
    curve = Cone();
    curve.uv = { { 1, 0, 4, 1, { 0, 0, 1, 2, 3, 3 }, {} }, { { 0, 0, 0, 1 }, { 30, 1, 0, 1 }, { 60, 0, 0, 1 }, { 90, 1, 0, 1 } } };
    Check(curve, { { 0, 3 }, { 0, 0, 1 }, .125, false, true }, false, "an interior visit and return cannot use the last endpoint marker");
    curve = Cone();
    curve.generator = Line({ 1, 0, 0 }, { 0, 0, 0 });
    curve.uv = { { 2, 0, 3, 1, { 0, 0, 0, 1, 1, 1 }, {} }, { { 0, .5, 0, 1 }, { 45, 1, 0, 1 }, { 90, .5, 0, 1 } } };
    Check(curve, { { 0, 1 }, { 0, 0, 0 }, .25 }, false, "isolated interior contact with the CLOSED ball is not outside");
    curve = Cone();
    curve.uv = { { 2, 0, 3, 1, { 0, 0, 0, 1, 1, 1 }, {} }, { { 0, 0, 0, 1 }, { 4500, 100, 0, 100 }, { 90, 0, 0, 1 } } };
    Check(curve, { { 0, 1 }, { 0, 0, 1 }, .125 }, false, "a rational interior excursion cannot hide between endpoints");
    curve = Cone();
    curve.uv = { { 2, 0, 3, 1, { 0, 0, 0, 1, 1, 1 }, {} }, { { 0, 0, 0, 1 }, { 45, 1, 0, 1 }, { 9000, 0, 0, 100 } } };
    // At t=1/11, V=1/11: distance^2=200/121 < (13/10)^2.
    // Both endpoints have distance^2=2. A numerator-only derivative would miss
    // the rational extremum and can falsely classify a whole prefix outside.
    Check(curve, { { 0, 1 }, { 0, 0, 1 }, 1.3 }, false, "independent rational extremum counterexample");
    curve = Cone();
    curve.originMm = { 128, -64, 32 };
    curve.frame = { std::array{ 0., 1., 0. }, std::array{ 0., 0., 1. }, std::array{ 1., 0., 0. } };
    curve.generator = Line({ 128, -63, 32 }, { 129, -64, 32 });
    Check(
        curve,
        { { 0, 1 }, { 129, -64 + 1. / 4096, 32 }, .125, false, true },
        true,
        "translated arbitrary-axis frame and off-axis native pole center"
    );
    curve.frame[0][1] += std::ldexp(1., -30);
    curve.frame[2][1] = std::ldexp(1., -31);
    Check(curve, { { 0, 1 }, { 129, -64 + 1. / 4096, 32 }, .125, false, true }, true, "stored frame need not be exactly orthonormal");
    curve = Cone();
    curve.frame[0][0] = .25;
    curve.uv = Line({ 0, 0, 0 }, { 180, 0, 0 });
    Check(curve, { { 0, 1 }, { 0, 0, 0 }, .125 }, false, "nonorthogonal frame residuals can create a forbidden ball visit");
    Check(curve, { { 0, 1 }, { 0, 0, 0 }, .01 }, true, "a conservative nonorthogonal-frame image can still stay outside");
}
void ContractCases()
{
    auto curve = Cone();
    auto query = End();
    std::size_t total = 0;
    auto counted = [&](std::size_t n) -> Result<void>
    {
        total += n;
        return {};
    };
    auto proof = CheckComposedPoleVisits(curve, query, Capacity(), counted);
    Require(proof && proof->established && total > 0, "bounded work accounting");
    const Failure limit{ Status::ResourceLimit, "PublicComposedQuota", "original work failure", 17, "PublicQuota" };
    for (const auto budget : { total, total - 1 })
    {
        std::size_t used = 0;
        auto bounded = [&](std::size_t n) -> Result<void>
        {
            if (n > budget - used)
            {
                return tl::unexpected(limit);
            }
            used += n;
            return {};
        };
        proof = CheckComposedPoleVisits(curve, query, Capacity(), bounded);
        Require(
            budget == total ? bool(proof && proof->established) :
                              bool(!proof && proof.error().key == limit.key && proof.error().record == limit.record),
            "N/N-1 work retains the caller's exact failure"
        );
    }
    const Failure cancel{ Status::Cancelled, "PublicComposedCancel", "original cancel", 23, "PublicCancel" };
    proof = CheckComposedPoleVisits(
        curve,
        query,
        Capacity(),
        [&](std::size_t) -> Result<void>
        {
            return tl::unexpected(cancel);
        }
    );
    Require(!proof && proof.error().key == cancel.key && proof.error().stage == cancel.stage, "cooperative cancellation retains its diagnostic");
    std::size_t entered = 0;
    proof = CheckComposedPoleVisits(
        curve,
        query,
        Capacity(),
        [&](std::size_t units) -> Result<void>
        {
            entered += units;
            return entered > 20000 ? Result<void>(tl::unexpected(cancel)) : Result<void>{};
        }
    );
    Require(!proof && proof.error().key == cancel.key && entered > 20000, "cancellation after preparation cannot publish a partial proof");
    bool threw = false;
    try
    {
        (void)CheckComposedPoleVisits(
            curve,
            query,
            Capacity(),
            [](std::size_t) -> Result<void>
            {
                throw std::overflow_error("caller");
            }
        );
    }
    catch (const std::overflow_error&)
    {
        threw = true;
    }
    Require(threw, "caller arithmetic exceptions must not be recast as interval capacity");
    auto tiny = Capacity();
    tiny.controlSlots = 2;
    proof = CheckComposedPoleVisits(curve, query, tiny, Work);
    Require(!proof && proof.error().status == Status::ResourceLimit, "resident net and scratch capacity");
    curve.uv.homogeneous[0][3] = 0;
    Require(!CheckComposedPoleVisits(curve, query, Capacity(), Work), "nonpositive weights are unestablished");
    curve = Cone();
    curve.uv.homogeneous[0][1] = -.01;
    proof = CheckComposedPoleVisits(curve, query, Capacity(), Work);
    Require(!proof && proof.error().key == "GeneralComposedPoleControlHullCoverage", "no silently clamped V definition");
    curve = Cone();
    query.interval = { -1, 1 };
    Require(!CheckComposedPoleVisits(curve, query, Capacity(), Work), "selected t cannot extrapolate the actual UV curve");
    curve = Cone();
    query = End();
    const int old = std::fegetround();
    if (std::fesetround(FE_UPWARD) == 0)
    {
        proof = CheckComposedPoleVisits(curve, query, Capacity(), Work);
        std::fesetround(old);
        Require(!proof && proof.error().key == "GeneralComposedPoleNumericEnvironment", "unsupported rounding cannot certify geometry");
    }
    unsigned callbacks = 0;
    proof = CheckComposedPoleVisits(
        curve,
        query,
        Capacity(),
        [&](std::size_t) -> Result<void>
        {
            if (++callbacks == 8)
            {
                std::fesetround(FE_UPWARD);
            }
            return {};
        }
    );
    std::fesetround(old);
    Require(
        !proof && callbacks == 8 && proof.error().key == "GeneralComposedPoleNumericEnvironment",
        "a callback cannot invalidate the floating environment after the entry probe"
    );
}
void RestrictedHullCases()
{
    auto curve = Cone();
    curve.uv = { { 2, 0, 3, 1, { 0, 0, 0, 1, 1, 1 }, {} }, { { 0, 0, 0, 1 }, { 120, 1.5, 0, 2 }, { 90, 1, 0, 1 } } };
    curve.generator.homogeneous[0] = { 2, 0, 0, 2 };
    auto query = End();
    query.radiusMm = .001;
    // Independently, V(t)=(3t-2t^2)/(1+2t-2t^2), with derivative numerator
    // 2(t-1)^2+1 > 0. The weighted cone has R=2(1-V)/(2-V), Z=V/(2-V).
    // Distance to (0,0,1) is sqrt(2)*R, strictly decreasing to zero: exactly
    // one last-endpoint ball component. No interval-code oracle is repeated.
    std::size_t used = 0, callbacks = 0;
    const auto counted = [&](std::size_t n) -> Result<void>
    {
        if (n > 500000 - used)
        {
            return tl::unexpected(Failure{ Status::ResourceLimit, "PublicRestrictedHullBudget", "", {}, "PublicQuota" });
        }
        used += n;
        ++callbacks;
        return {};
    };
    auto proof = CheckComposedPoleVisits(curve, query, Capacity(), counted);
    Require(proof && proof->established && callbacks > 32, "recursive rational proof fits unchanged bounded work, without redundant endpoint arithmetic");
    for (unsigned fault = 0; fault < 3; ++fault)
    {
        const int saved = std::fegetround();
        std::size_t count = 0;
        bool threw = false;
        try
        {
            proof = CheckComposedPoleVisits(curve, query, Capacity(), [&](std::size_t) -> Result<void>
            {
                if (++count == callbacks - 2)
                {
                    if (fault == 0) { return tl::unexpected(Failure{ Status::Cancelled, "LateCachedCancellation", "", 19, "PublicQuota" }); }
                    if (fault == 1) { std::fesetround(FE_UPWARD); }
                    if (fault == 2) { throw std::domain_error("LateCallerDomainError"); }
                }
                return {};
            });
        }
        catch (const std::domain_error& error) { threw = std::string(error.what()) == "LateCallerDomainError"; }
        std::fesetround(saved);
        Require(count == callbacks - 2, "late callback executes after reusable endpoint bounds exist");
        Require(fault == 2 ? threw : (!proof && proof.error().key == (fault == 0 ? "LateCachedCancellation" : "GeneralComposedPoleNumericEnvironment")),
                "cached endpoints retain late cancellation, FENV and actual exception contracts");
    }
    for (auto& knot : curve.uv.basis.knotsU) { knot = -2 + 9 * knot; }
    query.interval = { -1.3, 6.7 };
    query.radiusMm = .125;
    Check(curve, query, true, "nonzero natural domain and non-dyadic interior restriction fractions");
    curve = Cone();
    curve.uv = { { 2, 0, 5, 1, { 0, 0, 0, .5, .5, 1, 1, 1 }, {} },
                 { { 0, 0, 0, 1 }, { 22.5, .25, 0, 1 }, { 45, .5, 0, 1 }, { 67.5, .75, 0, 1 }, { 90, 1, 0, 1 } } };
    curve.generator = { { 2, 0, 5, 1, { 0, 0, 0, .5, .5, 1, 1, 1 }, {} },
                        { { 1, 0, 0, 1 }, { .75, 0, .25, 1 }, { .5, 0, .5, 1 }, { .25, 0, .75, 1 }, { 0, 0, 1, 1 } } };
    for (unsigned flags = 0; flags < 4; ++flags)
    {
        query = End();
        query.firstAllowed = (flags & 1) != 0;
        query.lastAllowed = (flags & 2) != 0;
        Check(curve, query, query.lastAllowed, "C0 multiple knots with exact midpoint and independent endpoint permissions");
    }
    query.interval = { .5, 1 };
    query.firstAllowed = false;
    query.lastAllowed = true;
    Check(curve, query, true, "selected endpoint at a C0 multiple knot retains both derivative sides");
}
void IndependentPolylineOracle()
{
    // For this independently specified unit cone and pole (0,0,1),
    // distance^2=2*(1-V)^2 for EVERY angle. Each linear V segment has no
    // hidden extremum. With controls V in {0,1} and r=1/8, connected runs of
    // V=1 vertices therefore characterize all closed-ball components exactly.
    // This oracle does not repeat interval evaluation or its state machine.
    for (unsigned mask = 0; mask < 32; ++mask)
    {
        for (unsigned allowed = 0; allowed < 4; ++allowed)
        {
            auto curve = Cone();
            curve.uv.basis = { 1, 0, 5, 1, { 0, 0, 1, 2, 3, 4, 4 }, {} };
            curve.uv.homogeneous.clear();
            for (unsigned i = 0; i < 5; ++i)
            {
                curve.uv.homogeneous.push_back({ double(40 * i), double((mask >> i) & 1), 0, 1 });
            }
            bool expected = mask != 31;
            for (unsigned first = 0; first < 5;)
            {
                if (!((mask >> first) & 1))
                {
                    ++first;
                    continue;
                }
                unsigned last = first;
                while (last + 1 < 5 && ((mask >> (last + 1)) & 1))
                {
                    ++last;
                }
                expected = expected && ((first == 0 && (allowed & 1)) || (last == 4 && (allowed & 2)));
                first = last + 1;
            }
            Check(
                curve,
                { { 0, 4 }, { 0, 0, 1 }, .125, bool(allowed & 1), bool(allowed & 2) },
                expected,
                "independent analytic polyline component oracle"
            );
        }
    }
}
} // namespace
int main()
{
    try
    {
        GeometryCases();
        IndependentPolylineOracle();
        ContractCases();
        RestrictedHullCases();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
