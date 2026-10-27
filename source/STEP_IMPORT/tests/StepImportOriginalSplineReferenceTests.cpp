// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportTerminalSpline.h"

#include <algorithm>
#include <bit>
#include <boost/multiprecision/cpp_int.hpp>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace step_import;
using namespace step_import::detail;
using namespace step_import::detail::terminal_spline;
using Integer = boost::multiprecision::cpp_int;
using Rational = boost::multiprecision::cpp_rational;

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
Failure CallerFailure(Status status, const char* key)
{
    return { status, key, "public original-reference caller", 61, "OriginalReferenceCaller" };
}
struct Ledger
{
    std::size_t work = 0, calls = 0, maximum = std::numeric_limits<std::size_t>::max();
    std::size_t cancelAt = 0, throwAt = 0;
    std::vector<std::size_t> trace;
    Result<void> Spend(std::size_t n)
    {
        ++calls;
        if (calls == throwAt)
        {
            throw std::overflow_error("public original-reference exception");
        }
        if (calls == cancelAt)
        {
            return tl::unexpected(CallerFailure(Status::Cancelled, "OriginalReferenceCancel"));
        }
        if (n > maximum - work)
        {
            return tl::unexpected(CallerFailure(Status::ResourceLimit, "OriginalReferenceWork"));
        }
        work += n;
        trace.push_back(n);
        return {};
    }
    Charge Callback()
    {
        return [this](std::size_t n)
        {
            return Spend(n);
        };
    }
};
Rational Exact(const Integer& n, const Integer& d)
{
    Require(d != 0, "independent rational denominator");
    if (n == 0)
    {
        return Rational(0);
    }
    // Explicit normalization also avoids the pinned mixed rational/cpp_int
    // division overload's noncanonical zero. This is a test-only oracle,
    // independent of the production fixed-width arithmetic/Bernstein engine.
    Rational value;
    boost::multiprecision::backends::assign_components(value.backend(), n.backend(), d.backend());
    return value;
}
Rational Binary(double value)
{
    const auto bits = std::bit_cast<std::uint64_t>(value);
    const unsigned exponent = unsigned((bits >> 52) & 2047);
    Require(exponent != 2047, "oracle only accepts finite native scalars");
    Integer n = (bits & 0xfffffffffffffULL) | (exponent ? 0x10000000000000ULL : 0), d = 1;
    if (n == 0)
    {
        return Rational(0);
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
    return Exact(n, d);
}
IntegerRatio Allowance(const Rational& value)
{
    return { boost::multiprecision::numerator(value).convert_to<std::string>(), boost::multiprecision::denominator(value).convert_to<std::string>() };
}
Source TinyWeight()
{
    Source source;
    source.u = { 1, { ".1", ".3" }, { 2, 2 }, {} };
    source.countU = 2;
    source.controls = { { "0", "0", "0" }, { "1", "0", "0" } };
    // Exactly 2^-100. It is positive on the entire original interval, but
    // the old outward low envelope makes the linear W extrapolation negative.
    source.weights = { "7.888609052210118054117285652827862296732064351090230047702789306640625E-31", "1" };
    return source;
}
BinaryNet TinyActual()
{
    return { { 1, 0, 2, 1, { .1, .1, .3, .3 }, {} }, { { 0, 0, 0, std::ldexp(1., -100) }, { 1, 0, 0, 1 } } };
}
Source Hat()
{
    Source source;
    source.u = { 1, { ".1", ".2", ".3" }, { 2, 1, 2 }, {} };
    source.countU = 3;
    source.controls = { { "0", "0", "0" }, { "1", "0", "0" }, { "0", "0", "0" } };
    return source;
}
BinaryNet HatActual(double physicalScale = 1)
{
    return { { 1, 0, 3, 1, { .1, .1, .2, .3, .3 }, {} }, { { 0, 0, 0, 1 }, { physicalScale, 0, 0, 1 }, { 0, 0, 0, 1 } } };
}
std::string StoredFingerprint(const BinaryNet& net)
{
    std::ostringstream text;
    text << net.basis.degreeU << ';' << net.basis.degreeV << ';' << net.basis.countU << ';' << net.basis.countV << ';';
    for (const auto* knots : { &net.basis.knotsU, &net.basis.knotsV })
    {
        text << knots->size() << ';';
        for (double k : *knots)
        {
            text << std::bit_cast<std::uint64_t>(k) << ';';
        }
    }
    for (const auto& pole : net.homogeneous)
    {
        for (double x : pole)
        {
            text << std::bit_cast<std::uint64_t>(x) << ';';
        }
    }
    return text.str();
}
void RequireZero(const Result<Proof>& proof, const char* message)
{
    if (!proof)
    {
        throw std::runtime_error(std::string(message) + ": " + proof.error().key);
    }
    Require(proof->established && proof->maximumMm && *proof->maximumMm == 0 && proof->patches == 1, message);
}

void NoEnvelopeOrEmit()
{
    const auto source = TinyWeight();
    const auto controls = source.controls;
    const auto weights = source.weights;
    const auto knots = source.u.knots;
    const auto actual = TinyActual();
    const auto stored = StoredFingerprint(actual);
    Ledger old;
    auto extended = Extend(source, Capacity(), old.Callback());
    Require(
        !extended && extended.error().key == "TerminalSplineExtendedWeightUnestablished",
        "old Extend still refuses a negative weight in its outward computational envelope"
    );
    Ledger capture;
    auto reference = CaptureOriginalReference(source, Capacity(), capture.Callback());
    Require(bool(reference) && bool(*reference), "original positive-weight source needs no outward extension or emitted constructor target");
    const auto captureCalls = capture.calls;
    auto proof = CertifyCurveTrim(*reference, { ".1", ".3" }, actual, { "1", "1000000000000" }, Capacity(), capture.Callback());
    // Both rational Bezier nets are exactly identical in normalized parameter:
    // H(s)=(s,0,0,(1-s)*2^-100+s). No endpoint sampling is used as proof.
    RequireZero(proof, "same positive rational homogeneous definition gives strict zero under the declared endpoint affine correspondence");
    Require(
        capture.calls > captureCalls && source.controls == controls && source.weights == weights && source.u.knots == knots &&
            StoredFingerprint(actual) == stored,
        "capture and proof leave both source tokens and actual homogeneous storage unchanged"
    );
    Ledger sameT;
    auto ordinary = Certify(*reference, actual, { "1", "1000000000000" }, Capacity(), sameT.Callback());
    Require(
        !ordinary && ordinary.error().key == "TerminalSplineActualDomainOrDegree",
        "ordinary same-parameter Certify is not relaxed to accept decimal/binary domain mismatch"
    );
    Ledger repeated;
    RequireZero(
        CertifyCurveTrim(*reference, { ".1", ".3" }, actual, { "1", "1000000000000" }, Capacity(), repeated.Callback()),
        "failed different proof cannot mutate the immutable original reference"
    );
}

Rational HatError()
{
    const Rational a = Binary(.1), b = Binary(.3), n = Binary(.2);
    const Rational m = (a + b) / 2;
    Require(a < m && m < n && n < b, "independent decimal/binary hat-knot ordering");
    const Rational left = (n - m) / (n - a), right = (n - m) / (b - m);
    const Rational error = std::max(left, right);
    Require(error == Exact(Integer(1), Integer("7205759403792793")), "independent closed-form whole-hat maximum");
    return error;
}
void NonAffineInteriorAndUnits()
{
    const auto source = Hat();
    const auto actual = HatActual();
    const auto stored = StoredFingerprint(actual);
    Ledger ledger;
    auto reference = CaptureOriginalReference(source, Capacity(), ledger.Callback());
    Require(bool(reference), "capture unmodified exact nonbinary multiknot source");
    const Rational error = HatError();
    const auto enough = Allowance(error * 2), insufficient = Allowance(error / 2);
    auto pass = CertifyCurveTrim(*reference, { ".1", ".3" }, actual, enough, Capacity(), ledger.Callback());
    auto fail = CertifyCurveTrim(*reference, { ".1", ".3" }, actual, insufficient, Capacity(), ledger.Callback());
    // The source hat peaks at m=A*(1/5)+B; the native hat peaks at n.
    // Their difference is affine on [a,m],[m,n],[n,b]. Therefore its maximum
    // absolute value is exactly max((n-m)/(n-a),(n-m)/(b-m)), achieved at m/n.
    Require(
        pass && pass->established && pass->maximumMm && pass->patches == 3,
        "whole-source common refinement establishes an in-budget nonzero carrier error"
    );
    Require(
        Binary(*pass->maximumMm) >= error && Binary(*pass->maximumMm) <= error * 2,
        "continuous Bernstein upper encloses the independent analytic maximum"
    );
    Require(
        fail && !fail->established && fail->maximumMm && fail->patches == pass->patches &&
            std::bit_cast<std::uint64_t>(*fail->maximumMm) == std::bit_cast<std::uint64_t>(*pass->maximumMm),
        "same mathematical error must fail the lower budget without changing the bound or patch schedule"
    );
    Require(StoredFingerprint(actual) == stored, "common refinement does not edit actual native knots or controls");

    auto millimetres = source;
    millimetres.lengthToMm = { "1000", "1" };
    const auto scaledActual = HatActual(1000);
    Ledger scaled;
    auto scaledReference = CaptureOriginalReference(millimetres, Capacity(), scaled.Callback());
    Require(bool(scaledReference), "capture applies the exact source physical scale");
    const Rational scaledError = error * 1000;
    auto scaledProof = CertifyCurveTrim(*scaledReference, { ".1", ".3" }, scaledActual, Allowance(scaledError * 2), Capacity(), scaled.Callback());
    Require(
        scaledProof && scaledProof->established && scaledProof->maximumMm && Binary(*scaledProof->maximumMm) >= scaledError &&
            Binary(*scaledProof->maximumMm) <= scaledError * 2,
        "source lengthToMm is applied exactly once, before homogeneous comparison"
    );
    auto wrongUnits = CertifyCurveTrim(*scaledReference, { ".1", ".3" }, actual, { "1", "1" }, Capacity(), scaled.Callback());
    Require(wrongUnits && !wrongUnits->established, "a native boundary in the wrong physical units cannot borrow source chart evidence");
}

void ScopesAndActualBindings()
{
    for (unsigned scope = 0; scope < 3; ++scope)
    {
        auto source = Hat();
        if (scope == 0)
        {
            source.u.requested = std::array<std::string, 2>{ ".1", ".3" };
        }
        else if (scope == 1)
        {
            source.v = SourceAxis{ 1, { "0", "1" }, { 2, 2 }, {} };
        }
        else
        {
            source.countV = 2;
        }
        Ledger ledger;
        auto refused = CaptureOriginalReference(source, Capacity(), ledger.Callback());
        Require(
            !refused && refused.error().status == Status::Unsupported && refused.error().key == "TerminalSplineOriginalCurveReference",
            "original capture is curve-only and cannot inherit any requested interpretation"
        );
    }
    const auto source = Hat();
    Ledger ledger;
    auto reference = CaptureOriginalReference(source, Capacity(), ledger.Callback());
    Require(bool(reference), "public source for actual-domain/degree binding refusals");
    auto wrongDomain = HatActual();
    wrongDomain.basis.knotsU = { .15, .15, .2, .3, .3 };
    auto refused = CertifyCurveTrim(*reference, { ".1", ".3" }, wrongDomain, { "1", "1" }, Capacity(), ledger.Callback());
    Require(
        !refused && refused.error().key == "TerminalSplineTrimNativeDomainMismatch",
        "whole exact source cuts do not authorize an arbitrary actual target domain"
    );
    auto wrongDegree = HatActual();
    wrongDegree.basis.degreeU = 2;
    wrongDegree.basis.knotsU = { .1, .1, .1, .3, .3, .3 };
    refused = CertifyCurveTrim(*reference, { ".1", ".3" }, wrongDegree, { "1", "1" }, Capacity(), ledger.Callback());
    Require(
        !refused && refused.error().key == "TerminalSplineActualDomainOrDegree",
        "a separately valid different-degree actual curve is refused by the unchanged certificate"
    );
    refused = CertifyCurveTrim(*reference, { "0", ".3" }, HatActual(), { "1", "1" }, Capacity(), ledger.Callback());
    Require(!refused && refused.error().key == "TerminalSplineTrimOutsideSourceDomain", "original-reference capture grants no source extension");

    for (std::size_t slots : { std::size_t(3), std::size_t(2) })
    {
        auto limits = Capacity();
        limits.controlSlots = slots;
        Ledger storage;
        auto bounded = CaptureOriginalReference(source, limits, storage.Callback());
        Require(
            slots == 3 ? bool(bounded) : !bounded && bounded.error().status == Status::ResourceLimit,
            "capture charges precisely the original resident homogeneous control net without an Emit target"
        );
    }
    auto invalid = source;
    invalid.weights = { "1", "0", "1" };
    auto badWeight = CaptureOriginalReference(invalid, Capacity(), ledger.Callback());
    Require(!badWeight && badWeight.error().key == "TerminalSplineSourceWeight", "original capture does not relax positive-weight scope");
    invalid = source;
    invalid.controls[1][0] = "1E100";
    auto narrow = Capacity();
    narrow.integerBits = 32;
    auto badBits = CaptureOriginalReference(invalid, narrow, ledger.Callback());
    Require(!badBits && badBits.error().status == Status::ResourceLimit, "original net still obeys fixed exact-integer capacity");
}

Result<Proof> Run(Ledger& ledger)
{
    auto reference = CaptureOriginalReference(Hat(), Capacity(), ledger.Callback());
    if (!reference)
    {
        return tl::unexpected(std::move(reference.error()));
    }
    return CertifyCurveTrim(*reference, { ".1", ".3" }, HatActual(), Allowance(HatError() * 2), Capacity(), ledger.Callback());
}
void WorkCancellationAndExceptions()
{
    Ledger baseline;
    const auto result = Run(baseline);
    Require(
        result && result->established && result->maximumMm && baseline.work > 0 && baseline.calls > 10,
        "capture and proof complete under one shared caller ledger"
    );
    Ledger capture;
    Require(bool(CaptureOriginalReference(Hat(), Capacity(), capture.Callback())), "independent capture-only cancellation split");
    for (std::size_t maximum : { baseline.work, baseline.work - 1 })
    {
        Ledger ledger;
        ledger.maximum = maximum;
        auto bounded = Run(ledger);
        if (maximum == baseline.work)
        {
            Require(
                bounded && bounded->established && bounded->patches == result->patches && bounded->maximumMm &&
                    std::bit_cast<std::uint64_t>(*bounded->maximumMm) == std::bit_cast<std::uint64_t>(*result->maximumMm) && ledger.work == maximum,
                "N total exact-work units cover capture and certificate without resetting the ledger"
            );
        }
        else
        {
            Require(
                !bounded && bounded.error().key == "OriginalReferenceWork" && bounded.error().record == 61 &&
                    bounded.error().stage == "OriginalReferenceCaller",
                "N-1 total work returns the original caller failure without a certificate"
            );
        }
    }
    for (std::size_t at : { std::size_t(1), capture.calls, capture.calls + 1, baseline.calls / 2, baseline.calls })
    {
        Ledger ledger;
        ledger.cancelAt = at;
        auto cancelled = Run(ledger);
        Require(
            !cancelled && cancelled.error().status == Status::Cancelled && cancelled.error().key == "OriginalReferenceCancel" &&
                cancelled.error().record == 61 && cancelled.error().stage == "OriginalReferenceCaller",
            "cancellation preserves identity during capture, certificate entry/refinement and final exact arithmetic"
        );
    }
    for (std::size_t at : { std::size_t(1), capture.calls + 1 })
    {
        Ledger ledger;
        ledger.throwAt = at;
        bool propagated = false;
        try
        {
            (void)Run(ledger);
        }
        catch (const std::overflow_error& error)
        {
            propagated = std::string_view(error.what()) == "public original-reference exception";
        }
        Require(propagated, "caller exception during either stage is not reclassified as checked-integer overflow");
    }
    Ledger again;
    auto repeated = Run(again);
    Require(
        repeated && repeated->established && again.trace == baseline.trace,
        "failed/cancelled attempts leave no hidden arithmetic state or changed charge schedule"
    );
}

void ExistingExtendSequence()
{
    Source source;
    source.u = { 1, { "0", "1" }, { 2, 2 }, {} };
    source.countU = 2;
    source.controls = { { "0", "0", "0" }, { "1", "0", "0" } };
    Ledger capture, extend;
    auto original = CaptureOriginalReference(source, Capacity(), capture.Callback());
    auto emitted = Extend(source, Capacity(), extend.Callback());
    Require(
        original && emitted && extend.trace.size() > capture.trace.size() &&
            std::equal(capture.trace.begin(), capture.trace.end(), extend.trace.begin()),
        "old Extend retains the entire original-read charge prefix before its existing Requested/ExtendRange/Emit stages"
    );
    Require(
        emitted->target.basis.knotsU == std::vector<double>{ 0, 0, 1, 1 } &&
            emitted->target.controlsMm == std::vector<std::array<double, 3>>{ { 0, 0, 0 }, { 1, 0, 0 } },
        "ordinary existing constructor output remains unchanged"
    );
}
void NaturalUnclampedCurve()
{
    Source source;
    source.u = { 2, { "-1", "0", "1", "2", "3", "4" }, { 1, 1, 1, 1, 1, 1 }, {} };
    source.countU = 3;
    source.controls = { { "0", "0", "0" }, { "1", "0", "0" }, { "2", "0", "0" } };
    BinaryNet actual;
    actual.basis = { 2, 0, 3, 1, { 1, 1, 1, 2, 2, 2 }, {} };
    actual.homogeneous = { { .5, 0, 0, 1 }, { 1, 0, 0, 1 }, { 1.5, 0, 0, 1 } };
    Ledger strict;
    const auto old = CaptureOriginalReference(source, Capacity(), strict.Callback());
    Require(!old && old.error().key == "TerminalSplineClampedContinuousBasis", "old source-reference contract remains strict");
    Ledger natural;
    const auto reference = CaptureNaturalCurveReference(source, Capacity(), natural.Callback());
    Require(bool(reference), "capture exact natural restriction of a nonclamped source curve");
    const auto proof = CertifyCurveTrim(*reference, { "1", "2" }, actual, { "1", "1000000000" }, Capacity(), natural.Callback());
    Require(
        proof && proof->established && proof->maximumMm && *proof->maximumMm == 0,
        "independent linear oracle: quadratic cardinal spline equals a clamped line from .5 to1.5 on original U[1,2]"
    );
    source.weights = { "1", "2", "1" };
    actual.homogeneous = { { 1, 0, 0, 1.5 }, { 2, 0, 0, 2 }, { 2, 0, 0, 1.5 } };
    Ledger rational;
    const auto weighted = CaptureNaturalCurveReference(source, Capacity(), rational.Callback());
    Require(bool(weighted), "positive rational natural restriction");
    const auto rationalProof = CertifyCurveTrim(*weighted, { "1", "2" }, actual, { "1", "1000000000" }, Capacity(), rational.Callback());
    Require(
        rationalProof && rationalProof->established && rationalProof->maximumMm && *rationalProof->maximumMm == 0,
        "homogeneous oracle: end weights3/2, center weight2 and exact numerators1,2,2"
    );
}
void NaturalOneSidedEndpoints()
{
    for (bool lowClamped : { false, true })
    {
        Source source;
        source.countU = 3;
        source.u = lowClamped ? SourceAxis{ 2, { "0", "1", "2", "3" }, { 3, 1, 1, 1 }, {} } :
                                SourceAxis{ 2, { "-1", "0", "1", "2" }, { 1, 1, 1, 3 }, {} };
        source.controls = { { "0", "0", "0" }, { "1", "0", "0" }, { "2", "0", "0" } };
        const double low = lowClamped ? 0. : 1., high = low + 1;
        BinaryNet actual;
        actual.basis = { 2, 0, 3, 1, { low, low, low, high, high, high }, {} };
        actual.homogeneous = { { lowClamped ? 0. : .5, 0, 0, 1 }, { 1, 0, 0, 1 }, { lowClamped ? 1.5 : 2., 0, 0, 1 } };
        // Direct quadratic polynomial oracle: C(U)=2U-.5U² if low-clamped;
        // otherwise C(U)=.5+(U-1)+.5(U-1)² on the original interval [1,2].
        Ledger ledger;
        const auto reference = CaptureNaturalCurveReference(source, Capacity(), ledger.Callback());
        Require(bool(reference), "one-sided natural endpoint capture");
        const std::array<std::string, 2> cuts = lowClamped ? std::array<std::string, 2>{ "0", "1" } : std::array<std::string, 2>{ "1", "2" };
        const auto proof = CertifyCurveTrim(*reference, cuts, actual, { "1", "1000000000" }, Capacity(), ledger.Callback());
        Require(proof && proof->established && proof->maximumMm && *proof->maximumMm == 0, "one-sided exact natural restriction");
        const auto outside = CertifyCurveTrim(*reference, { "-1", "3" }, actual, { "1", "1" }, Capacity(), ledger.Callback());
        Require(
            !outside && outside.error().key == "TerminalSplineTrimOutsideSourceDomain",
            "outer transmitted knots grant no source-domain extension"
        );
        auto wrong = actual;
        for (auto& k : wrong.basis.knotsU)
        {
            k += .25;
        }
        const auto shifted = CertifyCurveTrim(*reference, cuts, wrong, { "1", "1" }, Capacity(), ledger.Callback());
        Require(
            !shifted && shifted.error().key == "TerminalSplineTrimNativeDomainMismatch",
            "native knot shift cannot silently reparameterize the natural reference"
        );
    }
}
void NaturalDegreeAndRepeatedKnots()
{
    for (unsigned p = 1; p <= 8; ++p)
    {
        Source source;
        source.u.degree = p;
        source.countU = p + 4;
        for (unsigned i = 0; i < source.countU + p + 1; ++i)
        {
            source.u.knots.push_back(std::to_string(int(i) - int(p)));
            source.u.multiplicities.push_back(1);
        }
        for (unsigned i = 0; i < source.countU; ++i)
        {
            source.controls.push_back({ std::to_string(i), "0", "0" });
        }
        BinaryNet actual;
        actual.basis = { p, 0, p + 1, 1, {}, {} };
        actual.basis.knotsU.insert(actual.basis.knotsU.end(), p + 1, 0);
        actual.basis.knotsU.insert(actual.basis.knotsU.end(), p + 1, 4);
        // Uniform cardinal B-spline linear precision: C(U)=U+(p-1)/2.
        // Degree-elevating this line to p gives the following Bezier controls.
        // Non-dyadic 4*i/p poles are rounded only in the actual native fixture;
        // the unchanged whole-domain certificate must include that error too.
        for (unsigned i = 0; i <= p; ++i)
        {
            actual.homogeneous.push_back({ .5 * (p - 1) + 4. * i / p, 0, 0, 1 });
        }
        Ledger ledger;
        const auto reference = CaptureNaturalCurveReference(source, Capacity(), ledger.Callback());
        Require(bool(reference), "natural curve reference across public degrees");
        const auto proof = CertifyCurveTrim(*reference, { "0", "4" }, actual, { "1", "1000000000" }, Capacity(), ledger.Callback());
        Require(
            proof && proof->established && proof->maximumMm && *proof->maximumMm < 1e-9,
            "cardinal linear precision proves original parameter correspondence across degrees"
        );
    }
    Source source;
    source.u = { 3, { "-2", "-1", "0", "1", "2", "3", "4" }, { 1, 2, 2, 1, 2, 2, 1 }, {} };
    source.countU = 7;
    for (int i = 0; i < 7; ++i)
    {
        source.controls.push_back({ std::to_string(i), std::to_string(i * i - 2), std::to_string(i % 2 ? -1 : 1) });
        source.weights.push_back(i % 2 ? "2" : "1");
    }
    BinaryNet actual;
    actual.basis = { 3, 0, 7, 1, { 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2 }, {} };
    // Independent homogeneous piecewise polynomial oracle (t=U, s=U-1):
    // [0,1]: X=2+6t²-3.5t³, Y=6t+12t²-7t³,
    // Z=-.5+4.5t-9t²+4.5t³, W=1.5-1.5t+3t²-1.5t³.
    // [1,2]: X=4.5+1.5s-4.5s²+5.5s³, Y=11+9s-9s²+19s³,
    // Z=-.5+4.5s²-4.5s³, W=1.5-1.5s²+1.5s³.
    // These Bernstein coefficients are all exactly representable binary64.
    actual.homogeneous = { { 2, 0, -.5, 1.5 },  { 2, 2, 1, 1 },  { 4, 8, -.5, 1.5 }, { 4.5, 11, -.5, 1.5 },
                           { 5, 14, -.5, 1.5 }, { 4, 14, 1, 1 }, { 7, 30, -.5, 1.5 } };
    Ledger ledger;
    const auto reference = CaptureNaturalCurveReference(source, Capacity(), ledger.Callback());
    Require(bool(reference), "nonuniform repeated natural endpoints with positive rational weights");
    const auto proof = CertifyCurveTrim(*reference, { "0", "2" }, actual, { "1", "1000000000" }, Capacity(), ledger.Callback());
    Require(
        proof && proof->established && proof->maximumMm && *proof->maximumMm == 0 && proof->patches == 2,
        "original C2 interior knot and repeated natural endpoints match independent rational polynomial oracle"
    );
}
void NaturalMultiSpanAndExecution()
{
    Source source;
    source.u = { 2, { "-2", "-1", "0", "1", "3", "4", "6" }, { 1, 1, 1, 1, 1, 1, 1 }, {} };
    source.countU = 4;
    source.controls = { { "0", "0", "0" }, { "6", "6", "0" }, { "24", "0", "6" }, { "54", "6", "0" } };
    BinaryNet actual;
    actual.basis = { 2, 0, 5, 1, { 0, 0, 0, 1, 1, 3, 3, 3 }, {} };
    // Independent polynomial oracle, t=U on[0,1], s=(U-1)/2 on[1,3]:
    // first=(3+6t+3t²,3+6t-5t²,2t²),
    // second=(12+24s+8s²,4-8s+8s²,2+8s-8s²).
    actual.homogeneous = { { 3, 3, 0, 1 }, { 6, 6, 0, 1 }, { 12, 4, 2, 1 }, { 24, 0, 6, 1 }, { 44, 4, 2, 1 } };
    auto run = [&](Ledger& ledger) -> Result<Proof>
    {
        const auto reference = CaptureNaturalCurveReference(source, Capacity(), ledger.Callback());
        if (!reference)
        {
            return tl::unexpected(reference.error());
        }
        return CertifyCurveTrim(*reference, { "0", "3" }, actual, { "1", "1000000000" }, Capacity(), ledger.Callback());
    };
    Ledger baseline;
    const auto proof = run(baseline);
    Require(
        proof && proof->established && proof->maximumMm && *proof->maximumMm == 0 && proof->patches == 2,
        "nonuniform multispan natural restriction matches independent homogeneous polynomial oracle"
    );
    Ledger capture;
    Require(bool(CaptureNaturalCurveReference(source, Capacity(), capture.Callback())), "isolated natural capture ledger");
    for (auto maximum : { baseline.work, baseline.work - 1 })
    {
        Ledger bounded;
        bounded.maximum = maximum;
        const auto result = run(bounded);
        Require(
            maximum == baseline.work ? bool(result && result->established) : !result && result.error().key == "OriginalReferenceWork",
            "one cumulative ledger covers natural capture and certificate at N/N-1"
        );
    }
    for (auto at : { std::size_t(1), capture.calls / 2, capture.calls, capture.calls + 1, baseline.calls })
    {
        Ledger stopped;
        stopped.cancelAt = at;
        const auto result = run(stopped);
        Require(
            !result && result.error().status == Status::Cancelled && result.error().key == "OriginalReferenceCancel" && result.error().record == 61,
            "caller cancellation preserves identity before/during endpoint insertion, refinement and final bound"
        );
    }
    for (auto at : { std::size_t(1), capture.calls / 2, capture.calls + 1 })
    {
        Ledger throwing;
        throwing.throwAt = at;
        bool propagated = false;
        try
        {
            (void)run(throwing);
        }
        catch (const std::overflow_error& e)
        {
            propagated = std::string_view(e.what()) == "public original-reference exception";
        }
        Require(propagated, "natural capture must not reclassify caller exceptions as numeric overflow");
    }
    Ledger repeated;
    const auto same = run(repeated);
    Require(same && same->established && baseline.trace == repeated.trace, "failed natural captures leave no mutable arithmetic state");
    auto capacity = Capacity();
    capacity.controlSlots = source.countU;
    Ledger limited;
    const auto scarce = CaptureNaturalCurveReference(source, capacity, limited.Callback());
    Require(!scarce && scarce.error().status == Status::ResourceLimit, "natural reference charges resident and new control allocations");
    auto invalid = source;
    invalid.u.requested = std::array<std::string, 2>{ "-3", "7" };
    Ledger requests;
    Require(!CaptureNaturalCurveReference(invalid, Capacity(), requests.Callback()), "natural capture does not extend a requested domain");
    invalid = source;
    invalid.v = source.u;
    Ledger surfaces;
    Require(!CaptureNaturalCurveReference(invalid, Capacity(), surfaces.Callback()), "natural capture is not an implicit surface capability");
    invalid = source;
    invalid.weights = { "1", "1", "0", "1" };
    Ledger weights;
    Require(!CaptureNaturalCurveReference(invalid, Capacity(), weights.Callback()), "positive weights remain mandatory");
    auto changed = actual;
    changed.homogeneous[2][1] += .01;
    Ledger tamper;
    const auto reference = CaptureNaturalCurveReference(source, Capacity(), tamper.Callback());
    Require(bool(reference), "tamper reference");
    const auto wrong = CertifyCurveTrim(*reference, { "0", "3" }, changed, { "1", "1000000000" }, Capacity(), tamper.Callback());
    Require(wrong && !wrong->established, "changed actual native middle pole cannot pass exact natural-domain certificate");
}
} // namespace
int main()
{
    try
    {
        NoEnvelopeOrEmit();
        NonAffineInteriorAndUnits();
        ScopesAndActualBindings();
        WorkCancellationAndExceptions();
        ExistingExtendSequence();
        NaturalUnclampedCurve();
        NaturalMultiSpanAndExecution();
        NaturalDegreeAndRepeatedKnots();
        NaturalOneSidedEndpoints();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
