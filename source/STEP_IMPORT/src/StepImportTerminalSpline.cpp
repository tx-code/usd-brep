// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportTerminalSpline.h"

#include "StepImportAnalyticLinePole.h"

#include <algorithm>
#include <bit>
#include <boost/multiprecision/cpp_int.hpp>
#include <charconv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
namespace step_import::detail::terminal_spline
{
namespace
{
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53 && sizeof(double) == 8);
// Reduced ratios have <=4096 bits; fixed checked 8192-bit intermediates bound
// products and binary64 quantization. Overflow refuses evidence, never wraps.
using Big = boost::multiprecision::number<
    boost::multiprecision::cpp_int_backend<8192, 8192, boost::multiprecision::signed_magnitude, boost::multiprecision::checked, void>>;
struct R
{
    Big n = 0, d = 1;
};
using H = std::array<R, 4>;
struct Net
{
    unsigned p = 0, q = 0;
    std::size_t nu = 0, nv = 1;
    std::vector<R> u, v;
    std::vector<H> cp;
};
Failure Error(const char* key, Status status = Status::Indeterminate)
{
    return { status, key, "Exact terminal-spline evidence is unestablished", {}, "TerminalSpline" };
}
#define TS_TRY(name, expression)                                                                                                                     \
    auto name##Result = (expression);                                                                                                                \
    if (!name##Result)                                                                                                                               \
        return tl::unexpected(std::move(name##Result.error()));                                                                                      \
    auto name = std::move(*name##Result)
#define TS_OK(expression)                                                                                                                            \
    do                                                                                                                                               \
    {                                                                                                                                                \
        auto operation = (expression);                                                                                                               \
        if (!operation)                                                                                                                              \
            return tl::unexpected(std::move(operation.error()));                                                                                     \
    } while (false)
int Compare(const R& a, const R& b)
{
    const Big l = a.n * b.d, r = b.n * a.d;
    return l < r ? -1 : l > r ? 1 : 0;
}
R Negate(R a)
{
    a.n = -a.n;
    return a;
}
R Absolute(R a)
{
    if (a.n < 0)
    {
        a.n = -a.n;
    }
    return a;
}
class Arithmetic
{
public:

    const Limits& limits;
    const Charge& charge;
    bool callbackActive = false;
    std::size_t slots = 0;
    Arithmetic(const Limits& l, const Charge& c) : limits(l), charge(c)
    {
    }
    Result<void> Spend(std::size_t units = 1)
    {
        // If a caller actually throws overflow_error, the outer guard must rethrow it
        // rather than misclassify it as checked-integer capacity.
        callbackActive = true;
        auto result = charge(units);
        callbackActive = false;
        return result;
    }
    Result<void> Ready()
    {
        if (!charge || !limits.controlSlots || !limits.integerBits || limits.integerBits > 4096 || !limits.tokenCharacters)
        {
            return tl::unexpected(Error("TerminalSplineCapacityConfiguration", Status::ResourceLimit));
        }
        return Spend();
    }
    Result<void> Reserve(std::size_t count)
    {
        if (count > limits.controlSlots - slots || count > std::vector<H>().max_size())
        {
            return tl::unexpected(Error("TerminalSplineControlCapacity", Status::ResourceLimit));
        }
        TS_OK(Spend(count ? count : 1));
        slots += count;
        return {};
    }
    Result<R> Normalize(Big n, Big d)
    {
        TS_OK(Spend());
        if (d == 0)
        {
            return tl::unexpected(Error("TerminalSplineExactDivision"));
        }
        if (n == 0)
        {
            return R{};
        }
        if (d < 0)
        {
            n = -n;
            d = -d;
        }
        Big x = n < 0 ? -n : n, y = d;
        while (y != 0)
        {
            TS_OK(Spend());
            Big r = x % y;
            x = std::move(y);
            y = std::move(r);
        }
        n /= x;
        d /= x;
        const Big magnitude = n < 0 ? -n : n;
        if (boost::multiprecision::msb(magnitude) >= limits.integerBits || boost::multiprecision::msb(d) >= limits.integerBits)
        {
            return tl::unexpected(Error("TerminalSplineIntegerCapacity", Status::ResourceLimit));
        }
        return R{ std::move(n), std::move(d) };
    }
    Result<R> Add(const R& a, const R& b)
    {
        TS_OK(Spend());
        return Normalize(a.n * b.d + b.n * a.d, a.d * b.d);
    }
    Result<R> Sub(const R& a, const R& b)
    {
        TS_OK(Spend());
        return Normalize(a.n * b.d - b.n * a.d, a.d * b.d);
    }
    Result<R> Mul(const R& a, const R& b)
    {
        TS_OK(Spend());
        return Normalize(a.n * b.n, a.d * b.d);
    }
    Result<R> Div(const R& a, const R& b)
    {
        TS_OK(Spend());
        return Normalize(a.n * b.d, a.d * b.n);
    }
    Result<R> Decimal(std::string_view text)
    {
        TS_OK(Spend());
        if (text.empty() || text.size() > limits.tokenCharacters)
        {
            return tl::unexpected(Error("TerminalSplineDecimalCapacity", Status::ResourceLimit));
        }
        bool negative = false;
        if (text.front() == '-' || text.front() == '+')
        {
            negative = text.front() == '-';
            text.remove_prefix(1);
        }
        const auto at = text.find_first_of("Ee");
        const auto mantissa = text.substr(0, at);
        std::int64_t exponent = 0;
        if (at != std::string_view::npos)
        {
            auto power = text.substr(at + 1);
            if (!power.empty() && power.front() == '+')
            {
                power.remove_prefix(1);
            }
            const auto parsed = std::from_chars(power.data(), power.data() + power.size(), exponent);
            if (power.empty() || parsed.ec != std::errc{} || parsed.ptr != power.data() + power.size() || exponent < -2466 || exponent > 2466)
            {
                return tl::unexpected(Error("TerminalSplineDecimalExponent", Status::ResourceLimit));
            }
        }
        Big n = 0, d = 1;
        bool dot = false, digit = false;
        for (char c : mantissa)
        {
            TS_OK(Spend());
            if (c == '.' && !dot)
            {
                dot = true;
                continue;
            }
            if (c < '0' || c > '9')
            {
                return tl::unexpected(Error("TerminalSplineDecimalSyntax"));
            }
            digit = true;
            n *= 10;
            n += c - '0';
            if (dot)
            {
                --exponent;
            }
        }
        if (!digit)
        {
            return tl::unexpected(Error("TerminalSplineDecimalSyntax"));
        }
        if (n == 0)
        {
            return R{};
        }
        if (exponent < -2466 || exponent > 2466)
        {
            return tl::unexpected(Error("TerminalSplineDecimalExponent", Status::ResourceLimit));
        }
        Big& expanded = exponent < 0 ? d : n;
        for (std::int64_t i = 0, count = exponent < 0 ? -exponent : exponent; i < count; ++i)
        {
            TS_OK(Spend());
            expanded *= 10;
        }
        if (negative)
        {
            n = -n;
        }
        return Normalize(std::move(n), std::move(d));
    }
    Result<R> Ratio(const IntegerRatio& text)
    {
        TS_TRY(n, Decimal(text.numerator));
        TS_TRY(d, Decimal(text.denominator));
        if (n.d != 1 || d.d != 1 || n.n <= 0 || d.n <= 0)
        {
            return tl::unexpected(Error("TerminalSplinePositiveIntegerRatio"));
        }
        return Normalize(std::move(n.n), std::move(d.n));
    }
    Result<R> Binary(double value)
    {
        TS_OK(Spend());
        const auto bits = std::bit_cast<std::uint64_t>(value);
        const auto exponent = unsigned((bits >> 52) & 2047);
        const auto significand = (bits & 0xfffffffffffffULL) | (exponent ? 0x10000000000000ULL : 0);
        if (exponent == 2047)
        {
            return tl::unexpected(Error("TerminalSplineBinaryRange"));
        }
        if (!significand)
        {
            return R{};
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
    Result<double> Rounded(const R& value)
    {
        TS_OK(Spend());
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
            return tl::unexpected(Error("TerminalSplineBinaryRange"));
        }
        const std::uint64_t sign = value.n < 0 ? 0x8000000000000000ULL : 0;
        if (e < -1075)
        {
            return std::bit_cast<double>(sign);
        }
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
            return tl::unexpected(Error("TerminalSplineBinaryRange"));
        }
        return std::bit_cast<double>(sign | (std::uint64_t(e + 1023) << 52) | (q.convert_to<std::uint64_t>() - 0x10000000000000ULL));
    }
    Result<double> Outward(const R& value, bool upper)
    {
        TS_TRY(result, Rounded(value));
        TS_TRY(exact, Binary(result));
        const int c = Compare(exact, value);
        if ((upper && c < 0) || (!upper && c > 0))
        {
            auto bits = std::bit_cast<std::uint64_t>(result);
            if ((bits & 0x7fffffffffffffffULL) == 0)
            {
                bits = upper ? 1 : 0x8000000000000001ULL;
            }
            else if (bits >> 63)
            {
                bits += upper ? std::uint64_t(-1) : 1;
            }
            else
            {
                bits += upper ? 1 : std::uint64_t(-1);
            }
            result = std::bit_cast<double>(bits);
            TS_TRY(checked, Binary(result));
            (void)checked;
        }
        return result;
    }
};
Result<std::size_t> Product(std::size_t a, std::size_t b, Arithmetic& math)
{
    if (!a || !b || a > math.limits.controlSlots / b)
    {
        return tl::unexpected(Error("TerminalSplineControlCapacity", Status::ResourceLimit));
    }
    return a * b;
}
} // namespace
class Reference
{
public:

    Net net;
    // ReadSource captures these exact bounds before its ordinary outward
    // computational envelope is extended. Only an unrequested source curve
    // can authorize a source-domain restriction, never an interpreted domain.
    std::optional<std::array<R, 2>> originalCurveDomain;
};
namespace
{

Result<void> ValidateKnots(const std::vector<R>& knots, unsigned degree, std::size_t count, Arithmetic& math, bool requireClamped = true)
{
    if (count < degree + 1 || count > std::numeric_limits<std::size_t>::max() - degree - 1 || knots.size() != count + degree + 1)
    {
        return tl::unexpected(Error("TerminalSplineKnotShape", Status::Unsupported));
    }
    if (Compare(knots[degree], knots[count]) >= 0)
    {
        return tl::unexpected(Error("TerminalSplineEmptyDomain", Status::Unsupported));
    }
    std::size_t i = 0;
    while (i < knots.size())
    {
        TS_OK(math.Spend());
        std::size_t j = i + 1;
        while (j < knots.size() && Compare(knots[i], knots[j]) == 0)
        {
            ++j;
        }
        const bool badClamped = (i == 0 || j == knots.size()) ? j - i != degree + 1 : j - i > degree;
        bool invalid = badClamped;
        if (!requireClamped)
        {
            const bool inside = Compare(knots[i], knots[degree]) > 0 && Compare(knots[i], knots[count]) < 0;
            invalid = j - i > (inside ? degree : degree + 1);
        }
        if (invalid)
        {
            return tl::unexpected(Error("TerminalSplineClampedContinuousBasis", Status::Unsupported));
        }
        if (j < knots.size() && Compare(knots[i], knots[j]) >= 0)
        {
            return tl::unexpected(Error("TerminalSplineKnotOrder", Status::Unsupported));
        }
        i = j;
    }
    return {};
}
Result<std::vector<R>> SourceKnots(const SourceAxis& axis, std::size_t count, Arithmetic& math, bool requireClamped = true)
{
    if (axis.degree < 1 || axis.degree > 17 || axis.knots.size() < 2 || axis.knots.size() != axis.multiplicities.size() ||
        count > math.limits.controlSlots)
    {
        return tl::unexpected(Error("TerminalSplineSourceAxis", Status::Unsupported));
    }
    std::vector<R> result;
    for (std::size_t i = 0; i < axis.knots.size(); ++i)
    {
        TS_TRY(knot, math.Decimal(axis.knots[i]));
        const auto mult = axis.multiplicities[i];
        if (!mult || mult > axis.degree + 1 || result.size() > count + axis.degree + 1 || mult > count + axis.degree + 1 - result.size())
        {
            return tl::unexpected(Error("TerminalSplineSourceMultiplicity", Status::Unsupported));
        }
        result.insert(result.end(), mult, knot);
    }
    TS_OK(ValidateKnots(result, axis.degree, count, math, requireClamped));
    return result;
}
H& At(Net& net, bool uAxis, std::size_t index, std::size_t row)
{
    return net.cp[uAxis ? index * net.nv + row : row * net.nv + index];
}
const H& At(const Net& net, bool uAxis, std::size_t index, std::size_t row)
{
    return net.cp[uAxis ? index * net.nv + row : row * net.nv + index];
}
Result<H> Mix(const H& a, const H& b, const R& alpha, const R& other, Arithmetic& math)
{
    H out;
    for (std::size_t i = 0; i < 4; ++i)
    {
        TS_TRY(left, math.Mul(a[i], other));
        TS_TRY(right, math.Mul(b[i], alpha));
        TS_TRY(sum, math.Add(left, right));
        out[i] = std::move(sum);
    }
    return out;
}
Result<void> Insert(Net& net, bool uAxis, const R& value, Arithmetic& math)
{
    auto& knots = uAxis ? net.u : net.v;
    const unsigned p = uAxis ? net.p : net.q;
    const auto upper = std::upper_bound(
        knots.begin(),
        knots.end(),
        value,
        [](const R& a, const R& b)
        {
            return Compare(a, b) < 0;
        }
    );
    if (upper == knots.begin())
    {
        return tl::unexpected(Error("TerminalSplineInsertionDomain"));
    }
    const std::size_t k = std::size_t(upper - knots.begin() - 1);
    const std::size_t s = std::size_t(
        std::count_if(
            knots.begin(),
            knots.end(),
            [&](const R& x)
            {
                return Compare(x, value) == 0;
            }
        )
    );
    const auto count = uAxis ? net.nu : net.nv, rows = uAxis ? net.nv : net.nu;
    if (s >= p || k < p || k < s || k - s >= count)
    {
        return tl::unexpected(Error("TerminalSplineInsertionMultiplicity"));
    }
    const auto nu = net.nu + (uAxis ? 1 : 0), nv = net.nv + (uAxis ? 0 : 1);
    TS_TRY(size, Product(nu, nv, math));
    TS_OK(math.Reserve(size));
    std::vector<H> result(size);
    const auto output = [&](std::size_t i, std::size_t row) -> H&
    {
        return result[uAxis ? i * nv + row : row * nv + i];
    };
    for (std::size_t row = 0; row < rows; ++row)
    {
        TS_OK(math.Spend());
        for (std::size_t i = 0; i <= k - p; ++i)
        {
            output(i, row) = At(net, uAxis, i, row);
        }
        for (std::size_t i = k - s; i < count; ++i)
        {
            output(i + 1, row) = At(net, uAxis, i, row);
        }
        for (std::size_t i = k - p + 1; i <= k - s; ++i)
        {
            TS_TRY(numerator, math.Sub(value, knots[i]));
            TS_TRY(denominator, math.Sub(knots[i + p], knots[i]));
            TS_TRY(alpha, math.Div(numerator, denominator));
            TS_TRY(other, math.Sub(R{ 1, 1 }, alpha));
            TS_TRY(point, Mix(At(net, uAxis, i - 1, row), At(net, uAxis, i, row), alpha, other, math));
            output(i, row) = std::move(point);
        }
    }
    knots.insert(knots.begin() + std::ptrdiff_t(k + 1), value);
    net.cp = std::move(result);
    net.nu = nu;
    net.nv = nv;
    return {};
}
Result<void> Reverse(Net& net, bool uAxis, Arithmetic& math)
{
    auto& knots = uAxis ? net.u : net.v;
    std::reverse(knots.begin(), knots.end());
    for (auto& k : knots)
    {
        k = Negate(std::move(k));
    }
    const auto count = uAxis ? net.nu : net.nv, rows = uAxis ? net.nv : net.nu;
    for (std::size_t row = 0; row < rows; ++row)
    {
        TS_OK(math.Spend());
        for (std::size_t i = 0; i < count / 2; ++i)
        {
            std::swap(At(net, uAxis, i, row), At(net, uAxis, count - 1 - i, row));
        }
    }
    return {};
}
Result<void> Right(Net& net, bool uAxis, const R& newEnd, Arithmetic& math)
{
    auto& knots = uAxis ? net.u : net.v;
    const unsigned p = uAxis ? net.p : net.q;
    const R end = knots[uAxis ? net.nu : net.nv];
    if (Compare(newEnd, end) == 0)
    {
        return {};
    }
    if (Compare(newEnd, end) < 0)
    {
        return tl::unexpected(Error("TerminalSplineCannotTrim", Status::Unsupported));
    }
    auto i = knots.size();
    while (i && Compare(knots[i - 1], end) == 0)
    {
        --i;
    }
    if (!i)
    {
        return tl::unexpected(Error("TerminalSplineTerminalSpan"));
    }
    const R start = knots[i - 1];
    // Boehm isolates the last positive span before changing its Bezier polygon.
    // Changing the last original poles without this step can alter earlier spans.
    while (std::size_t(
               std::count_if(
                   knots.begin(),
                   knots.end(),
                   [&](const R& x)
                   {
                       return Compare(x, start) == 0;
                   }
               )
           ) < p)
    {
        TS_OK(Insert(net, uAxis, start, math));
    }
    TS_TRY(length, math.Sub(end, start));
    TS_TRY(expanded, math.Sub(newEnd, start));
    TS_TRY(alpha, math.Div(expanded, length));
    TS_TRY(other, math.Sub(R{ 1, 1 }, alpha));
    const auto count = uAxis ? net.nu : net.nv, rows = uAxis ? net.nv : net.nu;
    for (std::size_t row = 0; row < rows; ++row)
    {
        TS_OK(math.Reserve(p + 1));
        std::vector<H> work(p + 1);
        for (unsigned j = 0; j <= p; ++j)
        {
            work[j] = At(net, uAxis, count - p - 1 + j, row);
        }
        for (unsigned level = 1; level <= p; ++level)
        {
            for (unsigned j = 0; j <= p - level; ++j)
            {
                TS_TRY(point, Mix(work[j], work[j + 1], alpha, other, math));
                work[j] = std::move(point);
            }
            At(net, uAxis, count - p - 1 + level, row) = work[0];
        }
    }
    for (auto& knot : knots)
    {
        if (Compare(knot, end) == 0)
        {
            knot = newEnd;
        }
    }
    return {};
}
Result<void> ExtendRange(Net& net, bool uAxis, const std::array<R, 2>& ends, Arithmetic& math)
{
    const auto& k = uAxis ? net.u : net.v;
    const unsigned p = uAxis ? net.p : net.q;
    const auto count = uAxis ? net.nu : net.nv;
    if (Compare(ends[0], k[p]) > 0 || Compare(ends[1], k[count]) < 0)
    {
        return tl::unexpected(Error("TerminalSplineCannotTrim", Status::Unsupported));
    }
    if (Compare(ends[0], k[p]) < 0)
    {
        TS_OK(Reverse(net, uAxis, math));
        TS_OK(Right(net, uAxis, Negate(ends[0]), math));
        TS_OK(Reverse(net, uAxis, math));
    }
    return Right(net, uAxis, ends[1], math);
}
Result<std::array<R, 2>> Requested(const SourceAxis& source, const std::vector<R>& knots, std::size_t count, Arithmetic& math)
{
    std::array<R, 2> range{ knots[source.degree], knots[count] };
    if (source.requested)
    {
        TS_TRY(low, math.Decimal((*source.requested)[0]));
        TS_TRY(high, math.Decimal((*source.requested)[1]));
        if (Compare(low, range[0]) > 0 || Compare(high, range[1]) < 0)
        {
            return tl::unexpected(Error("TerminalSplineCannotTrim", Status::Unsupported));
        }
        range = { std::move(low), std::move(high) };
    }
    TS_TRY(low, math.Outward(range[0], false));
    TS_TRY(high, math.Outward(range[1], true));
    TS_TRY(a, math.Binary(low));
    TS_TRY(b, math.Binary(high));
    return std::array<R, 2>{ std::move(a), std::move(b) };
}
Result<Net> ReadOriginalSourceNet(
    const Source& source,
    Arithmetic& math,
    std::optional<std::array<R, 2>>& originalCurveDomain,
    bool requireClamped = true
)
{
    if (!source.v && source.countV != 1)
    {
        return tl::unexpected(Error("TerminalSplineCurveShape", Status::Unsupported));
    }
    TS_TRY(count, Product(source.countU, source.countV, math));
    if (count != source.controls.size() || (!source.weights.empty() && source.weights.size() != count))
    {
        return tl::unexpected(Error("TerminalSplineControlShape", Status::Unsupported));
    }
    Net net;
    net.p = source.u.degree;
    net.q = source.v ? source.v->degree : 0;
    net.nu = source.countU;
    net.nv = source.countV;
    TS_TRY(u, SourceKnots(source.u, net.nu, math, requireClamped));
    net.u = std::move(u);
    if (!source.v && !source.u.requested)
    {
        originalCurveDomain = std::array<R, 2>{ net.u[net.p], net.u[net.nu] };
    }
    if (source.v)
    {
        TS_TRY(v, SourceKnots(*source.v, net.nv, math));
        net.v = std::move(v);
    }
    else
    {
        net.v = { R{}, R{ 1, 1 } };
    }
    TS_TRY(scale, math.Ratio(source.lengthToMm));
    TS_OK(math.Reserve(count));
    net.cp.resize(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        R weight{ 1, 1 };
        if (!source.weights.empty())
        {
            TS_TRY(w, math.Decimal(source.weights[i]));
            weight = std::move(w);
        }
        if (weight.n <= 0)
        {
            return tl::unexpected(Error("TerminalSplineSourceWeight", Status::Unsupported));
        }
        net.cp[i][3] = weight;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            TS_TRY(raw, math.Decimal(source.controls[i][axis]));
            TS_TRY(mm, math.Mul(raw, scale));
            TS_TRY(h, math.Mul(mm, weight));
            net.cp[i][axis] = std::move(h);
        }
    }
    return net;
}
Result<Net> NaturalCurveNet(const Net& original, Arithmetic& math)
{
    if (original.q || original.nv != 1)
    {
        return tl::unexpected(Error("TerminalSplineNaturalCurveOnly", Status::Unsupported));
    }
    TS_OK(math.Reserve(original.cp.size()));
    TS_OK(math.Spend(original.cp.size()));
    Net work = original;
    const R low = original.u[original.p], high = original.u[original.nu];
    if (Compare(original.u.front(), low) == 0 && Compare(original.u.back(), high) == 0)
    {
        return work;
    }
    // Exact Boehm insertion only at the original natural endpoints restricts
    // the homogeneous curve without fitting, extending U, or altering interior
    // knots. Keeping this compact basis avoids expanding every span twice in
    // the subsequent common-refinement certificate. STEP's original data stays
    // immutable; this is a private exact reference, never emitted native data.
    // Unlike Insert's interior-only contract, an endpoint may reach p+1 copies.
    // At the high endpoint, upper_bound can select k==nu (or later duplicates):
    // capping k at nu-1 would change the final polynomial on unclamped inputs.
    const R one{ 1, 1 };
    for (const auto& endpoint : { low, high })
    {
        for (;;)
        {
            TS_OK(math.Spend(work.u.size()));
            const auto upper = std::upper_bound(
                work.u.begin(),
                work.u.end(),
                endpoint,
                [](const R& a, const R& b)
                {
                    return Compare(a, b) < 0;
                }
            );
            const auto lower = std::lower_bound(
                work.u.begin(),
                work.u.end(),
                endpoint,
                [](const R& a, const R& b)
                {
                    return Compare(a, b) < 0;
                }
            );
            const auto s = std::size_t(upper - lower);
            if (s == work.p + 1)
            {
                break;
            }
            const auto k = std::size_t(upper - work.u.begin() - 1);
            if (s > work.p || k < work.p || k < s || k - s >= work.nu || k - work.p >= work.nu)
            {
                return tl::unexpected(Error("TerminalSplineNaturalEndpointInsertion"));
            }
            TS_OK(math.Reserve(work.nu + 1));
            std::vector<H> next(work.nu + 1);
            for (std::size_t i = 0; i <= k - work.p; ++i)
            {
                TS_OK(math.Spend());
                next[i] = work.cp[i];
            }
            for (std::size_t i = k - s; i < work.nu; ++i)
            {
                TS_OK(math.Spend());
                next[i + 1] = work.cp[i];
            }
            for (std::size_t i = k - work.p + 1; i <= k - s; ++i)
            {
                TS_TRY(numerator, math.Sub(endpoint, work.u[i]));
                TS_TRY(denominator, math.Sub(work.u[i + work.p], work.u[i]));
                if (denominator.n <= 0)
                {
                    return tl::unexpected(Error("TerminalSplineNaturalInsertionDomain"));
                }
                TS_TRY(alpha, math.Div(numerator, denominator));
                if (alpha.n < 0 || Compare(alpha, one) > 0)
                {
                    return tl::unexpected(Error("TerminalSplineNaturalInsertionRange"));
                }
                TS_TRY(other, math.Sub(one, alpha));
                TS_TRY(point, Mix(work.cp[i - 1], work.cp[i], alpha, other, math));
                next[i] = std::move(point);
            }
            TS_OK(math.Spend(work.u.size() - k));
            work.u.insert(work.u.begin() + std::ptrdiff_t(k + 1), endpoint);
            work.cp = std::move(next);
            ++work.nu;
        }
    }
    const auto begin = std::lower_bound(
        work.u.begin(),
        work.u.end(),
        low,
        [](const R& a, const R& b)
        {
            return Compare(a, b) < 0;
        }
    );
    const auto end = std::upper_bound(
        work.u.begin(),
        work.u.end(),
        high,
        [](const R& a, const R& b)
        {
            return Compare(a, b) < 0;
        }
    );
    const auto offset = std::size_t(begin - work.u.begin());
    const auto count = std::size_t(end - begin) - work.p - 1;
    if (offset > work.nu || count > work.nu - offset)
    {
        return tl::unexpected(Error("TerminalSplineNaturalControlCount"));
    }
    TS_OK(math.Reserve(count));
    TS_OK(math.Spend(count + std::size_t(end - begin)));
    Net out;
    out.p = work.p;
    out.nu = count;
    out.v = work.v;
    out.u.assign(begin, end);
    out.cp.assign(work.cp.begin() + std::ptrdiff_t(offset), work.cp.begin() + std::ptrdiff_t(offset + count));
    TS_OK(ValidateKnots(out.u, out.p, out.nu, math));
    return out;
}
Result<Net> ReadSource(const Source& source, Arithmetic& math, std::optional<std::array<R, 2>>& originalCurveDomain)
{
    // Share the original exact reading without introducing a second parser.
    // Keep the established extension/constructor path and charge sequence:
    // original net, requested outward envelope, terminal extension, Emit.
    TS_TRY(net, ReadOriginalSourceNet(source, math, originalCurveDomain));
    TS_TRY(ur, Requested(source.u, net.u, net.nu, math));
    TS_OK(ExtendRange(net, true, ur, math));
    if (source.v)
    {
        TS_TRY(vr, Requested(*source.v, net.v, net.nv, math));
        TS_OK(ExtendRange(net, false, vr, math));
    }
    for (const auto& point : net.cp)
    {
        if (point[3].n <= 0)
        {
            return tl::unexpected(Error("TerminalSplineExtendedWeightUnestablished"));
        }
    }
    return net;
}
Result<std::vector<double>> RoundKnots(const std::vector<R>& knots, Arithmetic& math)
{
    std::vector<double> out;
    R previous;
    bool first = true;
    for (const auto& value : knots)
    {
        TS_TRY(rounded, math.Rounded(value));
        TS_TRY(current, math.Binary(rounded));
        if (!first && Compare(previous, current) == 0 && Compare(value, knots[out.size() - 1]) != 0)
        {
            return tl::unexpected(Error("TerminalSplineRoundedKnotCollision"));
        }
        out.push_back(rounded);
        previous = std::move(current);
        first = false;
    }
    return out;
}
Result<Canonical> Emit(const Net& net, Arithmetic& math)
{
    Canonical out;
    out.basis = { net.p, net.q, net.nu, net.nv, {}, {} };
    TS_TRY(u, RoundKnots(net.u, math));
    out.basis.knotsU = std::move(u);
    if (net.q)
    {
        TS_TRY(v, RoundKnots(net.v, math));
        out.basis.knotsV = std::move(v);
    }
    out.controlsMm.reserve(net.cp.size());
    out.weights.reserve(net.cp.size());
    for (const auto& point : net.cp)
    {
        TS_TRY(weight, math.Rounded(point[3]));
        TS_TRY(w, math.Binary(weight));
        if (w.n <= 0)
        {
            return tl::unexpected(Error("TerminalSplineRoundedWeightUnestablished"));
        }
        std::array<double, 3> p{};
        for (std::size_t j = 0; j < 3; ++j)
        {
            TS_TRY(value, math.Div(point[j], point[3]));
            TS_TRY(rounded, math.Rounded(value));
            p[j] = rounded;
        }
        out.controlsMm.push_back(p);
        out.weights.push_back(weight);
    }
    return out;
}
Result<Net> ReadBinary(const BinaryNet& input, Arithmetic& math)
{
    const auto& b = input.basis;
    if (b.degreeU < 1 || b.degreeU > 17 || b.degreeV > 17 || (!b.degreeV && b.countV != 1))
    {
        return tl::unexpected(Error("TerminalSplineNativeDegree", Status::Unsupported));
    }
    TS_TRY(count, Product(b.countU, b.countV, math));
    if (input.homogeneous.size() != count)
    {
        return tl::unexpected(Error("TerminalSplineNativeShape"));
    }
    if (b.knotsU.size() != b.countU + b.degreeU + 1 || (b.degreeV ? b.knotsV.size() != b.countV + b.degreeV + 1 : !b.knotsV.empty()))
    {
        return tl::unexpected(Error("TerminalSplineNativeKnotShape"));
    }
    Net out;
    out.p = b.degreeU;
    out.q = b.degreeV;
    out.nu = b.countU;
    out.nv = b.countV;
    for (double x : b.knotsU)
    {
        TS_TRY(k, math.Binary(x));
        out.u.push_back(std::move(k));
    }
    if (out.q)
    {
        for (double x : b.knotsV)
        {
            TS_TRY(k, math.Binary(x));
            out.v.push_back(std::move(k));
        }
    }
    else
    {
        if (!b.knotsV.empty())
        {
            return tl::unexpected(Error("TerminalSplineCurveKnots"));
        }
        out.v = { R{}, R{ 1, 1 } };
    }
    TS_OK(ValidateKnots(out.u, out.p, out.nu, math));
    TS_OK(ValidateKnots(out.v, out.q, out.nv, math));
    TS_OK(math.Reserve(count));
    out.cp.resize(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        for (std::size_t j = 0; j < 4; ++j)
        {
            TS_TRY(x, math.Binary(input.homogeneous[i][j]));
            out.cp[i][j] = std::move(x);
        }
        if (out.cp[i][3].n <= 0)
        {
            return tl::unexpected(Error("TerminalSplineNativeWeight"));
        }
    }
    return out;
}

Result<std::vector<R>> KnotUnion(const std::vector<R>& a, const std::vector<R>& b, Arithmetic& math)
{
    std::vector<R> out;
    std::size_t i = 0, j = 0;
    while (i < a.size() || j < b.size())
    {
        TS_OK(math.Spend());
        const bool fromA = j == b.size() || (i < a.size() && Compare(a[i], b[j]) <= 0);
        const auto& value = fromA ? a[i++] : b[j++];
        if (out.empty() || Compare(out.back(), value) != 0)
        {
            out.push_back(value);
        }
    }
    return out;
}
Result<void> RefineCurve(Net& net, const std::vector<R>& values, Arithmetic& math)
{
    // All callers supply nondecreasing common knots or ordered restriction
    // cuts. Merge that list with the ORIGINAL knot groups before allocating.
    // Counting each distinct cut once also preserves repeated-cut behavior.
    const auto originalCount = net.nu;
    auto finalCount = originalCount;
    auto knot = net.u.begin();
    const R* previous = nullptr;
    for (const auto& value : values)
    {
        TS_OK(math.Spend());
        if (previous && Compare(value, *previous) < 0)
        {
            return tl::unexpected(Error("TerminalSplineRefinementOrder"));
        }
        const bool repeated = previous && Compare(value, *previous) == 0;
        previous = &value;
        if (repeated || Compare(value, net.u[net.p]) <= 0 || Compare(value, net.u[originalCount]) >= 0)
        {
            continue;
        }
        while (knot != net.u.end() && Compare(*knot, value) < 0)
        {
            TS_OK(math.Spend());
            ++knot;
        }
        std::size_t multiplicity = 0;
        while (knot != net.u.end() && Compare(*knot, value) == 0)
        {
            TS_OK(math.Spend());
            ++knot;
            ++multiplicity;
        }
        if (multiplicity > net.p)
        {
            return tl::unexpected(Error("TerminalSplineInsertionMultiplicity"));
        }
        const auto missing = net.p - multiplicity;
        if (finalCount > math.limits.controlSlots || missing > math.limits.controlSlots - finalCount)
        {
            return tl::unexpected(Error("TerminalSplineControlCapacity", Status::ResourceLimit));
        }
        finalCount += missing;
    }
    if (finalCount == originalCount)
    {
        return {};
    }
    const auto added = finalCount - originalCount;
    if (added > net.u.max_size() - net.u.size())
    {
        return tl::unexpected(Error("TerminalSplineControlCapacity", Status::ResourceLimit));
    }
    // This is still a CUMULATIVE allocation ledger: charge the complete new
    // buffer, including the old net already counted by its reader/copy. Each
    // following insertion reuses it instead of allocating successively larger
    // full nets. Tensor refinement and terminal extension retain their path.
    TS_OK(math.Reserve(finalCount));
    std::vector<H> refined(finalCount);
    TS_OK(math.Spend(originalCount));
    std::move(net.cp.begin(), net.cp.end(), refined.begin());
    net.cp = std::move(refined);
    TS_OK(math.Spend(net.u.size() + added));
    net.u.reserve(net.u.size() + added);
    for (const auto& value : values)
    {
        TS_OK(math.Spend());
        if (Compare(value, net.u[net.p]) <= 0 || Compare(value, net.u[net.nu]) >= 0)
        {
            continue;
        }
        // Charge the search itself. Replacing full-net allocation must not
        // leave a hidden whole-knot scan outside the caller's work ledger.
        std::size_t first = 0, last = net.u.size();
        while (first < last)
        {
            TS_OK(math.Spend());
            const auto middle = first + (last - first) / 2;
            if (Compare(value, net.u[middle]) < 0)
            {
                last = middle;
            }
            else
            {
                first = middle + 1;
            }
        }
        if (!first)
        {
            return tl::unexpected(Error("TerminalSplineInsertionDomain"));
        }
        auto k = first - 1;
        std::size_t s = 0;
        for (auto at = first; at; --at)
        {
            TS_OK(math.Spend());
            if (Compare(net.u[at - 1], value) != 0)
            {
                break;
            }
            ++s;
        }
        while (s < net.p)
        {
            if (k < net.p || k < s || k - s >= net.nu || net.nu >= finalCount)
            {
                return tl::unexpected(Error("TerminalSplineInsertionMultiplicity"));
            }
            // Copy the tail BACKWARDS: moving P[k-s] would destroy an input of
            // its own blend. Then blend backwards so every P[i-1],P[i] is read
            // before either old pole is overwritten. This is the same exact
            // homogeneous Boehm formula as Insert, with no rounded operations.
            for (auto i = net.nu; i-- > k - s;)
            {
                TS_OK(math.Spend());
                net.cp[i + 1] = net.cp[i];
            }
            for (auto i = k - s; i > k - net.p; --i)
            {
                TS_TRY(numerator, math.Sub(value, net.u[i]));
                TS_TRY(denominator, math.Sub(net.u[i + net.p], net.u[i]));
                TS_TRY(alpha, math.Div(numerator, denominator));
                TS_TRY(other, math.Sub(R{ 1, 1 }, alpha));
                TS_TRY(point, Mix(net.cp[i - 1], net.cp[i], alpha, other, math));
                net.cp[i] = std::move(point);
            }
            TS_OK(math.Spend(net.u.size() - k)); // Shift existing knots and copy the new value.
            net.u.insert(net.u.begin() + std::ptrdiff_t(k + 1), value);
            ++net.nu;
            ++k;
            ++s;
        }
    }
    // These nets are request-private copies. Any failure or cancellation
    // above discards them; no immutable reference/actual input or published
    // candidate can observe a partially updated control net.
    if (net.nu != finalCount)
    {
        return tl::unexpected(Error("TerminalSplineRefinementCount"));
    }
    return {};
}
Result<void> Refine(Net& net, bool uAxis, const std::vector<R>& values, Arithmetic& math)
{
    if (uAxis && net.q == 0 && net.nv == 1)
    {
        return RefineCurve(net, values, math);
    }
    const auto& knots = uAxis ? net.u : net.v;
    const unsigned p = uAxis ? net.p : net.q;
    for (const auto& value : values)
    {
        TS_OK(math.Spend());
        const auto count = uAxis ? net.nu : net.nv;
        if (Compare(value, knots[p]) <= 0 || Compare(value, knots[count]) >= 0)
        {
            continue;
        }
        while (std::size_t(
                   std::count_if(
                       knots.begin(),
                       knots.end(),
                       [&](const R& x)
                       {
                           return Compare(x, value) == 0;
                       }
                   )
               ) < p)
        {
            TS_OK(Insert(net, uAxis, value, math));
        }
    }
    return {};
}
Result<std::size_t> Span(const std::vector<R>& knots, unsigned p, std::size_t count, const R& low, const R& high, Arithmetic& math)
{
    for (std::size_t i = p; i < count; ++i)
    {
        TS_OK(math.Spend());
        if (Compare(knots[i], low) == 0 && Compare(knots[i + 1], high) == 0)
        {
            return i;
        }
    }
    return tl::unexpected(Error("TerminalSplineCommonSpan"));
}
std::uint64_t Choose(unsigned n, unsigned k)
{
    k = std::min(k, n - k);
    std::uint64_t value = 1;
    for (unsigned i = 0; i < k; ++i)
    {
        value = value * (n - i) / (i + 1);
    }
    return value;
}
Result<R> Patch(const Net& a, const Net& b, std::size_t au, std::size_t av, std::size_t bu, std::size_t bv, Arithmetic& math)
{
    const unsigned p = a.p, q = a.q;
    const auto pole = [](const Net& n, std::size_t u, std::size_t v, unsigned i, unsigned j) -> const H&
    {
        return n.cp[(u - n.p + i) * n.nv + v - n.q + j];
    };
    R minA = pole(a, au, av, 0, 0)[3], minB = pole(b, bu, bv, 0, 0)[3];
    bool polynomial = true;
    for (unsigned i = 0; i <= p; ++i)
    {
        for (unsigned j = 0; j <= q; ++j)
        {
            const auto& x = pole(a, au, av, i, j);
            const auto& y = pole(b, bu, bv, i, j);
            if (Compare(x[3], minA) < 0)
            {
                minA = x[3];
            }
            if (Compare(y[3], minB) < 0)
            {
                minB = y[3];
            }
            polynomial = polynomial && x[3].n == x[3].d && y[3].n == y[3].d;
        }
    }
    if (minA.n <= 0 || minB.n <= 0)
    {
        return tl::unexpected(Error("TerminalSplineRefinedWeight"));
    }
    std::array<R, 3> maximum{};
    if (polynomial)
    {
        for (unsigned i = 0; i <= p; ++i)
        {
            for (unsigned j = 0; j <= q; ++j)
            {
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    TS_TRY(difference, math.Sub(pole(a, au, av, i, j)[axis], pole(b, bu, bv, i, j)[axis]));
                    difference = Absolute(std::move(difference));
                    if (Compare(difference, maximum[axis]) > 0)
                    {
                        maximum[axis] = std::move(difference);
                    }
                }
            }
        }
    }
    else
    {
        // Bernstein product coefficients on every common physical parameter patch.
        // This bounds the cross numerator everywhere, including all new strips and
        // tensor corners; positive control weights bound the denominator below.
        for (unsigned r = 0; r <= 2 * p; ++r)
        {
            for (unsigned s = 0; s <= 2 * q; ++s)
            {
                std::array<R, 3> sum{};
                for (unsigned i = r > p ? r - p : 0; i <= std::min(p, r); ++i)
                {
                    for (unsigned j = s > q ? s - q : 0; j <= std::min(q, s); ++j)
                    {
                        const unsigned k = r - i, l = s - j;
                        TS_TRY(cu, math.Normalize(Big(Choose(p, i)) * Choose(p, k), Big(Choose(2 * p, r))));
                        TS_TRY(cv, math.Normalize(Big(Choose(q, j)) * Choose(q, l), Big(Choose(2 * q, s))));
                        TS_TRY(coefficient, math.Mul(cu, cv));
                        const auto& x = pole(a, au, av, i, j);
                        const auto& y = pole(b, bu, bv, k, l);
                        for (std::size_t axis = 0; axis < 3; ++axis)
                        {
                            TS_TRY(left, math.Mul(x[axis], y[3]));
                            TS_TRY(right, math.Mul(y[axis], x[3]));
                            TS_TRY(cross, math.Sub(left, right));
                            TS_TRY(term, math.Mul(coefficient, cross));
                            TS_TRY(next, math.Add(sum[axis], term));
                            sum[axis] = std::move(next);
                        }
                    }
                }
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    auto magnitude = Absolute(std::move(sum[axis]));
                    if (Compare(magnitude, maximum[axis]) > 0)
                    {
                        maximum[axis] = std::move(magnitude);
                    }
                }
            }
        }
    }
    TS_TRY(xy, math.Add(maximum[0], maximum[1]));
    TS_TRY(total, math.Add(xy, maximum[2]));
    if (polynomial)
    {
        return total;
    }
    TS_TRY(denominator, math.Mul(minA, minB));
    return math.Div(total, denominator);
}
using Bernstein = std::vector<R>;
Result<Bernstein> MultiplyBernstein(const Bernstein& a, const Bernstein& b, Arithmetic& math)
{
    const auto p = unsigned(a.size() - 1), q = unsigned(b.size() - 1);
    TS_OK(math.Reserve(std::size_t(p) + q + 1));
    Bernstein result(std::size_t(p) + q + 1);
    for (unsigned r = 0; r <= p + q; ++r)
    {
        for (unsigned i = r > q ? r - q : 0; i <= std::min(p, r); ++i)
        {
            const unsigned j = r - i;
            TS_TRY(coefficient, math.Normalize(Big(Choose(p, i)) * Choose(q, j), Big(Choose(p + q, r))));
            TS_TRY(product, math.Mul(a[i], b[j]));
            TS_TRY(term, math.Mul(coefficient, product));
            TS_TRY(sum, math.Add(result[r], term));
            result[r] = std::move(sum);
        }
    }
    return result;
}
Result<Bernstein> DifferentiateBernstein(const Bernstein& a, Arithmetic& math)
{
    const auto degree = a.size() - 1;
    TS_OK(math.Reserve(degree ? degree : 1));
    Bernstein result(degree ? degree : 1);
    for (std::size_t i = 0; i < degree; ++i)
    {
        TS_TRY(difference, math.Sub(a[i + 1], a[i]));
        TS_TRY(derivative, math.Mul(difference, R{ Big(degree), 1 }));
        result[i] = std::move(derivative);
    }
    return result;
}
struct PolePatchBounds
{
    bool outside = false, inside = false, increasing = false, decreasing = false;
    int first = 0, last = 0; // Signs of exact squared-distance-minus-radius at the span endpoints.
};
Result<PolePatchBounds> BoundPolePatch(const std::vector<H>& cp, const std::array<R, 3>& pole, const R& radiusSquared, Arithmetic& math)
{
    const auto count = cp.size();
    TS_OK(math.Reserve(count * 4));
    std::array<Bernstein, 3> d;
    Bernstein w(count);
    for (auto& coordinate : d)
    {
        coordinate.resize(count);
    }
    bool constantWeight = true;
    for (std::size_t i = 0; i < count; ++i)
    {
        TS_OK(math.Spend());
        if (cp[i][3].n <= 0)
        {
            return tl::unexpected(Error("TerminalSplinePoleWeight"));
        }
        w[i] = cp[i][3];
        constantWeight = constantWeight && Compare(w[i], w.front()) == 0;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            TS_TRY(center, math.Mul(pole[axis], w[i]));
            TS_TRY(difference, math.Sub(cp[i][axis], center));
            d[axis][i] = std::move(difference);
        }
    }
    TS_OK(math.Reserve(2 * count - 1));
    Bernstein q(2 * count - 1);
    for (const auto& coordinate : d)
    {
        TS_TRY(square, MultiplyBernstein(coordinate, coordinate, math));
        for (std::size_t i = 0; i < q.size(); ++i)
        {
            TS_TRY(sum, math.Add(q[i], square[i]));
            q[i] = std::move(sum);
        }
    }
    TS_TRY(wSquared, MultiplyBernstein(w, w, math));
    TS_OK(math.Reserve(q.size()));
    Bernstein f(q.size());
    PolePatchBounds bounds;
    bounds.outside = bounds.inside = true;
    for (std::size_t i = 0; i < f.size(); ++i)
    {
        TS_TRY(ball, math.Mul(radiusSquared, wSquared[i]));
        TS_TRY(difference, math.Sub(q[i], ball));
        f[i] = std::move(difference);
        // The ball is CLOSED. Nonnegative coefficients do not exclude an
        // isolated contact and therefore cannot establish an outside region.
        bounds.outside = bounds.outside && f[i].n > 0;
        bounds.inside = bounds.inside && f[i].n <= 0;
    }
    const auto sign = [](const R& x)
    {
        return x.n < 0 ? -1 : x.n > 0 ? 1 : 0;
    };
    bounds.first = sign(f.front());
    bounds.last = sign(f.back());
    if (bounds.outside || bounds.inside)
    {
        return bounds;
    }
    TS_TRY(qDerivative, DifferentiateBernstein(q, math));
    Bernstein derivative;
    if (constantWeight)
    {
        derivative = std::move(qDerivative); // Positive constant W cannot change the derivative's sign.
    }
    else
    {
        // C-P=D/W, distance^2=Q/W^2, Q=sum(D_axis^2). Since W>0,
        // its derivative has the sign of Q'*W-2*Q*W'. Derivatives here use
        // each span's increasing [0,1] parameter; positive physical t scaling
        // cannot change the sign. Degree <=3*17-1=50 keeps Choose in uint64.
        TS_TRY(wDerivative, DifferentiateBernstein(w, math));
        TS_TRY(left, MultiplyBernstein(qDerivative, w, math));
        TS_TRY(right, MultiplyBernstein(q, wDerivative, math));
        derivative = std::move(left);
        for (std::size_t i = 0; i < derivative.size(); ++i)
        {
            TS_TRY(twice, math.Mul(R{ 2, 1 }, right[i]));
            TS_TRY(difference, math.Sub(derivative[i], twice));
            derivative[i] = std::move(difference);
        }
    }
    TS_OK(math.Spend(derivative.size()));
    bounds.increasing = std::all_of(
        derivative.begin(),
        derivative.end(),
        [](const R& x)
        {
            return x.n >= 0;
        }
    );
    bounds.decreasing = std::all_of(
        derivative.begin(),
        derivative.end(),
        [](const R& x)
        {
            return x.n <= 0;
        }
    );
    // Monotonicity plus its MINIMUM endpoint is also a whole-span outside
    // certificate, even when the F control hull is inconclusive.
    bounds.outside = (bounds.increasing && bounds.first > 0) || (bounds.decreasing && bounds.last > 0);
    return bounds;
}
enum class PolePhase
{
    First,
    Outside,
    Last
};
struct PoleState
{
    PolePhase phase = PolePhase::Outside;
    bool outsideSeen = false;
    PoleVisitProof proof;
};
Result<bool> VisitPolePatch(
    const std::vector<H>& cp,
    const std::array<R, 3>& pole,
    const R& radiusSquared,
    const PoleVisits& visits,
    PoleState& state,
    unsigned remainingDepth,
    Arithmetic& math
)
{
    TS_OK(math.Spend());
    TS_TRY(bounds, BoundPolePatch(cp, pole, radiusSquared, math));
    ++state.proof.patches;
    if (bounds.outside)
    {
        if (state.phase == PolePhase::Last)
        {
            return false; // A previously entered suffix left again.
        }
        state.phase = PolePhase::Outside;
        state.outsideSeen = true;
        return true;
    }
    if (bounds.inside)
    {
        if (state.phase == PolePhase::Outside)
        {
            if (!visits.lastAllowed)
            {
                return false;
            }
            state.phase = PolePhase::Last;
        }
        return true;
    }
    if (state.phase == PolePhase::First && bounds.increasing)
    {
        state.outsideSeen = state.outsideSeen || bounds.last > 0;
        return true; // One monotone departure attached to the allowed first endpoint.
    }
    if (visits.lastAllowed && bounds.decreasing)
    {
        state.outsideSeen = state.outsideSeen || bounds.first > 0;
        state.phase = PolePhase::Last;
        return true; // One monotone arrival; no later outside piece is permitted.
    }
    // These endpoint values are exact, not samples used to accept a curve.
    // A single forbidden contact/outside witness suffices to refuse the proof.
    if ((state.phase == PolePhase::Outside && !visits.lastAllowed && (bounds.first <= 0 || bounds.last <= 0)) ||
        (state.phase == PolePhase::Last && (bounds.first > 0 || bounds.last > 0)) || !remainingDepth)
    {
        return false;
    }
    const auto count = cp.size();
    TS_OK(math.Reserve(3 * count));
    auto work = cp;
    std::vector<H> left(count), right(count);
    left.front() = cp.front();
    right.back() = cp.back();
    for (std::size_t level = 1; level < count; ++level)
    {
        for (std::size_t i = 0; i < count - level; ++i)
        {
            TS_TRY(midpoint, Mix(work[i], work[i + 1], R{ 1, 2 }, R{ 1, 2 }, math));
            work[i] = std::move(midpoint);
        }
        left[level] = work.front();
        right[count - level - 1] = work[count - level - 1];
    }
    // ValidateKnots admits only C0-or-better nets. Homogeneous de Casteljau
    // preserves that continuity and positive weights at every shared endpoint.
    // Processing every selected piece in order makes each inside component
    // connect to an allowed ORIGINAL endpoint, rather than a subdivision cut.
    TS_TRY(first, VisitPolePatch(left, pole, radiusSquared, visits, state, remainingDepth - 1, math));
    if (!first)
    {
        return false;
    }
    return VisitPolePatch(right, pole, radiusSquared, visits, state, remainingDepth - 1, math);
}

Result<void> RestrictCurve(Net& net, const R& low, const R& high, Arithmetic& math)
{
    if (Compare(low, net.u[net.p]) == 0 && Compare(high, net.u[net.nu]) == 0)
    {
        return {};
    }
    // Refine at both exact binary64 cuts to degree multiplicity. The selected
    // consecutive Bezier pieces then share their actual C0 endpoint poles.
    // Adding one endpoint knot produces a clamped subcurve in the SAME t;
    // no native snapping, rounded Euclidean conversion or reverse is involved.
    TS_OK(math.Reserve(2));
    const std::vector<R> cuts{ low, high };
    TS_OK(Refine(net, true, cuts, math));
    const auto afterLow = std::upper_bound(
        net.u.begin(),
        net.u.end(),
        low,
        [](const R& a, const R& b)
        {
            return Compare(a, b) < 0;
        }
    );
    const auto beforeHigh = std::lower_bound(
        net.u.begin(),
        net.u.end(),
        high,
        [](const R& a, const R& b)
        {
            return Compare(a, b) < 0;
        }
    );
    const auto lowOffset = std::size_t(afterLow - net.u.begin());
    const auto end = std::size_t(beforeHigh - net.u.begin());
    if (lowOffset <= net.p || end > net.nu)
    {
        return tl::unexpected(Error("TerminalSplineRestrictionSpan"));
    }
    const auto first = lowOffset - net.p - 1;
    if (end <= first || end - first < net.p + 1)
    {
        return tl::unexpected(Error("TerminalSplineRestrictionSpan"));
    }
    const auto count = end - first;
    TS_OK(math.Reserve(count));
    Net selected;
    selected.p = net.p;
    selected.nu = count;
    selected.v = net.v;
    selected.u.reserve(count + net.p + 1);
    for (unsigned i = 0; i <= net.p; ++i)
    {
        TS_OK(math.Spend());
        selected.u.push_back(low);
    }
    for (auto knot = afterLow; knot != beforeHigh; ++knot)
    {
        TS_OK(math.Spend());
        selected.u.push_back(*knot);
    }
    for (unsigned i = 0; i <= net.p; ++i)
    {
        TS_OK(math.Spend());
        selected.u.push_back(high);
    }
    selected.cp.reserve(count);
    for (std::size_t i = first; i < end; ++i)
    {
        TS_OK(math.Spend());
        selected.cp.push_back(net.cp[i]);
    }
    TS_OK(ValidateKnots(selected.u, selected.p, selected.nu, math));
    net = std::move(selected);
    return {};
}
Result<Proof> CertifyNets(Net& a, Net& b, const R& allowed, Arithmetic& math)
{
    // Both callers must arrive with exactly matching domains and degrees.
    // The ordinary Certify entry keeps its original whole-domain refusal;
    // restriction changes only its PRIVATE exact reference before this gate.
    if (a.p != b.p || a.q != b.q || Compare(a.u[a.p], b.u[b.p]) || Compare(a.u[a.nu], b.u[b.nu]) || Compare(a.v[a.q], b.v[b.q]) ||
        Compare(a.v[a.nv], b.v[b.nv]))
    {
        return tl::unexpected(Error("TerminalSplineActualDomainOrDegree"));
    }
    TS_TRY(u, KnotUnion(a.u, b.u, math));
    TS_TRY(v, KnotUnion(a.v, b.v, math));
    TS_OK(Refine(a, true, u, math));
    TS_OK(Refine(a, false, v, math));
    TS_OK(Refine(b, true, u, math));
    TS_OK(Refine(b, false, v, math));
    Proof proof;
    R maximum;
    for (std::size_t i = a.p; i < a.nu; ++i)
    {
        if (Compare(a.u[i], a.u[i + 1]) < 0)
        {
            TS_TRY(bu, Span(b.u, b.p, b.nu, a.u[i], a.u[i + 1], math));
            for (std::size_t j = a.q; j < a.nv; ++j)
            {
                if (Compare(a.v[j], a.v[j + 1]) < 0)
                {
                    TS_TRY(bv, Span(b.v, b.q, b.nv, a.v[j], a.v[j + 1], math));
                    TS_TRY(bound, Patch(a, b, i, j, bu, bv, math));
                    if (Compare(bound, maximum) > 0)
                    {
                        maximum = std::move(bound);
                    }
                    ++proof.patches;
                }
            }
        }
    }
    if (!proof.patches)
    {
        return tl::unexpected(Error("TerminalSplineNoPatches"));
    }
    TS_TRY(upper, math.Outward(maximum, true));
    proof.maximumMm = upper;
    proof.established = Compare(maximum, allowed) <= 0;
    return proof;
}
Result<void> MapCurveParameter(Net& net, const R& sourceLow, const R& sourceHigh, const R& targetLow, const R& targetHigh, Arithmetic& math)
{
    if (Compare(sourceLow, targetLow) == 0 && Compare(sourceHigh, targetHigh) == 0)
    {
        return {};
    }
    TS_TRY(sourceLength, math.Sub(sourceHigh, sourceLow));
    TS_TRY(targetLength, math.Sub(targetHigh, targetLow));
    for (auto& knot : net.u)
    {
        TS_TRY(offset, math.Sub(knot, sourceLow));
        TS_TRY(fraction, math.Div(offset, sourceLength));
        TS_TRY(mappedOffset, math.Mul(fraction, targetLength));
        TS_TRY(mapped, math.Add(targetLow, mappedOffset));
        knot = std::move(mapped);
    }
    return ValidateKnots(net.u, net.p, net.nu, math);
}
Result<H> EvaluateCurve(const Net& net, const R& parameter, Arithmetic& math)
{
    std::size_t span = net.nu - 1;
    if (Compare(parameter, net.u[net.nu]) < 0)
    {
        const auto after = std::upper_bound(
            net.u.begin(),
            net.u.end(),
            parameter,
            [](const R& a, const R& b)
            {
                return Compare(a, b) < 0;
            }
        );
        if (after == net.u.begin())
        {
            return tl::unexpected(Error("TerminalSplinePointParameter"));
        }
        span = std::size_t(after - net.u.begin() - 1);
    }
    if (span < net.p || span >= net.nu)
    {
        return tl::unexpected(Error("TerminalSplinePointParameter"));
    }
    TS_OK(math.Reserve(net.p + 1));
    std::vector<H> work(net.cp.begin() + std::ptrdiff_t(span - net.p), net.cp.begin() + std::ptrdiff_t(span + 1));
    // Exact homogeneous de Boor at ONE declared selector, including an
    // original endpoint or interior knot. This is not a sampled whole-curve
    // qualification; every scalar operation retains the caller's exact ledger.
    for (unsigned level = 1; level <= net.p; ++level)
    {
        for (unsigned j = net.p; j >= level; --j)
        {
            const auto index = span - net.p + j;
            TS_TRY(offset, math.Sub(parameter, net.u[index]));
            TS_TRY(length, math.Sub(net.u[index + net.p - level + 1], net.u[index]));
            TS_TRY(alpha, math.Div(offset, length));
            TS_TRY(other, math.Sub(R{ 1, 1 }, alpha));
            TS_TRY(point, Mix(work[j - 1], work[j], alpha, other, math));
            work[j] = std::move(point);
        }
    }
    return work[net.p];
}
template <class T, class F>
Result<T> Guard(const Limits& limits, const Charge& charge, F&& body)
{
    Arithmetic math(limits, charge);
    try
    {
        TS_OK(math.Ready());
        return body(math);
    }
    catch (const std::overflow_error&)
    {
        if (math.callbackActive)
        {
            throw; // A real caller exception retains its meaning.
        }
        return tl::unexpected(Error("TerminalSplineIntegerCapacity", Status::ResourceLimit));
    }
}
} // namespace

