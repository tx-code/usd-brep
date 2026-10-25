// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPcurveLineWindow.h"

#include "StepImportProofEnvironment.h"

#include <algorithm>
#include <bit>
#include <boost/multiprecision/cpp_int.hpp>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace step_import::detail
{
namespace
{
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53 && sizeof(double) == 8);
// No arbitrary-precision heap growth: every scalar and intermediate has fixed
// checked storage. Reduced scalars obey the caller's narrower bit allowance.
using Big = boost::multiprecision::number<
    boost::multiprecision::cpp_int_backend<8192, 8192, boost::multiprecision::signed_magnitude, boost::multiprecision::checked, void>>;
struct Ratio
{
    Big n = 0, d = 1;
};
Failure WindowError(const char* key, Status status = Status::Indeterminate)
{
    return { status, key, "Exact finite pcurve LINE window is unestablished", {}, "PcurveLineWindow" };
}
#define LW_TRY(name, expression)                                                                                                                     \
    auto name##Result = (expression);                                                                                                                \
    if (!name##Result)                                                                                                                               \
        return tl::unexpected(std::move(name##Result.error()));                                                                                      \
    auto name = std::move(*name##Result)
#define LW_OK(expression)                                                                                                                            \
    do                                                                                                                                               \
    {                                                                                                                                                \
        auto operation = (expression);                                                                                                               \
        if (!operation)                                                                                                                              \
            return tl::unexpected(std::move(operation.error()));                                                                                     \
    } while (false)

class Arithmetic
{
public:

    const PcurveLineWindowLimits& limits;
    const PcurveLineWindowCallbacks& callbacks;
    std::size_t used = 0, characters = 0;
    bool callbackActive = false;

    Arithmetic(const PcurveLineWindowLimits& l, const PcurveLineWindowCallbacks& c) : limits(l), callbacks(c)
    {
    }
    Result<void> Spend(std::size_t units = 1)
    {
        // Keep callback exceptions distinct from checked-integer overflow.
        callbackActive = true;
        auto cancelled = callbacks.checkCancelled();
        callbackActive = false;
        if (!cancelled)
        {
            return tl::unexpected(std::move(cancelled.error()));
        }
        if (!SurfaceProofNumericEnvironment())
        {
            return tl::unexpected(WindowError("PcurveLineWindowNumericEnvironment"));
        }
        if (units > limits.workUnits - used)
        {
            return tl::unexpected(WindowError("PcurveLineWindowWorkLimit", Status::ResourceLimit));
        }
        callbackActive = true;
        auto charged = callbacks.chargeWork(units);
        callbackActive = false;
        if (!charged)
        {
            return tl::unexpected(std::move(charged.error()));
        }
        // A successful callback may still alter the floating environment. The
        // last callback is also covered, so no plan escapes under a different
        // mode from the explicitly declared nearest-even construction mode.
        if (!SurfaceProofNumericEnvironment())
        {
            return tl::unexpected(WindowError("PcurveLineWindowNumericEnvironment"));
        }
        used += units;
        return {};
    }
    Result<void> Ready()
    {
        if (!callbacks.checkCancelled || !callbacks.chargeWork || !limits.workUnits || !limits.integerBits || limits.integerBits > 4096 ||
            !limits.outputCharacters || limits.outputCharacters > 32768 || !limits.tokenCharacters || limits.tokenCharacters > 2466)
        {
            return tl::unexpected(WindowError("PcurveLineWindowCapacityConfiguration", Status::ResourceLimit));
        }
        if (!SurfaceProofNumericEnvironment())
        {
            return tl::unexpected(WindowError("PcurveLineWindowNumericEnvironment"));
        }
        LW_OK(Spend());
        return {};
    }
    Result<Ratio> Normalize(Big n, Big d)
    {
        LW_OK(Spend());
        if (d == 0)
        {
            return tl::unexpected(WindowError("PcurveLineWindowExactDivision"));
        }
        if (n == 0)
        {
            return Ratio{};
        }
        if (d < 0)
        {
            n = -n;
            d = -d;
        }
        Big x = n < 0 ? -n : n, y = d;
        while (y != 0)
        {
            LW_OK(Spend());
            Big remainder = x % y;
            x = std::move(y);
            y = std::move(remainder);
        }
        n /= x;
        d /= x;
        const Big magnitude = n < 0 ? -n : n;
        if (boost::multiprecision::msb(magnitude) >= limits.integerBits || boost::multiprecision::msb(d) >= limits.integerBits)
        {
            return tl::unexpected(WindowError("PcurveLineWindowIntegerCapacity", Status::ResourceLimit));
        }
        return Ratio{ std::move(n), std::move(d) };
    }
    Result<int> Compare(const Ratio& a, const Ratio& b)
    {
        LW_OK(Spend());
        const Big l = a.n * b.d, r = b.n * a.d;
        return l < r ? -1 : l > r ? 1 : 0;
    }
    Result<Ratio> Add(const Ratio& a, const Ratio& b)
    {
        LW_OK(Spend());
        return Normalize(a.n * b.d + b.n * a.d, a.d * b.d);
    }
    Result<Ratio> Sub(const Ratio& a, const Ratio& b)
    {
        LW_OK(Spend());
        return Normalize(a.n * b.d - b.n * a.d, a.d * b.d);
    }
    Result<Ratio> Mul(const Ratio& a, const Ratio& b)
    {
        LW_OK(Spend());
        return Normalize(a.n * b.n, a.d * b.d);
    }
    Result<Ratio> Div(const Ratio& a, const Ratio& b)
    {
        LW_OK(Spend());
        return Normalize(a.n * b.d, a.d * b.n);
    }
    Result<Ratio> Binary(double value)
    {
        LW_OK(Spend());
        const auto bits = std::bit_cast<std::uint64_t>(value);
        const auto exponent = unsigned((bits >> 52) & 2047);
        const auto significand = (bits & 0xfffffffffffffULL) | (exponent ? 0x10000000000000ULL : 0);
        if (exponent == 2047)
        {
            return tl::unexpected(WindowError("PcurveLineWindowNonFinite"));
        }
        if (!significand)
        {
            return Ratio{};
        }
        Big n = significand, d = 1;
        const int power = exponent ? int(exponent) - 1023 - 52 : -1074;
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
        return Normalize(std::move(n), std::move(d));
    }
    Result<Ratio> Decimal(std::string_view text)
    {
        LW_OK(Spend());
        if (text.empty())
        {
            return tl::unexpected(WindowError("PcurveLineWindowDecimalSyntax"));
        }
        if (text.size() > limits.tokenCharacters)
        {
            return tl::unexpected(WindowError("PcurveLineWindowDecimalCapacity", Status::ResourceLimit));
        }
        bool negative = false;
        if (text.front() == '+' || text.front() == '-')
        {
            negative = text.front() == '-';
            text.remove_prefix(1);
        }
        Big n = 0, d = 1;
        bool digit = false, dot = false;
        int fractionDigits = 0;
        std::size_t i = 0;
        for (; i < text.size() && text[i] != 'e' && text[i] != 'E'; ++i)
        {
            LW_OK(Spend());
            const char c = text[i];
            if (c == '.' && !dot)
            {
                dot = true;
                continue;
            }
            if (c < '0' || c > '9')
            {
                return tl::unexpected(WindowError("PcurveLineWindowDecimalSyntax"));
            }
            digit = true;
            n *= 10;
            n += c - '0';
            if (dot)
            {
                ++fractionDigits;
            }
        }
        if (!digit)
        {
            return tl::unexpected(WindowError("PcurveLineWindowDecimalSyntax"));
        }
        int exponent = 0;
        if (i < text.size())
        {
            ++i;
            bool negativePower = false;
            if (i < text.size() && (text[i] == '+' || text[i] == '-'))
            {
                negativePower = text[i] == '-';
                ++i;
            }
            const auto firstDigit = i;
            for (; i < text.size(); ++i)
            {
                LW_OK(Spend());
                const char c = text[i];
                if (c < '0' || c > '9')
                {
                    return tl::unexpected(WindowError("PcurveLineWindowDecimalSyntax"));
                }
                if (exponent > (2466 - (c - '0')) / 10)
                {
                    return tl::unexpected(WindowError("PcurveLineWindowDecimalExponent", Status::ResourceLimit));
                }
                exponent = exponent * 10 + (c - '0');
            }
            if (i == firstDigit)
            {
                return tl::unexpected(WindowError("PcurveLineWindowDecimalSyntax"));
            }
            if (negativePower)
            {
                exponent = -exponent;
            }
        }
        exponent -= fractionDigits;
        if (exponent < -2466 || exponent > 2466)
        {
            return tl::unexpected(WindowError("PcurveLineWindowDecimalExponent", Status::ResourceLimit));
        }
        if (n == 0)
        {
            return Ratio{};
        }
        Big& expanded = exponent < 0 ? d : n;
        for (int power = 0, count = exponent < 0 ? -exponent : exponent; power < count; ++power)
        {
            LW_OK(Spend());
            expanded *= 10;
        }
        if (negative)
        {
            n = -n;
        }
        return Normalize(std::move(n), std::move(d));
    }
    Result<double> Rounded(const Ratio& value)
    {
        LW_OK(Spend());
        if (value.n == 0)
        {
            return 0.;
        }
        Big n = value.n < 0 ? -value.n : value.n;
        int e = int(boost::multiprecision::msb(n)) - int(boost::multiprecision::msb(value.d));
        if (e >= 0)
        {
            if (n < (value.d << e))
            {
                --e;
            }
        }
        else if ((n << -e) < value.d)
        {
            --e;
        }
        if (e > 1023)
        {
            return tl::unexpected(WindowError("PcurveLineWindowParameterBinaryRange"));
        }
        const std::uint64_t sign = value.n < 0 ? 0x8000000000000000ULL : 0;
        if (e < -1075)
        {
            return std::bit_cast<double>(sign);
        }
        // Integer quotient/remainder supplies IEEE nearest-even, including
        // subnormals. There is no floating division or inward adjustment here.
        const int shift = e < -1022 ? 1074 : 52 - e;
        Big denominator = value.d;
        if (shift >= 0)
        {
            n <<= shift;
        }
        else
        {
            denominator <<= -shift;
        }
        Big q = n / denominator, remainder = n % denominator;
        if (2 * remainder > denominator || (2 * remainder == denominator && (q & 1) != 0))
        {
            ++q;
        }
        if (e < -1022)
        {
            return std::bit_cast<double>(sign | q.convert_to<std::uint64_t>());
        }
        if (q == (Big(1) << 53))
        {
            q >>= 1;
            ++e;
        }
        if (e > 1023)
        {
            return tl::unexpected(WindowError("PcurveLineWindowParameterBinaryRange"));
        }
        return std::bit_cast<double>(sign | (std::uint64_t(e + 1023) << 52) | (q.convert_to<std::uint64_t>() - 0x10000000000000ULL));
    }
    Result<double> OutwardNonnegative(const Ratio& value)
    {
        LW_TRY(rounded, Rounded(value));
        LW_TRY(binary, Binary(rounded));
        LW_TRY(comparison, Compare(binary, value));
        if (comparison < 0)
        {
            // value >= 0. Advancing positive IEEE bits is one outward ULP;
            // unlike cropping the window, this changes only an error bound.
            rounded = std::bit_cast<double>(std::bit_cast<std::uint64_t>(rounded) + 1);
            LW_TRY(checked, Binary(rounded));
            LW_TRY(finalComparison, Compare(checked, value));
            if (finalComparison < 0)
            {
                return tl::unexpected(WindowError("PcurveLineWindowRoundingPostcondition"));
            }
        }
        return rounded;
    }
    Result<std::size_t> DecimalLength(const Big& integer)
    {
        Big remaining = integer < 0 ? -integer : integer;
        std::size_t length = integer < 0 ? 1 : 0;
        do
        {
            LW_OK(Spend());
            ++length;
            remaining /= 10;
        } while (remaining != 0);
        return length;
    }
    Result<PcurveLineWindowRatio> Text(const Ratio& value)
    {
        LW_TRY(n, DecimalLength(value.n));
        LW_TRY(d, DecimalLength(value.d));
        if (n > limits.outputCharacters - characters || d > limits.outputCharacters - characters - n)
        {
            return tl::unexpected(WindowError("PcurveLineWindowOutputCapacity", Status::ResourceLimit));
        }
        LW_OK(Spend(n + d));
        characters += n + d;
        return PcurveLineWindowRatio{ value.n.convert_to<std::string>(), value.d.convert_to<std::string>() };
    }
};

using ExactPair = std::array<Ratio, 2>;
struct ExactKnotChart
{
    ExactPair scale, offset;
    std::array<ExactPair, 2> sourceDomains;
    std::array<std::size_t, 2> counts{};
    std::array<std::optional<unsigned>, 2> fixedBoundary;
    std::array<bool, 2> requiresBoundaryCurveCertificate{};
};
Result<bool> ClampedBoundaryKnots(
    std::span<const std::string_view> source,
    std::span<const double> native,
    unsigned degree,
    const ExactPair& sourceDomain,
    const ExactPair& nativeDomain,
    Arithmetic& math
)
{
    const auto p = std::size_t(degree);
    LW_OK(math.Spend());
    // Same expanded count and an attested common degree imply the same number
    // of basis controls. Strict interior knots ensure exactly p+1 endpoint
    // repeats. No claimed C0/native qualification is inferred from this check.
    if (!p || source.size() < 2 || p >= source.size() / 2 || source.size() != native.size())
    {
        return false;
    }
    ExactPair previous;
    std::array<std::size_t, 2> repeats{};
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        LW_TRY(sourceKnot, math.Decimal(source[i]));
        LW_TRY(nativeKnot, math.Binary(native[i]));
        const ExactPair current{ std::move(sourceKnot), std::move(nativeKnot) };
        std::array<bool, 2> samePrevious{};
        for (std::size_t which = 0; which < 2; ++which)
        {
            const auto& domain = which ? nativeDomain : sourceDomain;
            LW_TRY(lower, math.Compare(current[which], domain[0]));
            LW_TRY(upper, math.Compare(current[which], domain[1]));
            if ((i <= p && lower != 0) || (i >= source.size() - p - 1 && upper != 0) ||
                (i > p && i < source.size() - p - 1 && !(lower > 0 && upper < 0)))
            {
                return false;
            }
            if (i)
            {
                LW_TRY(step, math.Compare(previous[which], current[which]));
                if (step > 0)
                {
                    return false;
                }
                samePrevious[which] = step == 0;
                repeats[which] = step == 0 ? repeats[which] + 1 : 1;
            }
            else
            {
                repeats[which] = 1;
            }
            if (repeats[which] > p + 1)
            {
                return false;
            }
        }
        // Boundary restriction is narrower than whole-axis covariance, but
        // does not authorize merging or splitting source knot groups. Preserve
        // the exact adjacent-equality pattern as well as each side's validity.
        if (i && samePrevious[0] != samePrevious[1])
        {
            return false;
        }
        previous = current;
    }
    return true;
}
Result<void> CheckKnotChartStructure(const PcurveLineKnotChartInput& input, Arithmetic& math)
{
    constexpr std::size_t maximumPairedKnots = 8192;
    std::size_t total = 0;
    for (std::size_t axis = 0; axis < 2; ++axis)
    {
        const auto count = input.sourceKnots[axis].size();
        if (count > maximumPairedKnots - total || input.nativeKnots[axis].size() > maximumPairedKnots - total)
        {
            return tl::unexpected(WindowError("PcurveLineWindowKnotCapacity", Status::ResourceLimit));
        }
        if (count < 2 || count != input.nativeKnots[axis].size())
        {
            return tl::unexpected(WindowError("PcurveLineWindowKnotChartStructure"));
        }
        total += count;
    }
    // Account for both borrowed expanded arrays before inspecting any knot.
    // No per-knot allocation is needed: previous values suffice to check order.
    LW_OK(math.Spend(2 * total));
    return {};
}
Result<ExactKnotChart> ProveKnotChart(
    const PcurveLineKnotChartInput& input,
    const ExactPair& point,
    const ExactPair& velocity,
    const std::array<Ratio, 4>& rectangle,
    Arithmetic& math
)
{
    LW_OK(CheckKnotChartStructure(input, math));
    ExactKnotChart result;
    for (std::size_t axis = 0; axis < 2; ++axis)
    {
        const auto source = input.sourceKnots[axis];
        const auto native = input.nativeKnots[axis];
        LW_TRY(sourceLo, math.Decimal(source.front()));
        LW_TRY(sourceHi, math.Decimal(source.back()));
        LW_TRY(nativeLo, math.Binary(native.front()));
        LW_TRY(nativeHi, math.Binary(native.back()));
        LW_TRY(sourceOrder, math.Compare(sourceLo, sourceHi));
        LW_TRY(nativeOrder, math.Compare(nativeLo, nativeHi));
        LW_TRY(lowerDomain, math.Compare(nativeLo, rectangle[axis]));
        LW_TRY(upperDomain, math.Compare(nativeHi, rectangle[axis + 2]));
        if (sourceOrder >= 0 || nativeOrder >= 0 || lowerDomain != 0 || upperDomain != 0)
        {
            return tl::unexpected(WindowError("PcurveLineWindowKnotChartDomain"));
        }
        LW_TRY(sourceWidth, math.Sub(sourceHi, sourceLo));
        LW_TRY(nativeWidth, math.Sub(nativeHi, nativeLo));
        LW_TRY(scale, math.Div(nativeWidth, sourceWidth));
        LW_TRY(scaledLo, math.Mul(scale, sourceLo));
        LW_TRY(offset, math.Sub(nativeLo, scaledLo));
        LW_TRY(positive, math.Compare(scale, Ratio{}));
        if (positive <= 0)
        {
            return tl::unexpected(WindowError("PcurveLineWindowKnotChartUnestablished"));
        }
        std::optional<unsigned> fixedBoundary;
        if (input.degrees[axis] && velocity[axis].n == 0)
        {
            LW_TRY(atMinimum, math.Compare(point[axis], sourceLo));
            LW_TRY(atMaximum, math.Compare(point[axis], sourceHi));
            if (atMinimum == 0)
            {
                fixedBoundary = 0;
            }
            else if (atMaximum == 0)
            {
                fixedBoundary = 1;
            }
        }
        bool wholeAxis = true;
        Ratio previousSource, previousNative;
        for (std::size_t i = 0; i < source.size(); ++i)
        {
            LW_TRY(sourceKnot, math.Decimal(source[i]));
            LW_TRY(nativeKnot, math.Binary(native[i]));
            if (i)
            {
                LW_TRY(sourceStep, math.Compare(previousSource, sourceKnot));
                LW_TRY(nativeStep, math.Compare(previousNative, nativeKnot));
                if (sourceStep > 0 || nativeStep > 0)
                {
                    return tl::unexpected(WindowError("PcurveLineWindowKnotChartOrder"));
                }
            }
            LW_TRY(scaled, math.Mul(scale, sourceKnot));
            LW_TRY(mapped, math.Add(scaled, offset));
            LW_TRY(equal, math.Compare(mapped, nativeKnot));
            if (equal != 0)
            {
                if (!fixedBoundary)
                {
                    return tl::unexpected(WindowError("PcurveLineWindowKnotChartUnestablished"));
                }
                wholeAxis = false;
            }
            previousSource = std::move(sourceKnot);
            previousNative = std::move(nativeKnot);
        }
        if (!wholeAxis)
        {
            LW_TRY(clamped, ClampedBoundaryKnots(source, native, input.degrees[axis], { sourceLo, sourceHi }, { nativeLo, nativeHi }, math));
            if (!clamped)
            {
                return tl::unexpected(WindowError("PcurveLineWindowBoundaryRestrictionUnestablished"));
            }
            // A clamped B-spline at its exact minimum/maximum selects ONLY its
            // first/last control row. Interior knots on that fixed axis have
            // no contribution. This proves this LINE use, not an interior
            // coordinate map; preserve that distinction in the owned receipt.
            LW_TRY(boundaryOffset, math.Sub(*fixedBoundary ? nativeHi : nativeLo, *fixedBoundary ? sourceHi : sourceLo));
            LW_TRY(identity, math.Normalize(Big(1), Big(1)));
            scale = std::move(identity);
            offset = std::move(boundaryOffset);
            result.fixedBoundary[axis] = fixedBoundary;
        }
        // Otherwise ALL expanded knots, including multiplicities, obey the
        // same A,B: unchanged degree/XYZ/W then give full-axis covariance.
        result.scale[axis] = std::move(scale);
        result.offset[axis] = std::move(offset);
        result.sourceDomains[axis] = { std::move(sourceLo), std::move(sourceHi) };
        result.counts[axis] = source.size();
    }
    return result;
}
Result<std::optional<ExactKnotChart>> ProposeBoundaryKnotChart(
    const PcurveLineKnotChartInput& input,
    const ExactPair& point,
    const ExactPair& velocity,
    const std::array<Ratio, 4>& rectangle,
    Arithmetic& math
)
{
    LW_OK(CheckKnotChartStructure(input, math));
    if (!input.degrees[0] || !input.degrees[1])
    {
        LW_OK(math.Spend());
        return std::optional<ExactKnotChart>{};
    }
    ExactKnotChart result;
    std::array<ExactPair, 2> nativeDomains;
    // Validate both complete carriers before normal eligibility can return an
    // absence. A source knot group merge/split is an input-evidence failure,
    // not permission to fall back to another numerical interpretation.
    for (std::size_t axis = 0; axis < 2; ++axis)
    {
        const auto source = input.sourceKnots[axis];
        const auto native = input.nativeKnots[axis];
        LW_TRY(sourceLo, math.Decimal(source.front()));
        LW_TRY(sourceHi, math.Decimal(source.back()));
        LW_TRY(nativeLo, math.Binary(native.front()));
        LW_TRY(nativeHi, math.Binary(native.back()));
        LW_TRY(sourceOrder, math.Compare(sourceLo, sourceHi));
        LW_TRY(nativeOrder, math.Compare(nativeLo, nativeHi));
        LW_TRY(lowerDomain, math.Compare(nativeLo, rectangle[axis]));
        LW_TRY(upperDomain, math.Compare(nativeHi, rectangle[axis + 2]));
        if (sourceOrder >= 0 || nativeOrder >= 0 || lowerDomain != 0 || upperDomain != 0)
        {
            return tl::unexpected(WindowError("PcurveLineWindowKnotChartDomain"));
        }
        LW_TRY(clamped, ClampedBoundaryKnots(source, native, input.degrees[axis], { sourceLo, sourceHi }, { nativeLo, nativeHi }, math));
        if (!clamped)
        {
            return tl::unexpected(WindowError("PcurveLineWindowBoundaryPendingStructure"));
        }
        result.sourceDomains[axis] = { std::move(sourceLo), std::move(sourceHi) };
        nativeDomains[axis] = { std::move(nativeLo), std::move(nativeHi) };
        result.counts[axis] = source.size();
    }
    const bool fixedU = velocity[0].n == 0, fixedV = velocity[1].n == 0;
    if (fixedU == fixedV)
    {
        return std::optional<ExactKnotChart>{};
    }
    const std::size_t fixed = fixedU ? 0 : 1, variable = 1 - fixed;
    LW_TRY(atMinimum, math.Compare(point[fixed], result.sourceDomains[fixed][0]));
    LW_TRY(atMaximum, math.Compare(point[fixed], result.sourceDomains[fixed][1]));
    if (atMinimum != 0 && atMaximum != 0)
    {
        return std::optional<ExactKnotChart>{};
    }
    const unsigned end = atMinimum == 0 ? 0 : 1;
    LW_TRY(identity, math.Normalize(Big(1), Big(1)));
    LW_TRY(boundaryOffset, math.Sub(nativeDomains[fixed][end], result.sourceDomains[fixed][end]));
    result.scale[fixed] = std::move(identity);
    result.offset[fixed] = std::move(boundaryOffset);
    result.fixedBoundary[fixed] = end;

    // Each variable-axis knot has its original nearest conversion, but the
    // endpoint-derived A/B may NOT map the interior knots. Therefore this
    // candidate cannot be delivered through the established-chart API: only
    // the later continuous source-boundary curve certificate can admit it.
    const auto source = input.sourceKnots[variable];
    const auto native = input.nativeKnots[variable];
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        LW_TRY(sourceKnot, math.Decimal(source[i]));
        LW_TRY(nativeKnot, math.Binary(native[i]));
        LW_TRY(nearest, math.Rounded(sourceKnot));
        LW_TRY(roundedKnot, math.Binary(nearest));
        LW_TRY(equal, math.Compare(nativeKnot, roundedKnot));
        if (equal != 0)
        {
            return tl::unexpected(WindowError("PcurveLineWindowBoundaryPendingKnotConversion"));
        }
    }
    LW_TRY(sourceWidth, math.Sub(result.sourceDomains[variable][1], result.sourceDomains[variable][0]));
    LW_TRY(nativeWidth, math.Sub(nativeDomains[variable][1], nativeDomains[variable][0]));
    LW_TRY(scale, math.Div(nativeWidth, sourceWidth));
    LW_TRY(scaledLo, math.Mul(scale, result.sourceDomains[variable][0]));
    LW_TRY(offset, math.Sub(nativeDomains[variable][0], scaledLo));
    result.scale[variable] = std::move(scale);
    result.offset[variable] = std::move(offset);
    result.requiresBoundaryCurveCertificate[variable] = true;
    return std::optional<ExactKnotChart>{ std::move(result) };
}
Result<PcurveLineKnotChart> ChartText(const ExactKnotChart& chart, const std::array<double, 4>& domain, Arithmetic& math)
{
    PcurveLineKnotChart text;
    for (std::size_t axis = 0; axis < 2; ++axis)
    {
        LW_TRY(scale, math.Text(chart.scale[axis]));
        LW_TRY(offset, math.Text(chart.offset[axis]));
        text.axes[axis].scale = std::move(scale);
        text.axes[axis].offset = std::move(offset);
        for (std::size_t end = 0; end < 2; ++end)
        {
            LW_TRY(sourceEnd, math.Text(chart.sourceDomains[axis][end]));
            text.axes[axis].sourceDomainExact[end] = std::move(sourceEnd);
        }
        text.axes[axis].targetDomain = { domain[axis], domain[axis + 2] };
        text.axes[axis].knotsChecked = chart.counts[axis];
        text.axes[axis].fixedSourceBoundary = chart.fixedBoundary[axis];
        text.axes[axis].requiresBoundaryCurveCertificate = chart.requiresBoundaryCurveCertificate[axis];
    }
    return text;
}
Result<std::optional<PcurveLineWindow>> Intersect(
    const ExactPair& point,
    const ExactPair& velocity,
    const std::array<Ratio, 4>& domain,
    Arithmetic& math
)
{
    for (std::size_t axis = 0; axis < 2; ++axis)
    {
        LW_TRY(width, math.Compare(domain[axis], domain[axis + 2]));
        if (width >= 0)
        {
            return tl::unexpected(WindowError("PcurveLineWindowDomain"));
        }
    }
    if (velocity[0].n == 0 && velocity[1].n == 0)
    {
        return tl::unexpected(WindowError("PcurveLineWindowZeroVelocity"));
    }
    std::optional<ExactPair> interval;
    for (std::size_t axis = 0; axis < 2; ++axis)
    {
        LW_OK(math.Spend());
        if (velocity[axis].n == 0)
        {
            LW_TRY(lower, math.Compare(point[axis], domain[axis]));
            LW_TRY(upper, math.Compare(point[axis], domain[axis + 2]));
            if (lower < 0 || upper > 0)
            {
                return std::optional<PcurveLineWindow>{};
            }
            continue;
        }
        LW_TRY(deltaLo, math.Sub(domain[axis], point[axis]));
        LW_TRY(deltaHi, math.Sub(domain[axis + 2], point[axis]));
        LW_TRY(lo, math.Div(deltaLo, velocity[axis]));
        LW_TRY(hi, math.Div(deltaHi, velocity[axis]));
        LW_TRY(order, math.Compare(lo, hi));
        if (order > 0)
        {
            std::swap(lo, hi);
        }
        if (!interval)
        {
            interval = ExactPair{ std::move(lo), std::move(hi) };
        }
        else
        {
            LW_TRY(lower, math.Compare(lo, (*interval)[0]));
            LW_TRY(upper, math.Compare(hi, (*interval)[1]));
            if (lower > 0)
            {
                (*interval)[0] = std::move(lo);
            }
            if (upper < 0)
            {
                (*interval)[1] = std::move(hi);
            }
        }
    }
    // At least one velocity component is nonzero, so a finite rectangle gives
    // a finite interval. Equal endpoints are contact, not a usable pcurve.
    if (!interval)
    {
        return tl::unexpected(WindowError("PcurveLineWindowIntervalPostcondition"));
    }
    LW_TRY(order, math.Compare((*interval)[0], (*interval)[1]));
    if (order >= 0)
    {
        return std::optional<PcurveLineWindow>{};
    }

    PcurveLineWindow plan;
    std::array<ExactPair, 2> roundedEnds;
    for (std::size_t end = 0; end < 2; ++end)
    {
        LW_TRY(qText, math.Text((*interval)[end]));
        plan.generatorWindowExact[end] = std::move(qText);
        LW_TRY(q, math.Rounded((*interval)[end]));
        plan.generatorWindowRounded[end] = q;
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            LW_TRY(displacement, math.Mul((*interval)[end], velocity[axis]));
            LW_TRY(coordinate, math.Add(point[axis], displacement));
            LW_TRY(lower, math.Compare(coordinate, domain[axis]));
            LW_TRY(upper, math.Compare(coordinate, domain[axis + 2]));
            if (lower < 0 || upper > 0)
            {
                return tl::unexpected(WindowError("PcurveLineWindowIntersectionPostcondition"));
            }
            LW_TRY(text, math.Text(coordinate));
            plan.uvEndsExact[end][axis] = std::move(text);
            LW_TRY(rounded, math.Rounded(coordinate));
            plan.uvEnds[end][axis] = rounded;
            LW_TRY(binary, math.Binary(rounded));
            roundedEnds[end][axis] = binary;
            // Monotone nearest rounding preserves inclusion between binary
            // rectangle endpoints. Check that invariant; never clamp a result.
            LW_TRY(roundedLower, math.Compare(binary, domain[axis]));
            LW_TRY(roundedUpper, math.Compare(binary, domain[axis + 2]));
            if (roundedLower < 0 || roundedUpper > 0)
            {
                return tl::unexpected(WindowError("PcurveLineWindowRoundingPostcondition"));
            }
            LW_TRY(error, math.Sub(coordinate, binary));
            if (error.n < 0)
            {
                error.n = -error.n;
            }
            plan.hasUvRounding = plan.hasUvRounding || error.n != 0;
            LW_TRY(bound, math.OutwardNonnegative(error));
            plan.endpointRoundingUpperUv[end][axis] = bound;
            plan.maximumRoundingUpperUv[axis] = std::max(plan.maximumRoundingUpperUv[axis], bound);
        }
    }
    LW_TRY(equalU, math.Compare(roundedEnds[0][0], roundedEnds[1][0]));
    LW_TRY(equalV, math.Compare(roundedEnds[0][1], roundedEnds[1][1]));
    if (equalU == 0 && equalV == 0)
    {
        return tl::unexpected(WindowError("PcurveLineWindowEndpointCollapse"));
    }
    LW_TRY(span, math.Sub((*interval)[1], (*interval)[0]));
    LW_TRY(spanText, math.Text(span));
    plan.generatorSpanExact = std::move(spanText);
    LW_TRY(roundedLo, math.Binary(plan.generatorWindowRounded[0]));
    LW_TRY(roundedHi, math.Binary(plan.generatorWindowRounded[1]));
    LW_TRY(roundedOrder, math.Compare(roundedLo, roundedHi));
    plan.generatorRoundingCollapsed = roundedOrder == 0;
    // For tau in [0,1], UV error is (1-tau)*e0 + tau*e1. Thus the
    // per-axis maximum of outward endpoint bounds covers the entire segment,
    // without a sampling claim or a surface/physical error claim.
    return std::optional<PcurveLineWindow>{ std::move(plan) };
}

Result<std::optional<PcurveLineWindow>> Run(
    const std::array<double, 2>* binaryPoint,
    const std::array<double, 2>* binaryVelocity,
    const ExactDecimalPcurveLineGenerator* decimal,
    const PcurveLineKnotChartInput* knots,
    const std::array<double, 4>& domain,
    const PcurveLineWindowLimits& limits,
    const PcurveLineWindowCallbacks& callbacks,
    bool pendingBoundary = false
)
{
    Arithmetic math(limits, callbacks);
    try
    {
        LW_OK(math.Ready());
        ExactPair point, velocity;
        std::array<Ratio, 4> rectangle;
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            LW_TRY(p, decimal ? math.Decimal(decimal->point[axis]) : math.Binary((*binaryPoint)[axis]));
            LW_TRY(v, decimal ? math.Decimal(decimal->velocity[axis]) : math.Binary((*binaryVelocity)[axis]));
            point[axis] = std::move(p);
            velocity[axis] = std::move(v);
        }
        for (std::size_t i = 0; i < 4; ++i)
        {
            LW_TRY(bound, math.Binary(domain[i]));
            rectangle[i] = std::move(bound);
        }
        std::optional<ExactKnotChart> chart;
        if (knots)
        {
            ExactKnotChart proof;
            if (pendingBoundary)
            {
                LW_TRY(proposed, ProposeBoundaryKnotChart(*knots, point, velocity, rectangle, math));
                if (!proposed)
                {
                    LW_OK(math.Spend());
                    return std::optional<PcurveLineWindow>{};
                }
                proof = std::move(*proposed);
            }
            else
            {
                LW_TRY(proven, ProveKnotChart(*knots, point, velocity, rectangle, math));
                proof = std::move(proven);
            }
            for (std::size_t axis = 0; axis < 2; ++axis)
            {
                LW_TRY(scaledPoint, math.Mul(proof.scale[axis], point[axis]));
                LW_TRY(mappedPoint, math.Add(scaledPoint, proof.offset[axis]));
                LW_TRY(mappedVelocity, math.Mul(proof.scale[axis], velocity[axis]));
                point[axis] = std::move(mappedPoint);
                velocity[axis] = std::move(mappedVelocity);
            }
            chart = std::move(proof);
        }
        LW_TRY(result, Intersect(point, velocity, rectangle, math));
        if (chart && result)
        {
            LW_TRY(text, ChartText(*chart, domain, math));
            result->affineSourceChart = std::move(text);
        }
        LW_OK(math.Spend()); // Complete handoff, also for normal absence.
        return result;
    }
    catch (const std::overflow_error&)
    {
        if (math.callbackActive)
        {
            throw;
        }
        return tl::unexpected(WindowError("PcurveLineWindowIntegerCapacity", Status::ResourceLimit));
    }
}
#undef LW_TRY
#undef LW_OK
} // namespace

