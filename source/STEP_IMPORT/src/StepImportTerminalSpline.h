// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace step_import::detail::terminal_spline
{
struct IntegerRatio
{
    std::string numerator = "1", denominator = "1";
};
struct SourceAxis
{
    unsigned degree = 0;
    std::vector<std::string> knots; // Unique increasing original decimal tokens.
    std::vector<std::size_t> multiplicities;
    // Original parameter numbers, not lengths. Absence retains the old interval.
    // These endpoints must contain the entire original interval; no trimming.
    std::optional<std::array<std::string, 2>> requested;
};
struct Source
{
    SourceAxis u;
    std::optional<SourceAxis> v; // Absent for a curve; its parameter is u here.
    std::size_t countU = 0, countV = 1;
    std::vector<std::array<std::string, 3>> controls; // Euclidean, U-major/V-minor.
    std::vector<std::string> weights; // Empty means exactly one at every pole.
    IntegerRatio lengthToMm; // Exact positive physical scale, applied once to XYZ.
};
struct Limits
{
    // Conservative cumulative homogeneous control slots allocated by one call;
    // verification includes the resident reference. This also bounds peak
    // storage. No implicit production capacity is supplied by this module.
    std::size_t controlSlots = 0;
    std::size_t integerBits = 4096; // Reduced ratios; hard maximum 4096 bits.
    std::size_t tokenCharacters = 1234;
};
// Charge before exact arithmetic, conversion and allocated control slots.
// Units are implementation work, not native calls or millimetre allowance.
// The owner accumulates them across ALL calls; its Failure propagates unchanged.
using Charge = std::function<Result<void>(std::size_t)>;

struct Basis
{
    unsigned degreeU = 0, degreeV = 0;
    std::size_t countU = 0, countV = 1;
    std::vector<double> knotsU, knotsV; // Expanded; knotsV empty for a curve.
};
struct Canonical
{
    Basis basis;
    std::vector<std::array<double, 3>> controlsMm; // Euclidean constructor input.
    std::vector<double> weights;
};
struct BinaryNet
{
    Basis basis;
    // Exact stored binary64 homogeneous XYZW, read AFTER native construction.
    // For a nonrational native net use stored XYZ and literal W=1. Do not divide
    // then multiply a native homogeneous pole through rounded Euclidean values.
    std::vector<std::array<double, 4>> homogeneous;
};
class Reference;
struct Prepared
{
    Canonical target;
    std::shared_ptr<const Reference> reference;
};
struct Proof
{
    bool established = false;
    std::optional<double> maximumMm; // Outward upper bound, not an error sample.
    std::size_t patches = 0;
};
struct PoleVisits
{
    std::array<double, 2> interval{}; // Selected actual native t, increasing and contained in the natural domain.
    std::array<double, 3> poleMm{};
    double radiusMm = 0; // Positive native working-zone radius; never a source uncertainty or a new tolerance.
    bool firstAllowed = false, lastAllowed = false;
};
struct PoleVisitProof
{
    bool established = false;
    std::size_t patches = 0; // Exact Bezier regions inspected, including bounded subdivision nodes.
};

// Explicit interpreted geometry only. Parse original decimals/scale exactly;
// extract clamped continuous terminal spans by homogeneous Boehm insertion and
// extend their rational polynomials at the SAME parameter. Tensor directions
// use the same operation, including their corner patches. Positive weights are
// required on the full resulting net. Old-domain equality is an exact algebraic
// consequence; it is not established by finite samples or a native old-domain
// restriction. Requested endpoints are enclosed outward in binary64 (exact zero
// stays zero); the reference also covers this computational envelope.
//
// Degrees 1..17, clamped endpoints and interior multiplicity <= degree are
// admitted. Unestablished geometry/range, arithmetic/storage capacity and caller
// failure remain distinct. Checked fixed-width exact integer intermediates are
// bounded; Boost types do not cross this interface. STL allocation exceptions
// retain their actual meaning. No source, kernel object or global state changes.
[[nodiscard]] Result<Prepared> Extend(const Source& source, const Limits& limits, const Charge& charge);

// Compare the actual native binary64 definition against the exact interpreted
// SOURCE reference on the WHOLE actual natural domain. Degrees/domain must match
// the prepared computational envelope; distinct source/native interior knots
// are compared through exact common refinement, not assumed to share a basis.
// Bernstein coefficients of N_ref*W_actual-N_actual*W_ref and strictly positive
// weight minima bound Euclidean distance via an L1 upper bound on every patch.
// maximumMm is the ONE remaining exact mm allowance assigned by the request
// owner, not an additional allowance to spend after source->native conversion.
// This certifies defining stored geometry, not libm/evaluator error, SOR frame
// roundtrip, UV conversion, injectivity, topology, source validity or publication.
[[nodiscard]] Result<Proof> Certify(
    const std::shared_ptr<const Reference>& reference,
    const BinaryNet& actual,
    const IntegerRatio& maximumMm,
    const Limits& limits,
    const Charge& charge
);
// Compare a CURVE's actual stored definition on its whole natural subdomain
// against an original source reference made by Extend with no requested range.
// The actual binary64 endpoints must lie inside the ORIGINAL exact source
// domain, not merely its outward computational envelope. Exact homogeneous
// Boehm restriction and the same common-refinement/Bernstein engine retain the
// original scalar parameter; there is no extension, reparameterization or
// second mm allowance. The reference and actual input remain unchanged.
// The caller separately binds the actual interval and orientation to its
// requested source trim/use. A different valid subcurve can also have zero
// error here; this evidence alone cannot establish the selection contract.
//
// Scope is the existing degree 1..17, clamped C0-or-better positive-weight
// curve representation. This checks that basis, not geometric nonclosure or
// an unavailable native periodic flag. Prepare and proof must charge the same
// caller ledger; allocation, exact-capacity and cancellation contracts match
// Certify. Neither endpoint samples nor the chosen work resolution certify it.
[[nodiscard]] Result<Proof> CertifyCurveRestriction(
    const std::shared_ptr<const Reference>& reference,
    const BinaryNet& actual,
    const IntegerRatio& maximumMm,
    const Limits& limits,
    const Charge& charge
);
// Exact source trim parameters, increasing in the basis parameter (source
// sense is recorded by the caller), compared to the ACTUAL stored curve.
// Source cuts must lie inside the unrequested original source domain. Its
// private exact restriction is affinely mapped to the nearest-binary64 cut
// interval solely for error comparison; neither source nor native parameters
// are edited. The actual natural interval must equal those converted cuts.
// One whole-domain mm bound includes cut rounding AND defining-net error;
// a same-t certificate or endpoint samples cannot replace this proof.
[[nodiscard]] Result<Proof> CertifyCurveTrim(
    const std::shared_ptr<const Reference>& reference,
    const std::array<std::string, 2>& exactSourceCuts,
    const BinaryNet& actual,
    const IntegerRatio& maximumMm,
    const Limits& limits,
    const Charge& charge
);
// Outward L1 difference between the original exact source curve at an exact
// source parameter and a Cartesian source point times its exact mm ratio.
// This is a selector-consistency upper bound, not a sampled Euclidean residual
// or a full-curve error certificate. The unrequested curve reference/domain
// restrictions and callback/capacity contracts match CertifyCurveTrim.
[[nodiscard]] Result<double> SourceCurvePointDifferenceUpper(
    const std::shared_ptr<const Reference>& reference,
    const std::string& exactParameter,
    const std::array<std::string, 3>& sourcePoint,
    const IntegerRatio& lengthToMm,
    const Limits& limits,
    const Charge& charge
);
// Prove, on the WHOLE selected interval of a continuous positive-weight CURVE,
// that the closed pole-ball preimage has only components attached to allowed
// first/last endpoints. At least one point must be strictly outside the ball;
// an edge wholly within the working zone is never established. Isolated
// interior tangencies, leaving/reentering a ball and endpoint return visits
// are not exempted by an already represented pole vertex elsewhere on a face.
//
// Exact homogeneous Boehm extraction and Bernstein signs bound squared distance
// and its derivative; unknown pieces use at most 16 exact midpoint subdivisions.
// Resource/caller failures retain their status. A false proof means this
// conservative certificate is unestablished, NOT that the source is invalid.
// This proves the stored BinaryNet geometry only, not evaluator roundoff,
// STEP-source semantics, pole detection, topology or publication qualification.
[[nodiscard]] Result<PoleVisitProof> CheckPoleVisits(const BinaryNet& curve, const PoleVisits& visits, const Limits& limits, const Charge& charge);
// Exact source hull coverage in original scalar parameters, without tolerance.
[[nodiscard]] Result<bool> CheckScalarCoverage(
    const std::array<std::string, 2>& requested,
    const std::vector<std::string>& controls,
    const Limits& limits,
    const Charge& charge
);
// Outward L1 error relative to source decimals times the exact length ratio.
[[nodiscard]] Result<double> SourcePointDifferenceUpper(
    const std::array<std::string, 3>& source,
    const IntegerRatio& lengthToMm,
    const std::array<double, 3>& actual,
    const Limits& limits,
    const Charge& charge
);
} // namespace step_import::detail::terminal_spline