Result<Prepared> Extend(const Source& source, const Limits& limits, const Charge& charge)
{
    return Guard<Prepared>(
        limits,
        charge,
        [&](Arithmetic& math) -> Result<Prepared>
        {
            std::optional<std::array<R, 2>> originalCurveDomain;
            TS_TRY(net, ReadSource(source, math, originalCurveDomain));
            TS_TRY(target, Emit(net, math));
            auto reference = std::make_shared<Reference>();
            reference->net = std::move(net);
            reference->originalCurveDomain = std::move(originalCurveDomain);
            return Prepared{ std::move(target), std::move(reference) };
        }
    );
}
Result<std::shared_ptr<const Reference>> CaptureOriginalReference(const Source& source, const Limits& limits, const Charge& charge)
{
    return Guard<std::shared_ptr<const Reference>>(
        limits,
        charge,
        [&](Arithmetic& math) -> Result<std::shared_ptr<const Reference>>
        {
            if (source.v || source.countV != 1 || source.u.requested)
            {
                return tl::unexpected(Error("TerminalSplineOriginalCurveReference", Status::Unsupported));
            }
            std::optional<std::array<R, 2>> originalCurveDomain;
            TS_TRY(net, ReadOriginalSourceNet(source, math, originalCurveDomain));
            // ReadOriginalSourceNet has already charged the resident exact net.
            // Moving it into immutable ownership creates no second control net
            // and deliberately skips Requested/ExtendRange/Emit. The later
            // certificate counts both this resident reference and its copy.
            auto reference = std::make_shared<Reference>();
            reference->net = std::move(net);
            reference->originalCurveDomain = std::move(originalCurveDomain);
            return std::shared_ptr<const Reference>(std::move(reference));
        }
    );
}
Result<std::shared_ptr<const Reference>> CaptureNaturalCurveReference(const Source& source, const Limits& limits, const Charge& charge)
{
    return Guard<std::shared_ptr<const Reference>>(
        limits,
        charge,
        [&](Arithmetic& math) -> Result<std::shared_ptr<const Reference>>
        {
            if (source.v || source.countV != 1 || source.u.requested)
            {
                return tl::unexpected(Error("TerminalSplineNaturalCurveReference", Status::Unsupported));
            }
            std::optional<std::array<R, 2>> originalCurveDomain;
            TS_TRY(original, ReadOriginalSourceNet(source, math, originalCurveDomain, false));
            TS_TRY(net, NaturalCurveNet(original, math));
            auto reference = std::make_shared<Reference>();
            reference->net = std::move(net);
            reference->originalCurveDomain = std::move(originalCurveDomain);
            return std::shared_ptr<const Reference>(std::move(reference));
        }
    );
}
Result<Proof> Certify(
    const std::shared_ptr<const Reference>& reference,
    const BinaryNet& actual,
    const IntegerRatio& maximumMm,
    const Limits& limits,
    const Charge& charge
)
{
    return Guard<Proof>(
        limits,
        charge,
        [&](Arithmetic& math) -> Result<Proof>
        {
            if (!reference)
            {
                return tl::unexpected(Error("TerminalSplineReferenceMissing"));
            }
            TS_TRY(allowed, math.Ratio(maximumMm));
            TS_OK(math.Reserve(reference->net.cp.size())); // Already resident immutable reference.
            TS_OK(math.Reserve(reference->net.cp.size()));
            Net a = reference->net;
            TS_TRY(b, ReadBinary(actual, math));
            return CertifyNets(a, b, allowed, math);
        }
    );
}
Result<Proof> CertifyCurveRestriction(
    const std::shared_ptr<const Reference>& reference,
    const BinaryNet& actual,
    const IntegerRatio& maximumMm,
    const Limits& limits,
    const Charge& charge
)
{
    return Guard<Proof>(
        limits,
        charge,
        [&](Arithmetic& math) -> Result<Proof>
        {
            if (!reference)
            {
                return tl::unexpected(Error("TerminalSplineReferenceMissing"));
            }
            if (!reference->originalCurveDomain || reference->net.q || reference->net.nv != 1 || actual.basis.degreeV || actual.basis.countV != 1 ||
                !actual.basis.knotsV.empty())
            {
                return tl::unexpected(Error("TerminalSplineRestrictionCurveReference", Status::Unsupported));
            }
            TS_TRY(allowed, math.Ratio(maximumMm));
            TS_OK(math.Reserve(reference->net.cp.size())); // The immutable resident reference also counts.
            TS_OK(math.Reserve(reference->net.cp.size()));
            Net a = reference->net;
            TS_TRY(b, ReadBinary(actual, math));
            const auto& low = b.u[b.p];
            const auto& high = b.u[b.nu];
            if (Compare(low, (*reference->originalCurveDomain)[0]) < 0 || Compare(high, (*reference->originalCurveDomain)[1]) > 0)
            {
                return tl::unexpected(Error("TerminalSplineRestrictionOutsideSourceDomain"));
            }
            TS_OK(RestrictCurve(a, low, high, math));
            return CertifyNets(a, b, allowed, math);
        }
    );
}
Result<Proof> CertifyCurveTrim(
    const std::shared_ptr<const Reference>& reference,
    const std::array<std::string, 2>& exactSourceCuts,
    const BinaryNet& actual,
    const IntegerRatio& maximumMm,
    const Limits& limits,
    const Charge& charge
)
{
    return Guard<Proof>(
        limits,
        charge,
        [&](Arithmetic& math) -> Result<Proof>
        {
            if (!reference)
            {
                return tl::unexpected(Error("TerminalSplineReferenceMissing"));
            }
            if (!reference->originalCurveDomain || reference->net.q || reference->net.nv != 1 || actual.basis.degreeV || actual.basis.countV != 1 ||
                !actual.basis.knotsV.empty())
            {
                return tl::unexpected(Error("TerminalSplineRestrictionCurveReference", Status::Unsupported));
            }
            TS_TRY(low, math.Decimal(exactSourceCuts[0]));
            TS_TRY(high, math.Decimal(exactSourceCuts[1]));
            if (Compare(low, high) >= 0)
            {
                return tl::unexpected(Error("TerminalSplineTrimSourceRange"));
            }
            if (Compare(low, (*reference->originalCurveDomain)[0]) < 0 || Compare(high, (*reference->originalCurveDomain)[1]) > 0)
            {
                return tl::unexpected(Error("TerminalSplineTrimOutsideSourceDomain"));
            }
            TS_TRY(roundedLow, math.Rounded(low));
            TS_TRY(roundedHigh, math.Rounded(high));
            TS_TRY(targetLow, math.Binary(roundedLow));
            TS_TRY(targetHigh, math.Binary(roundedHigh));
            if (Compare(targetLow, targetHigh) >= 0)
            {
                return tl::unexpected(Error("TerminalSplineTrimBinaryIntervalCollapsed"));
            }
            TS_TRY(allowed, math.Ratio(maximumMm));
            TS_OK(math.Reserve(reference->net.cp.size()));
            TS_OK(math.Reserve(reference->net.cp.size()));
            Net a = reference->net;
            TS_TRY(b, ReadBinary(actual, math));
            if (Compare(b.u[b.p], targetLow) || Compare(b.u[b.nu], targetHigh))
            {
                return tl::unexpected(Error("TerminalSplineTrimNativeDomainMismatch"));
            }
            // Endpoint conversion is a declared representation correspondence,
            // not a source extension or a native reparameterization. Comparing
            // the complete mapped reference catches derivative-amplified cut
            // rounding and native construction error in ONE defining budget.
            TS_OK(RestrictCurve(a, low, high, math));
            TS_OK(MapCurveParameter(a, low, high, targetLow, targetHigh, math));
            return CertifyNets(a, b, allowed, math);
        }
    );
}
Result<double> SourceCurvePointDifferenceUpper(
    const std::shared_ptr<const Reference>& reference,
    const std::string& exactParameter,
    const std::array<std::string, 3>& sourcePoint,
    const IntegerRatio& lengthToMm,
    const Limits& limits,
    const Charge& charge
)
{
    return Guard<double>(
        limits,
        charge,
        [&](Arithmetic& math) -> Result<double>
        {
            if (!reference || !reference->originalCurveDomain || reference->net.q || reference->net.nv != 1)
            {
                return tl::unexpected(Error("TerminalSplineRestrictionCurveReference", Status::Unsupported));
            }
            TS_TRY(parameter, math.Decimal(exactParameter));
            if (Compare(parameter, (*reference->originalCurveDomain)[0]) < 0 || Compare(parameter, (*reference->originalCurveDomain)[1]) > 0)
            {
                return tl::unexpected(Error("TerminalSplinePointOutsideSourceDomain"));
            }
            TS_OK(math.Reserve(reference->net.cp.size()));
            TS_TRY(point, EvaluateCurve(reference->net, parameter, math));
            if (point[3].n <= 0)
            {
                return tl::unexpected(Error("TerminalSplineRefinedWeight"));
            }
            TS_TRY(scale, math.Ratio(lengthToMm));
            R maximum;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                TS_TRY(source, math.Decimal(sourcePoint[axis]));
                TS_TRY(mm, math.Mul(source, scale));
                TS_TRY(curvePoint, math.Div(point[axis], point[3]));
                TS_TRY(delta, math.Sub(curvePoint, mm));
                TS_TRY(sum, math.Add(maximum, Absolute(std::move(delta))));
                maximum = std::move(sum);
            }
            return math.Outward(maximum, true);
        }
    );
}
Result<PoleVisitProof> CheckPoleVisits(const BinaryNet& curve, const PoleVisits& visits, const Limits& limits, const Charge& charge)
{
    return Guard<PoleVisitProof>(
        limits,
        charge,
        [&](Arithmetic& math) -> Result<PoleVisitProof>
        {
            if (curve.basis.degreeV || curve.basis.countV != 1 || !curve.basis.knotsV.empty())
            {
                return tl::unexpected(Error("TerminalSplinePoleCurveOnly", Status::Unsupported));
            }
            TS_TRY(net, ReadBinary(curve, math));
            TS_TRY(low, math.Binary(visits.interval[0]));
            TS_TRY(high, math.Binary(visits.interval[1]));
            if (Compare(low, high) >= 0 || Compare(low, net.u[net.p]) < 0 || Compare(high, net.u[net.nu]) > 0)
            {
                return tl::unexpected(Error("TerminalSplinePoleInterval"));
            }
            std::array<R, 3> pole;
            for (std::size_t i = 0; i < pole.size(); ++i)
            {
                TS_TRY(coordinate, math.Binary(visits.poleMm[i]));
                pole[i] = std::move(coordinate);
            }
            TS_TRY(radius, math.Binary(visits.radiusMm));
            if (radius.n <= 0)
            {
                return tl::unexpected(Error("TerminalSplinePoleRadius"));
            }
            TS_TRY(radiusSquared, math.Mul(radius, radius));
            TS_OK(math.Reserve(net.u.size() + 2));
            std::vector<R> cuts;
            cuts.reserve(net.u.size() + 2);
            cuts.push_back(low);
            for (const auto& knot : net.u)
            {
                TS_OK(math.Spend());
                if (Compare(knot, low) > 0 && Compare(knot, high) < 0 && Compare(cuts.back(), knot) != 0)
                {
                    cuts.push_back(knot);
                }
            }
            cuts.push_back(high);
            TS_OK(Refine(net, true, cuts, math));
            PoleState state;
            state.phase = visits.firstAllowed ? PolePhase::First : PolePhase::Outside;
            for (std::size_t i = net.p; i < net.nu; ++i)
            {
                TS_OK(math.Spend());
                if (Compare(net.u[i], net.u[i + 1]) >= 0 || Compare(net.u[i], low) < 0 || Compare(net.u[i + 1], high) > 0)
                {
                    continue;
                }
                TS_OK(math.Reserve(net.p + 1));
                const std::vector<H> cp(net.cp.begin() + std::ptrdiff_t(i - net.p), net.cp.begin() + std::ptrdiff_t(i + 1));
                TS_TRY(valid, VisitPolePatch(cp, pole, radiusSquared, visits, state, 16, math));
                if (!valid)
                {
                    return state.proof;
                }
            }
            state.proof.established = state.proof.patches > 0 && state.outsideSeen;
            return state.proof;
        }
    );
}
Result<PoleVisitProof> CheckLinePoleVisits(const AffineLineDefinition& line, const PoleVisits& visits, const Limits& limits, const Charge& charge)
{
    return Guard<PoleVisitProof>(
        limits,
        charge,
        [&](Arithmetic& math) -> Result<PoleVisitProof>
        {
            TS_TRY(naturalFirst, math.Binary(line.naturalInterval[0]));
            TS_TRY(naturalLast, math.Binary(line.naturalInterval[1]));
            TS_TRY(first, math.Binary(visits.interval[0]));
            TS_TRY(last, math.Binary(visits.interval[1]));
            if (Compare(naturalFirst, naturalLast) >= 0 || Compare(first, last) >= 0 || Compare(first, naturalFirst) < 0 ||
                Compare(last, naturalLast) > 0)
            {
                return tl::unexpected(Error("TerminalSplineLineInterval"));
            }
            TS_TRY(stepFirst, math.Binary(line.stepAtNaturalEnds[0]));
            TS_TRY(stepLast, math.Binary(line.stepAtNaturalEnds[1]));
            TS_TRY(scale, math.Binary(line.scale));
            if (scale.n == 0)
            {
                return tl::unexpected(Error("TerminalSplineLineScale"));
            }
            std::array<R, 3> origin, direction, pole;
            bool nonzeroDirection = false;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                TS_TRY(o, math.Binary(line.originMm[axis]));
                TS_TRY(d, math.Binary(line.direction[axis]));
                TS_TRY(p, math.Binary(visits.poleMm[axis]));
                nonzeroDirection = nonzeroDirection || d.n != 0;
                origin[axis] = std::move(o);
                direction[axis] = std::move(d);
                pole[axis] = std::move(p);
            }
            if (!nonzeroDirection)
            {
                return tl::unexpected(Error("TerminalSplineLineDirection"));
            }
            TS_TRY(radius, math.Binary(visits.radiusMm));
            if (radius.n <= 0)
            {
                return tl::unexpected(Error("TerminalSplinePoleRadius"));
            }
            TS_TRY(radiusSquared, math.Mul(radius, radius));
            R naturalLength, stepLength;
            if (!line.directParameter)
            {
                TS_TRY(length, math.Sub(naturalLast, naturalFirst));
                TS_TRY(stepDelta, math.Sub(stepLast, stepFirst));
                if (stepDelta.n == 0)
                {
                    return tl::unexpected(Error("TerminalSplineLineParameterMapping"));
                }
                naturalLength = std::move(length);
                stepLength = std::move(stepDelta);
            }
            TS_OK(math.Reserve(2));
            std::vector<H> cp(2);
            const std::array<R, 2> selected{ first, last };
            for (std::size_t end = 0; end < cp.size(); ++end)
            {
                R s = selected[end];
                if (!line.directParameter)
                {
                    TS_TRY(delta, math.Sub(selected[end], naturalFirst));
                    TS_TRY(fraction, math.Div(delta, naturalLength));
                    TS_TRY(offset, math.Mul(fraction, stepLength));
                    TS_TRY(parameter, math.Add(stepFirst, offset));
                    s = std::move(parameter);
                }
                // SmLine evaluates its stored analytic fields, not its rounded
                // NURBS endpoint cache. Form the affine selected endpoints as
                // exact ratios; 2^53+1, for example, must NOT round to 2^53 here.
                // The tiny-domain native branch uses s=t even for InsideOut.
                TS_TRY(amplitude, math.Mul(s, scale));
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    TS_TRY(delta, math.Mul(amplitude, direction[axis]));
                    TS_TRY(coordinate, math.Add(origin[axis], delta));
                    cp[end][axis] = std::move(coordinate);
                }
                cp[end][3] = R{ 1, 1 };
            }
            // An affine function is exactly its degree-one Bezier interpolation
            // in increasing selected t. Reuse the same CLOSED-ball visitation
            // proof and endpoint authorizations; there is no second geometric
            // interpretation and no machine endpoint/parameter sampling.
            PoleState state;
            state.phase = visits.firstAllowed ? PolePhase::First : PolePhase::Outside;
            TS_TRY(valid, VisitPolePatch(cp, pole, radiusSquared, visits, state, 16, math));
            state.proof.established = valid && state.proof.patches > 0 && state.outsideSeen;
            return state.proof;
        }
    );
}
Result<bool> CheckScalarCoverage(
    const std::array<std::string, 2>& requested,
    const std::vector<std::string>& controls,
    const Limits& limits,
    const Charge& charge
)
{
    return Guard<bool>(
        limits,
        charge,
        [&](Arithmetic& math) -> Result<bool>
        {
            TS_TRY(low, math.Decimal(requested[0]));
            TS_TRY(high, math.Decimal(requested[1]));
            if (Compare(low, high) >= 0)
            {
                return tl::unexpected(Error("TerminalSplineRequestedRange"));
            }
            if (controls.size() > limits.controlSlots)
            {
                return tl::unexpected(Error("TerminalSplineControlCapacity", Status::ResourceLimit));
            }
            bool contained = true;
            for (const auto& text : controls)
            {
                TS_TRY(value, math.Decimal(text));
                contained = contained && Compare(low, value) <= 0 && Compare(value, high) <= 0;
            }
            return contained;
        }
    );
}
Result<double> SourcePointDifferenceUpper(
    const std::array<std::string, 3>& source,
    const IntegerRatio& lengthToMm,
    const std::array<double, 3>& actual,
    const Limits& limits,
    const Charge& charge
)
{
    return Guard<double>(
        limits,
        charge,
        [&](Arithmetic& math) -> Result<double>
        {
            TS_TRY(scale, math.Ratio(lengthToMm));
            R sum;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                TS_TRY(raw, math.Decimal(source[axis]));
                TS_TRY(mm, math.Mul(raw, scale));
                TS_TRY(native, math.Binary(actual[axis]));
                TS_TRY(difference, math.Sub(mm, native));
                TS_TRY(next, math.Add(sum, Absolute(std::move(difference))));
                sum = std::move(next);
            }
            return math.Outward(sum, true);
        }
    );
}
#undef TS_TRY
#undef TS_OK
} // namespace step_import::detail::terminal_spline
