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
void DecimalTrimCorrespondence()
{
    Source line;
    line.u = { 1, { "0", "1" }, { 2, 2 }, {} };
    line.countU = 2;
    line.controls = { { "0", "0", "0" }, { "1", "0", "0" } };
    const auto prepared = Extend(line, Capacity(), Work);
    Require(bool(prepared), "exact unit line for decimal endpoint conversion");
    const std::array<std::string, 2> cuts{ "0.1", "0.9" };
    BinaryNet actual{ { 1, 0, 2, 1, { .1, .1, .9, .9 }, {} }, { { .1, 0, 0, 1 }, { .9, 0, 0, 1 } } };
    RequireExact(
        CertifyCurveRestriction(prepared->reference, actual, { "1", "1000000000000" }, Capacity(), Work),
        1,
        "same-t identity alone cannot detect rounding of selected decimal endpoints"
    );
    auto proof = CertifyCurveTrim(prepared->reference, cuts, actual, { "1", "1000000000000" }, Capacity(), Work);
    Require(
        proof && proof->established && proof->maximumMm && *proof->maximumMm > 0 && *proof->maximumMm < 1e-15,
        "declared full-domain endpoint correspondence includes nonzero binary conversion error"
    );
    const auto before = actual;
    actual.basis.knotsU = { .2, .2, .9, .9 };
    auto refused = CertifyCurveTrim(prepared->reference, cuts, actual, { "1", "1000000000000" }, Capacity(), Work);
    Require(!refused && refused.error().key == "TerminalSplineTrimNativeDomainMismatch", "an arbitrary target interval cannot hide in the affine map");
    actual = before;
    refused = CertifyCurveTrim(prepared->reference, { "-0.1", "0.9" }, actual, { "1", "1000000000000" }, Capacity(), Work);
    Require(!refused && refused.error().key == "TerminalSplineTrimOutsideSourceDomain", "source decimal cuts cannot extend the original domain");
    refused = CertifyCurveTrim(prepared->reference, { "0.1", "0.1000000000000000001" }, actual, { "1", "1000000000000" }, Capacity(), Work);
    Require(!refused && refused.error().key == "TerminalSplineTrimBinaryIntervalCollapsed", "distinct decimals cannot collapse the target range");
    refused = CertifyCurveTrim(prepared->reference, { "0.9", "0.1" }, actual, { "1", "1000000000000" }, Capacity(), Work);
    Require(
        !refused && refused.error().key == "TerminalSplineTrimSourceRange",
        "proof consumes an increasing basis range, never guesses source sense"
    );

    // C(t)=2^40*t: the same parameter rounding is now micrometres of physical
    // displacement. Every homogeneous value is still an exact binary number;
    // this counterexample cannot be dismissed as a rounded native control net.
    line.controls[1][0] = "1099511627776";
    const auto amplified = Extend(line, Capacity(), Work);
    Require(bool(amplified), "large derivative source line");
    actual.homogeneous = { { std::ldexp(.1, 40), 0, 0, 1 }, { std::ldexp(.9, 40), 0, 0, 1 } };
    RequireExact(
        CertifyCurveRestriction(amplified->reference, actual, { "1", "1000000000" }, Capacity(), Work),
        1,
        "large-derivative same-t identity remains mathematically exact"
    );
    proof = CertifyCurveTrim(amplified->reference, cuts, actual, { "1", "1000000000" }, Capacity(), Work);
    Require(
        proof && !proof->established && proof->maximumMm && *proof->maximumMm > 1e-6,
        "large derivatives cannot amplify cut conversion outside the single original defining budget"
    );

    const auto quadratic = Extend(Quadratic(), Capacity(), Work);
    Require(bool(quadratic), "source point selector reference");
    auto selector = SourceCurvePointDifferenceUpper(quadratic->reference, "2.75", { "0.5", "0.75", "0" }, { "1", "1" }, Capacity(), Work);
    Require(selector && *selector == 0, "exact source parameter and exact Cartesian selector coincide");
    selector = SourceCurvePointDifferenceUpper(quadratic->reference, "2.75", { "0.51", "0.75", "0" }, { "1", "1" }, Capacity(), Work);
    Require(selector && *selector >= .01 && *selector < .01000000000000001, "exact declared selector discrepancy is reported as an outward bound");
    selector = SourceCurvePointDifferenceUpper(quadratic->reference, "2.75", { "0.25", "0.375", "0" }, { "2", "1" }, Capacity(), Work);
    Require(selector && *selector == 0, "selector coordinates use their exact physical length conversion once");
    selector = SourceCurvePointDifferenceUpper(quadratic->reference, "1", { "0", "0", "0" }, { "1", "1" }, Capacity(), Work);
    Require(
        !selector && selector.error().key == "TerminalSplinePointOutsideSourceDomain",
        "point checks cannot evaluate an undeclared source extension"
    );
    const auto rational = Extend(Quadratic(true), Capacity(), Work);
    Require(bool(rational), "positive nonuniform rational point selector reference");
    selector = SourceCurvePointDifferenceUpper(
        rational->reference,
        "2.75",
        { "0.63636363636363636", "1.0909090909090909", "0" },
        { "1", "1" },
        Capacity(),
        Work
    );
    Require(selector && *selector > 0 && *selector < 1e-15, "rational de Boor compares the exact source point before decimal or native rounding");
    std::size_t total = 0;
    const auto run = [&](std::size_t maximum, std::size_t& spent) -> Result<Proof>
    {
        const Charge charge = [&](std::size_t units) -> Result<void>
        {
            if (units > maximum - spent)
            {
                return tl::unexpected(Failure{ Status::ResourceLimit, "TrimCallerBudget", "public cumulative limit", 43, "PublicTrimBudget" });
            }
            spent += units;
            return {};
        };
        const auto source = Extend(line, Capacity(), charge);
        if (!source)
        {
            return tl::unexpected(source.error());
        }
        return CertifyCurveTrim(source->reference, cuts, actual, { "1", "1" }, Capacity(), charge);
    };
    const auto observed = run(std::numeric_limits<std::size_t>::max(), total);
    Require(observed && observed->established && total > 0, "prepare and endpoint-correspondence proof share a caller ledger");
    std::size_t exact = 0;
    Require(bool(run(total, exact)) && exact == total, "exact endpoint-correspondence work budget succeeds");
    std::size_t limitedUnits = 0;
    const auto limited = run(total - 1, limitedUnits);
    Require(!limited && limited.error().key == "TrimCallerBudget", "N-1 endpoint-correspondence work budget cannot publish a certificate");
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
void BoundedCurveRefinement()
{
    // Independent analytic truth: degree-one controls at their own knots
    // define C(t)=(t,0,0), irrespective of the two different partitions. Their
    // union has 32 positive spans. Refinement must not need a fresh full net
    // for every inserted knot; the SAME 128-slot allowance covers both nets.
    Source source;
    source.u.degree = 1;
    source.countU = 17;
    for (unsigned i = 0; i <= 16; ++i)
    {
        const auto text = std::to_string(double(i) / 16);
        source.u.knots.push_back(text);
        source.u.multiplicities.push_back(i == 0 || i == 16 ? 2 : 1);
        source.controls.push_back({ text, "0", "0" });
    }
    BinaryNet actual;
    actual.basis.degreeU = 1;
    actual.basis.countU = 18;
    actual.basis.knotsU.push_back(0);
    actual.homogeneous.push_back({ 0, 0, 0, 1 });
    for (unsigned i = 0; i < 16; ++i)
    {
        const double knot = double(2 * i + 1) / 32;
        actual.basis.knotsU.push_back(knot);
        actual.homogeneous.push_back({ knot, 0, 0, 1 });
    }
    actual.basis.knotsU.insert(actual.basis.knotsU.begin(), 0);
    actual.basis.knotsU.push_back(1);
    actual.basis.knotsU.push_back(1);
    actual.homogeneous.push_back({ 1, 0, 0, 1 });
    const auto originalControls = source.controls;
    const auto originalKnots = source.u.knots;
    const auto actualBefore = actual;
    auto capacity = Capacity();
    capacity.controlSlots = 128;
    auto reference = CaptureOriginalReference(source, capacity, Work);
    Require(bool(reference), "bounded straight-line reference capture");
    const auto certify = [&](const terminal_spline::Limits& limits, const Charge& charge)
    {
        return CertifyCurveTrim(*reference, { "0", "1" }, actual, { "1", "1000000000000" }, limits, charge);
    };
    RequireExact(certify(capacity, Work), 32, "different partitions certify the same line within bounded control storage");
    actual.homogeneous[8][1] = .25;
    const auto changed = certify(capacity, Work);
    Require(
        changed && !changed->established && changed->maximumMm && *changed->maximumMm > 0,
        "refinement still detects an interior defining-control error"
    );
    actual = actualBefore;

    // Resident reference 17 + private source copy 17 + actual 18 + the two
    // newly allocated 33-control refinement buffers = 118 cumulative slots.
    // Both old buffers are still counted; this is not a peak-only ledger.
    capacity.controlSlots = 118;
    RequireExact(certify(capacity, Work), 32, "exact cumulative control allocation N succeeds");
    capacity.controlSlots = 117;
    const auto shortStorage = certify(capacity, Work);
    Require(
        !shortStorage && shortStorage.error().status == Status::ResourceLimit && shortStorage.error().key == "TerminalSplineControlCapacity",
        "N-1 control storage refuses without a partial proof"
    );
    capacity.controlSlots = 128;
    std::size_t units = 0, calls = 0;
    RequireExact(
        certify(
            capacity,
            [&](std::size_t count) -> Result<void>
            {
                units += count;
                ++calls;
                return {};
            }
        ),
        32,
        "refinement work remains charged to the caller"
    );
    for (const auto limit : { units, units - 1 })
    {
        std::size_t spent = 0;
        const auto result = certify(
            capacity,
            [&](std::size_t count) -> Result<void>
            {
                if (count > limit - spent)
                {
                    return tl::unexpected(Failure{ Status::ResourceLimit, "RefineWork", "public work", 19, "RefineCaller" });
                }
                spent += count;
                return {};
            }
        );
        if (limit == units)
        {
            RequireExact(result, 32, "exact refinement work N succeeds");
        }
        else
        {
            Require(!result && result.error().key == "RefineWork" && result.error().record == 19, "N-1 refinement work propagates");
        }
    }
    for (const auto stop : { std::size_t(1), calls / 2, calls })
    {
        std::size_t visited = 0;
        const auto stopped = certify(
            capacity,
            [&](std::size_t) -> Result<void>
            {
                if (++visited == stop)
                {
                    return tl::unexpected(Failure{ Status::Cancelled, "RefineCancel", "public cancellation", 20, "RefineCaller" });
                }
                return {};
            }
        );
        Require(!stopped && stopped.error().key == "RefineCancel" && visited == stop, "refinement cooperates with cancellation");
    }
    RequireExact(certify(capacity, Work), 32, "reference remains reusable after cancellation and capacity refusal");
    Require(
        source.controls == originalControls && source.u.knots == originalKnots && actual.homogeneous == actualBefore.homogeneous &&
            actual.basis.knotsU == actualBefore.basis.knotsU,
        "refinement does not mutate source or stored-native inputs"
    );

    // A quadratic homogeneous insertion mixes TWO adjacent old poles. These
    // independently calculated midpoint controls detect forward overwrite or
    // moving a tail pole which the next blend still needs.
    auto rational = Quadratic(true);
    rational.u.knots = { "0", "1" };
    const auto rationalReference = CaptureOriginalReference(rational, Capacity(), Work);
    Require(bool(rationalReference), "rational alias control reference");
    BinaryNet midpoint;
    midpoint.basis = { 2, 0, 4, 1, { 0, 0, 0, .5, 1, 1, 1 }, {} };
    midpoint.homogeneous = { { 0, 0, 0, 1 }, { 1, 2, 0, 1.5 }, { 2, 2, 0, 1.5 }, { 2, 0, 0, 1 } };
    RequireExact(
        CertifyCurveTrim(*rationalReference, { "0", "1" }, midpoint, { "1", "1000000000000" }, Capacity(), Work),
        2,
        "in-place quadratic refinement preserves both old blend dependencies"
    );

    Source oneSpan;
    oneSpan.u = { 1, { "0", "1" }, { 2, 2 }, {} };
    oneSpan.countU = 2;
    oneSpan.controls = { { "0", "0", "0" }, { "1", "0", "0" } };
    const auto shortReference = CaptureOriginalReference(oneSpan, Capacity(), Work);
    Require(bool(shortReference), "single-span reference");
    BinaryNet oneSpanActual;
    oneSpanActual.basis = { 1, 0, 2, 1, { 0, 0, 1, 1 }, {} };
    oneSpanActual.homogeneous = { { 0, 0, 0, 1 }, { 1, 0, 0, 1 } };
    capacity.controlSlots = 6;
    RequireExact(
        CertifyCurveTrim(*shortReference, { "0", "1" }, oneSpanActual, { "1", "1000000000000" }, capacity, Work),
        1,
        "no missing knots require no extra control buffer beyond resident and two copies"
    );
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
        DecimalTrimCorrespondence();
        Refusals();
        BoundedCurveRefinement();
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
