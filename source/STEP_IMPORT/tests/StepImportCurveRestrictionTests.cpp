// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportTerminalSpline.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

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
terminal_spline::Limits Capacity()
{
    return { 200000, 4096, 1234 };
}
Result<void> Work(std::size_t)
{
    return {};
}
Source Quadratic(bool rational = false)
{
    Source source;
    source.u = { 2, { "2", "5" }, { 3, 3 }, {} };
    source.countU = 3;
    source.controls = { { "0", "0", "0" }, { "1", "2", "0" }, { "2", "0", "0" } };
    if (rational)
    {
        source.weights = { "1", "2", "1" };
    }
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
BinaryNet RestrictedQuadratic(bool rational = false)
{
    // Independent quadratic blossom at a=1/4, b=3/4 in s=(t-2)/3.
    // Polynomial C(s)=(2s,4s(1-s),0): C(a)=(.5,.75,0), C(b)=(1.5,.75,0).
    // With weights (1,2,1), homogeneous C has coordinates
    // (4s-2s^2,8s-8s^2,0,1+2s-2s^2). Its Euclidean ends are
    // (7/11,12/11,0)/(15/11,12/11,0), and C(1/2)=(1,4/3,0).
    BinaryNet net;
    net.basis = { 2, 0, 3, 1, { 2.75, 2.75, 2.75, 4.25, 4.25, 4.25 }, {} };
    net.homogeneous = rational ? std::vector<std::array<double, 4>>{ { .875, 1.5, 0, 1.375 }, { 1.625, 2.5, 0, 1.625 }, { 1.875, 1.5, 0, 1.375 } } :
                                 std::vector<std::array<double, 4>>{ { .5, .75, 0, 1 }, { 1, 1.25, 0, 1 }, { 1.5, .75, 0, 1 } };
    return net;
}
bool Same(const BinaryNet& a, const BinaryNet& b)
{
    return a.basis.degreeU == b.basis.degreeU && a.basis.degreeV == b.basis.degreeV && a.basis.countU == b.basis.countU &&
           a.basis.countV == b.basis.countV && a.basis.knotsU == b.basis.knotsU && a.basis.knotsV == b.basis.knotsV && a.homogeneous == b.homogeneous;
}
void RequireExact(const Result<Proof>& proof, std::size_t patches, const char* message)
{
    Require(proof && proof->established && proof->maximumMm && *proof->maximumMm == 0 && proof->patches == patches, message);
}
void AnalyticQuadratics()
{
    for (bool rational : { false, true })
    {
        const auto source = Quadratic(rational);
        const auto originalControls = source.controls;
        const auto made = Extend(source, Capacity(), Work);
        Require(bool(made), "original public quadratic reference");
        auto actual = RestrictedQuadratic(rational);
        const auto before = actual;
        auto proof = CertifyCurveRestriction(made->reference, actual, { "1", "1000000000000" }, Capacity(), Work);
        RequireExact(proof, 1, "independent blossom gives a whole-subdomain zero-error certificate");
        Require(Same(actual, before) && source.controls == originalControls, "proof cannot edit either input definition");
        const auto ordinary = Certify(made->reference, actual, { "1", "1000000000000" }, Capacity(), Work);
        Require(!ordinary && ordinary.error().key == "TerminalSplineActualDomainOrDegree", "ordinary whole-domain Certify still refuses a subdomain");
        actual.homogeneous[1][1] += .125;
        proof = CertifyCurveRestriction(made->reference, actual, { "1", "1000000000000" }, Capacity(), Work);
        Require(
            proof && !proof->established && proof->maximumMm && *proof->maximumMm > 1e-12,
            "interior corruption with unchanged endpoints fails the continuous bound"
        );
        auto differentParameter = before;
        differentParameter.basis.knotsU = { 3, 3, 3, 4, 4, 4 };
        proof = CertifyCurveRestriction(made->reference, differentParameter, { "1", "1000000000000" }, Capacity(), Work);
        Require(proof && !proof->established, "the same locus reparameterized to another interval cannot borrow a source-t identity");
        const auto whole = Stored(made->target);
        std::size_t ordinaryWork = 0, restrictionWork = 0;
        const auto wholeProof = Certify(
            made->reference,
            whole,
            { "1", "1000000000000" },
            Capacity(),
            [&](std::size_t units) -> Result<void>
            {
                ordinaryWork += units;
                return {};
            }
        );
        proof = CertifyCurveRestriction(
            made->reference,
            whole,
            { "1", "1000000000000" },
            Capacity(),
            [&](std::size_t units) -> Result<void>
            {
                restrictionWork += units;
                return {};
            }
        );
        RequireExact(wholeProof, 1, "ordinary complete source identity survives restriction calls and corruption refusals");
        RequireExact(proof, 1, "whole-domain identity is also a valid restriction");
        Require(ordinaryWork == restrictionWork, "whole-domain shared engine preserves the existing proof accounting");
    }
}
void MultipleSpans()
{
    Source source;
    source.u = { 1, { "0", "1", "2", "3" }, { 2, 1, 1, 2 }, {} };
    source.countU = 4;
    source.controls = { { "0", "0", "0" }, { "1", "2", "0" }, { "2", "-1", "0" }, { "4", "1", "0" } };
    const auto made = Extend(source, Capacity(), Work);
    Require(bool(made), "public three-span polyline reference");
    const BinaryNet actual{ { 1, 0, 4, 1, { .5, .5, 1, 2, 2.5, 2.5 }, {} }, { { .5, 1, 0, 1 }, { 1, 2, 0, 1 }, { 2, -1, 0, 1 }, { 3, 0, 0, 1 } } };
    RequireExact(
        CertifyCurveRestriction(made->reference, actual, { "1", "1000000000000" }, Capacity(), Work),
        3,
        "all selected C0 spans and both interior knots contribute to the proof"
    );
    // A target knot absent from the source forces common refinement on both
    // definitions; splitting this affine first piece does not change its t.
    const BinaryNet refined{ { 1, 0, 5, 1, { .5, .5, .75, 1, 2, 2.5, 2.5 }, {} },
                             { { .5, 1, 0, 1 }, { .75, 1.5, 0, 1 }, { 1, 2, 0, 1 }, { 2, -1, 0, 1 }, { 3, 0, 0, 1 } } };
    RequireExact(
        CertifyCurveRestriction(made->reference, refined, { "1", "1000000000000" }, Capacity(), Work),
        4,
        "source and target with distinct interior knots compare through exact common refinement"
    );
    const BinaryNet knotBounded{ { 1, 0, 2, 1, { 1, 1, 2, 2 }, {} }, { { 1, 2, 0, 1 }, { 2, -1, 0, 1 } } };
    RequireExact(
        CertifyCurveRestriction(made->reference, knotBounded, { "1", "1000000000000" }, Capacity(), Work),
        1,
        "original degree-multiplicity interior knots need no invented boundary poles"
    );
    const auto quadratic = Extend(Quadratic(), Capacity(), Work);
    Require(bool(quadratic), "quadratic reference for one-sided cuts");
    const BinaryNet prefix{ { 2, 0, 3, 1, { 2, 2, 2, 3.5, 3.5, 3.5 }, {} }, { { 0, 0, 0, 1 }, { .5, 1, 0, 1 }, { 1, 1, 0, 1 } } };
    const BinaryNet suffix{ { 2, 0, 3, 1, { 3.5, 3.5, 3.5, 5, 5, 5 }, {} }, { { 1, 1, 0, 1 }, { 1.5, 1, 0, 1 }, { 2, 0, 0, 1 } } };
    RequireExact(
        CertifyCurveRestriction(quadratic->reference, prefix, { "1", "1000000000000" }, Capacity(), Work),
        1,
        "restriction retains the original clamped first endpoint"
    );
    RequireExact(
        CertifyCurveRestriction(quadratic->reference, suffix, { "1", "1000000000000" }, Capacity(), Work),
        1,
        "restriction retains the original clamped last endpoint"
    );
}
void DecimalSourceDomain()
{
    Source source;
    source.u = { 2, { "0.1", "0.9" }, { 3, 3 }, {} };
    source.countU = 3;
    source.controls = { { "0.1", "0.2", "0" }, { "1.1", "2.2", "0" }, { "2.1", "0.2", "0" } };
    source.lengthToMm = { "25", "2" };
    const auto made = Extend(source, Capacity(), Work);
    Require(bool(made), "exact nonbinary decimal source and exact physical length ratio");
    // Independently: s=(t-.1)/.8, physical C=(1.25+25s,2.5+50s(1-s),0).
    // At t=.25/.75, s=3/16 and 13/16. These blossom coefficients are dyadic
    // despite the nonbinary source parameters, controls and unit scale.
    const BinaryNet actual{ { 2, 0, 3, 1, { .25, .25, .25, .75, .75, .75 }, {} },
                            { { 5.9375, 10.1171875, 0, 1 }, { 13.75, 19.8828125, 0, 1 }, { 21.5625, 10.1171875, 0, 1 } } };
    RequireExact(
        CertifyCurveRestriction(made->reference, actual, { "1", "1000000000000" }, Capacity(), Work),
        1,
        "original decimal arithmetic and source-to-mm conversion precede exact restriction"
    );
    const auto envelope = Stored(made->target);
    Require(envelope.basis.knotsU.front() < .1, "Extend has an outward lower computational bound");
    const auto ordinary = Certify(made->reference, envelope, { "1", "1000000000" }, Capacity(), Work);
    Require(ordinary && ordinary->established, "ordinary Certify retains its computational envelope contract");
    const auto refused = CertifyCurveRestriction(made->reference, envelope, { "1", "1000000000" }, Capacity(), Work);
    Require(
        !refused && refused.error().key == "TerminalSplineRestrictionOutsideSourceDomain",
        "the outward envelope is never mistaken for the original exact source domain"
    );
}
void Refusals()
{
    const auto made = Extend(Quadratic(), Capacity(), Work);
    Require(bool(made), "reference for public refusal controls");
    const auto check = [&](BinaryNet actual)
    {
        const auto before = actual;
        const auto result = CertifyCurveRestriction(made->reference, actual, { "1", "1000000000" }, Capacity(), Work);
        Require(!result && Same(actual, before), "unsupported or invalid restriction withholds evidence without edits");
    };
    auto actual = RestrictedQuadratic();
    actual.basis.knotsU = { 1, 1, 1, 4.25, 4.25, 4.25 };
    check(actual);
    actual = RestrictedQuadratic();
    actual.basis.knotsU = { 2.75, 2.75, 2.75, 6, 6, 6 };
    check(actual);
    actual = RestrictedQuadratic();
    actual.basis.knotsU.assign(6, 3);
    check(actual);
    actual = RestrictedQuadratic();
    actual.basis.knotsU = { 4.25, 4.25, 4.25, 2.75, 2.75, 2.75 };
    check(actual);
    actual = RestrictedQuadratic();
    actual.basis.knotsU.front() = 2;
    check(actual); // Not a clamped basis, even though its natural interval is increasing.
    actual = RestrictedQuadratic();
    actual.homogeneous[1][3] = 0;
    check(actual);
    actual.homogeneous[1][3] = -1;
    check(actual);
    actual = RestrictedQuadratic();
    actual.homogeneous[0][0] = std::numeric_limits<double>::infinity();
    check(actual);
    actual = RestrictedQuadratic();
    actual.basis.degreeV = 1;
    check(actual);
    actual = { { 1, 0, 2, 1, { 2.75, 2.75, 4.25, 4.25 }, {} }, { { .5, .75, 0, 1 }, { 1.5, .75, 0, 1 } } };
    check(actual); // Degree conversion cannot masquerade as the unchanged definition.
    Require(!CertifyCurveRestriction({}, RestrictedQuadratic(), { "1", "1000000000" }, Capacity(), Work), "missing reference refuses");
    auto extendedSource = Quadratic();
    extendedSource.u.requested = std::array<std::string, 2>{ "2", "6" };
    const auto extended = Extend(extendedSource, Capacity(), Work);
    Require(bool(extended), "ordinary explicit extension remains available");
    const auto extendedProof = Certify(extended->reference, Stored(extended->target), { "1", "1000000000" }, Capacity(), Work);
    Require(extendedProof && extendedProof->established, "original extension proof remains available");
    const auto refused = CertifyCurveRestriction(extended->reference, RestrictedQuadratic(), { "1", "1000000000" }, Capacity(), Work);
    Require(!refused && refused.error().key == "TerminalSplineRestrictionCurveReference", "interpreted reference is not original-source evidence");
}
void Boundaries()
{
    const auto source = Quadratic(true);
    const auto actual = RestrictedQuadratic(true);
    std::size_t prepareUnits = 0;
    const auto run = [&](std::size_t maximum, std::size_t& spent) -> Result<Proof>
    {
        const Charge charged = [&](std::size_t units) -> Result<void>
        {
            if (units > maximum - spent)
            {
                return tl::unexpected(Failure{ Status::ResourceLimit, "RestrictionCallerBudget", "public cumulative limit", 42, "PublicBudget" });
            }
            spent += units;
            return {};
        };
        const auto prepared = Extend(source, Capacity(), charged);
        if (!prepared)
        {
            return tl::unexpected(prepared.error());
        }
        prepareUnits = spent;
        return CertifyCurveRestriction(prepared->reference, actual, { "1", "1000000000000" }, Capacity(), charged);
    };
    std::size_t total = 0;
    RequireExact(run(std::numeric_limits<std::size_t>::max(), total), 1, "combined source preparation and restriction proof");
    Require(total > prepareUnits && prepareUnits > 0, "one caller ledger charges both phases");
    std::size_t exact = 0;
    RequireExact(run(total, exact), 1, "exact cumulative N succeeds");
    Require(exact == total, "deterministic exact work amount");
    std::size_t shortBudget = 0;
    const auto limited = run(total - 1, shortBudget);
    Require(
        !limited && limited.error().status == Status::ResourceLimit && limited.error().key == "RestrictionCallerBudget" &&
            limited.error().record == 42 && limited.error().stage == "PublicBudget",
        "N-1 propagates caller capacity without a partial certificate"
    );
    const auto prepared = Extend(source, Capacity(), Work);
    Require(bool(prepared), "reference for resource and cancellation boundaries");
    auto capacity = Capacity();
    capacity.controlSlots = 3;
    auto proof = CertifyCurveRestriction(prepared->reference, actual, { "1", "1000000000000" }, capacity, Work);
    Require(!proof && proof.error().status == Status::ResourceLimit, "resident reference and private copies count before allocation");
    capacity = Capacity();
    capacity.integerBits = 16;
    auto oversized = actual;
    oversized.homogeneous[0][0] = std::ldexp(1., 100);
    proof = CertifyCurveRestriction(prepared->reference, oversized, { "1", "1" }, capacity, Work);
    Require(!proof && proof.error().status == Status::ResourceLimit, "large exact binary coefficient cannot evade integer capacity");
    const Failure cancel{ Status::Cancelled, "RestrictionCallerCancel", "public cancellation", 71, "PublicCancel" };
    std::size_t calls = 0;
    proof = CertifyCurveRestriction(
        prepared->reference,
        actual,
        { "1", "1000000000000" },
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
        !proof && calls == 17 && proof.error().status == cancel.status && proof.error().key == cancel.key && proof.error().record == cancel.record &&
            proof.error().stage == cancel.stage,
        "cancellation stops at the failing operation and retains the original failure"
    );
    bool threw = false;
    try
    {
        (void)CertifyCurveRestriction(
            prepared->reference,
            actual,
            { "1", "1000000000000" },
            Capacity(),
            [](std::size_t) -> Result<void>
            {
                throw std::overflow_error("restriction caller");
            }
        );
    }
    catch (const std::overflow_error& error)
    {
        threw = std::string_view(error.what()) == "restriction caller";
    }
    Require(threw, "real caller exceptions are not relabelled as arithmetic capacity");
    RequireExact(
        CertifyCurveRestriction(prepared->reference, actual, { "1", "1000000000000" }, Capacity(), Work),
        1,
        "refusals, cancellation and exceptions leave the immutable reference reusable"
    );
}
} // namespace
int main()
{
    try
    {
        AnalyticQuadratics();
        MultipleSpans();
        DecimalSourceDomain();
        Refusals();
        Boundaries();
        std::cout << "Exact source curve-restriction contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