Result<std::optional<PcurveLineWindow>> PlanPcurveLineWindow(
    const std::array<double, 2>& point,
    const std::array<double, 2>& velocity,
    const std::array<double, 4>& domain,
    const PcurveLineWindowLimits& limits,
    const PcurveLineWindowCallbacks& callbacks
)
{
    return Run(&point, &velocity, nullptr, nullptr, domain, limits, callbacks);
}
Result<std::optional<PcurveLineWindow>> PlanPcurveLineWindow(
    const ExactDecimalPcurveLineGenerator& generator,
    const std::array<double, 4>& domain,
    const PcurveLineWindowLimits& limits,
    const PcurveLineWindowCallbacks& callbacks
)
{
    return Run(nullptr, nullptr, &generator, nullptr, domain, limits, callbacks);
}
Result<std::optional<PcurveLineWindow>> PlanPcurveLineWindow(
    const ExactDecimalPcurveLineGenerator& generator,
    const PcurveLineKnotChartInput& chart,
    const std::array<double, 4>& nativeDomain,
    const PcurveLineWindowLimits& limits,
    const PcurveLineWindowCallbacks& callbacks
)
{
    return Run(nullptr, nullptr, &generator, &chart, nativeDomain, limits, callbacks);
}
PendingBoundaryPcurveLineWindow::PendingBoundaryPcurveLineWindow(PcurveLineWindow window, std::size_t fixedAxis, unsigned fixedEnd)
    : m_window(std::move(window)), m_fixedAxis(fixedAxis), m_fixedEnd(fixedEnd)
{
}
const PcurveLineWindow& PendingBoundaryPcurveLineWindow::ProposedWindow() const noexcept
{
    return m_window;
}
std::size_t PendingBoundaryPcurveLineWindow::FixedAxis() const noexcept
{
    return m_fixedAxis;
}
unsigned PendingBoundaryPcurveLineWindow::FixedEnd() const noexcept
{
    return m_fixedEnd;
}
Result<std::optional<PendingBoundaryPcurveLineWindow>> PlanBoundaryPcurveLineWindow(
    const ExactDecimalPcurveLineGenerator& generator,
    const PcurveLineKnotChartInput& chart,
    const std::array<double, 4>& nativeDomain,
    const PcurveLineWindowLimits& limits,
    const PcurveLineWindowCallbacks& callbacks
)
{
    auto planned = Run(nullptr, nullptr, &generator, &chart, nativeDomain, limits, callbacks, true);
    if (!planned)
    {
        return tl::unexpected(std::move(planned.error()));
    }
    if (!*planned)
    {
        return std::optional<PendingBoundaryPcurveLineWindow>{};
    }
    auto& window = **planned;
    const auto& axes = window.affineSourceChart->axes;
    const std::size_t fixed = axes[0].fixedSourceBoundary ? 0 : 1;
    const auto end = *axes[fixed].fixedSourceBoundary;
    return std::optional<PendingBoundaryPcurveLineWindow>{ PendingBoundaryPcurveLineWindow(std::move(window), fixed, end) };
}
} // namespace step_import::detail
