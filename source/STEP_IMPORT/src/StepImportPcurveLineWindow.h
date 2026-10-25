// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace step_import::detail
{
struct PcurveLineWindowRatio
{
    std::string numerator;
    std::string denominator;
};

struct PcurveLineWindowLimits
{
    // Reduced scalars have at most 4096 bits; checked intermediates have fixed
    // 8192-bit storage. These are evidence capacities, not geometry tolerances.
    std::size_t integerBits = 4096;
    std::size_t workUnits = 0;
    std::size_t outputCharacters = 0; // Total ratio-text bytes, at most 32768.
    std::size_t tokenCharacters = 1234; // Decimal overload only; at most 2466.
};

struct PcurveLineWindowCallbacks
{
    // Both are mandatory. This module calls no native API and therefore must
    // not consume a native-call allowance. Every exact operation is charged;
    // cancellation is checked before work and before returning a result.
    std::function<Result<void>(std::size_t)> chargeWork;
    std::function<Result<void>()> checkCancelled;
};

struct ExactDecimalPcurveLineGenerator
{
    std::array<std::string_view, 2> point;
    std::array<std::string_view, 2> velocity;
};

struct PcurveLineKnotChartInput
{
    // Paired full EXPANDED knots, in U/V order. These are borrowed only for
    // this call; no source text or native knot storage is mutated or retained.
    // The paired U+V count is bounded to 8192, before reading any knot values.
    std::array<std::span<const std::string_view>, 2> sourceKnots;
    std::array<std::span<const double>, 2> nativeKnots;
    // A positive value authorizes only the separately checked fixed-boundary
    // restriction below. The caller must first establish that source/native
    // surface degrees agree; zero does not authorize that restricted proof.
    std::array<unsigned, 2> degrees{};
};
struct PcurveLineKnotChartAxis
{
    // native_parameter = scale*source_parameter + offset, with scale > 0.
    // Unless fixedSourceBoundary is present, every expanded knot must satisfy
    // this equality EXACTLY. This is not fitted/approximate knot conversion.
    PcurveLineWindowRatio scale;
    PcurveLineWindowRatio offset;
    std::array<PcurveLineWindowRatio, 2> sourceDomainExact;
    std::array<double, 2> targetDomain{};
    std::size_t knotsChecked = 0;
    // 0=min, 1=max: ONLY the fixed source boundary use is established. Then
    // scale=1 and offset is the exact boundary-coordinate difference. Interior
    // knots may have no common affine chart; never use this as a whole-axis
    // coordinate equivalence. It relies on clamped boundary basis identity and
    // the caller's binding of the complete unchanged control net and degrees.
    std::optional<unsigned> fixedSourceBoundary;
};
struct PcurveLineKnotChart
{
    std::array<PcurveLineKnotChartAxis, 2> axes;
};

struct PcurveLineWindow
{
    // The generator is P + q*D, without direction normalization. q is not
    // assumed to be the physical STEP LINE parameter. Its full intersection
    // with the rectangle is [lo,hi], including both boundary points.
    std::array<PcurveLineWindowRatio, 2> generatorWindowExact;
    PcurveLineWindowRatio generatorSpanExact;
    std::array<double, 2> generatorWindowRounded;
    bool generatorRoundingCollapsed = false;

    // Native tau is exactly [0,1]: q = lo + (hi-lo)*tau. The exact endpoints
    // retain that identity even if rounded q endpoints coincide. uvEnds are
    // nearest-even, remain inside the original binary rectangle, and must be
    // distinct. No inward nextafter, clamping or curve fitting is performed.
    static constexpr std::array<double, 2> NativeParameterInterval{ 0., 1. };
    std::array<std::array<PcurveLineWindowRatio, 2>, 2> uvEndsExact;
    std::array<std::array<double, 2>, 2> uvEnds{};
    std::array<std::array<double, 2>, 2> endpointRoundingUpperUv{};
    // Convex endpoint error bounds each whole affine segment, per UV axis.
    // These are UV bounds; a physical 3D lift needs separate surface evidence.
    std::array<double, 2> maximumRoundingUpperUv{};
    bool hasUvRounding = false;
    std::optional<PcurveLineKnotChart> affineSourceChart;
};

// Rectangle order is {u_min,v_min,u_max,v_max}; both widths must be positive.
// Empty, parallel-outside and point-only intersections return nullopt. Invalid
// inputs, unrepresentable finite q endpoints, or collapsed rounded UV endpoints
// return Indeterminate. Capacity/rounding-mode failures are explicit. Caller
// callback failures propagate unchanged; allocation/caller exceptions propagate.
// Inputs and floating control modes are never modified. FE_TONEAREST and
// gradual underflow are required, including after every successful callback;
// as with other proof-environment checks, FP status flags are not preserved.
[[nodiscard]] Result<std::optional<PcurveLineWindow>> PlanPcurveLineWindow(
    const std::array<double, 2>& point,
    const std::array<double, 2>& velocity,
    const std::array<double, 4>& domain,
    const PcurveLineWindowLimits& limits,
    const PcurveLineWindowCallbacks& callbacks
);

// Accepts already parsed numeric lexemes, not STEP syntax/entities: optional
// sign, decimal digits with at most one dot, optional E/e signed integer power.
// At least one mantissa digit is required; whitespace/NaN/Inf/D exponents are
// rejected. Decimal powers are bounded to +/-2466 before exact reduction; all
// decimal digits, powers and integer operations use the same finite ledger.
[[nodiscard]] Result<std::optional<PcurveLineWindow>> PlanPcurveLineWindow(
    const ExactDecimalPcurveLineGenerator& generator,
    const std::array<double, 4>& domain,
    const PcurveLineWindowLimits& limits,
    const PcurveLineWindowCallbacks& callbacks
);

// First attempts WHOLE expanded source/native knot correspondence before using
// the chart on the exact source LINE. With an explicit shared positive degree,
// a failing whole-axis proof may establish a narrower fixed-boundary use:
// D=0 and P EXACTLY equals the source endpoint; both full knot arrays must be
// valid and exactly clamped with degree+1 endpoint multiplicities. The curve
// then uses the same first/last control row independently of interior knots.
// This is separately marked fixedSourceBoundary, never a whole-axis claim.
// nativeDomain must equal the two
// actual native knot end domains. q remains the original generator parameter:
// P'=A*P+B, D'=A*D; no direction normalization, inward crop or source rewrite.
// Failure to establish a positive exact common affine chart is Indeterminate;
// bad capacities/callbacks retain the same contracts as the other overloads.
// The geometry/control-net/degree/weight binding of a surface to these knots
// belongs to the caller; knot correspondence alone is not a surface proof.
[[nodiscard]] Result<std::optional<PcurveLineWindow>> PlanPcurveLineWindow(
    const ExactDecimalPcurveLineGenerator& generator,
    const PcurveLineKnotChartInput& chart,
    const std::array<double, 4>& nativeDomain,
    const PcurveLineWindowLimits& limits,
    const PcurveLineWindowCallbacks& callbacks
);
} // namespace step_import::detail
