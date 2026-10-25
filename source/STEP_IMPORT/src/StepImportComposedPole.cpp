// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportComposedPole.h"

#include "StepImportInterval.h"
#include "StepImportProofEnvironment.h"

#include <numeric>
#include <optional>

namespace step_import::detail
{
namespace
{
using I = Interval;
using V = std::array<I, 3>;
using H = std::array<I, 4>;
using M = std::array<V, 3>;
using Net = terminal_spline::BinaryNet;
auto Refuse(const char* key, Status status = Status::Indeterminate)
{
    return tl::unexpected(Failure{ status, key, "Continuous composed-curve pole visits are unestablished", {}, "RevolutionComposedPole" });
}
#define CP_TRY(name, expression)                                                                                                                     \
    auto name##Result = (expression);                                                                                                                \
    if (!name##Result)                                                                                                                               \
        return tl::unexpected(std::move(name##Result.error()));                                                                                      \
    auto name = std::move(*name##Result)
#define CP_OK(expression)                                                                                                                            \
    do                                                                                                                                               \
    {                                                                                                                                                \
        auto operation = (expression);                                                                                                               \
        if (!operation)                                                                                                                              \
            return tl::unexpected(std::move(operation.error()));                                                                                     \
    } while (false)

// Exact arithmetic identities preserve known zeros without granting a rounding
// allowance. Every remaining elementary operation rounds outwards. In particular
// a zero Gram residual must not become a large dependency interval in q_i*q_j.
bool Zero(I a)
{
    return a.low == 0 && a.high == 0;
}
I Add(I a, I b)
{
    if (Zero(a))
    {
        return b;
    }
    if (Zero(b))
    {
        return a;
    }
    if (a.low == a.high && b.low == b.high && a.low == -b.low)
    {
        return Exact(0);
    }
    return a + b;
}
I Minus(I a)
{
    return { -a.high, -a.low };
}
I Sub(I a, I b)
{
    return Add(a, Minus(b));
}
I Mul(I a, I b)
{
    if (Zero(a) || Zero(b))
    {
        return Exact(0);
    }
    if (a.low == 1 && a.high == 1)
    {
        return b;
    }
    if (b.low == 1 && b.high == 1)
    {
        return a;
    }
    return a * b;
}
I Divide(I a, I b)
{
    if (b.low <= 0 && b.high >= 0)
    {
        throw std::domain_error("ComposedPoleDivision");
    }
    if (Zero(a))
    {
        return Exact(0);
    }
    if (a.low == a.high && b.low == b.high && a.low == b.low)
    {
        return Exact(1);
    }
    if (b.low == 1 && b.high == 1)
    {
        return a;
    }
    return a / b;
}
I Square(I a)
{
    if (Zero(a))
    {
        return Exact(0);
    }
    const double minimum = a.low > 0 ? a.low : a.high < 0 ? -a.high : 0;
    const double maximum = std::max(std::abs(a.low), std::abs(a.high));
    return { minimum == 0 ? 0 : std::max(0., Down(minimum * minimum)), Up(maximum * maximum) };
}
double Magnitude(I a)
{
    return std::max(std::abs(a.low), std::abs(a.high));
}
I Symmetric(double maximum)
{
    return { -maximum, maximum };
}
I Hull(I a, I b)
{
    return { std::min(a.low, b.low), std::max(a.high, b.high) };
}
V Plus(V a, V b)
{
    for (unsigned i = 0; i < 3; ++i)
    {
        a[i] = Add(a[i], b[i]);
    }
    return a;
}
V Scale(V a, I b)
{
    for (auto& x : a)
    {
        x = Mul(x, b);
    }
    return a;
}
I Inner(V a, V b)
{
    I sum = Exact(0);
    for (unsigned i = 0; i < 3; ++i)
    {
        sum = Add(sum, Mul(a[i], b[i]));
    }
    return sum;
}
I NormSquared(V a)
{
    I sum = Exact(0);
    for (auto x : a)
    {
        sum = Add(sum, Square(x));
    }
    return sum;
}
V Apply(const M& a, V x)
{
    return { Inner(a[0], x), Inner(a[1], x), Inner(a[2], x) };
}
M Transpose(const M& a)
{
    M b{};
    for (unsigned i = 0; i < 3; ++i)
    {
        for (unsigned j = 0; j < 3; ++j)
        {
            b[i][j] = a[j][i];
        }
    }
    return b;
}
M Product(const M& a, const M& b)
{
    const auto bt = Transpose(b);
    M out{};
    for (unsigned i = 0; i < 3; ++i)
    {
        for (unsigned j = 0; j < 3; ++j)
        {
            out[i][j] = Inner(a[i], bt[j]);
        }
    }
    return out;
}
M Difference(M a, const M& b)
{
    for (unsigned i = 0; i < 3; ++i)
    {
        for (unsigned j = 0; j < 3; ++j)
        {
            a[i][j] = Sub(a[i][j], b[i][j]);
        }
    }
    return a;
}
M Symmetrize(M a)
{
    for (unsigned i = 0; i < 3; ++i)
    {
        for (unsigned j = i + 1; j < 3; ++j)
        {
            a[i][j] = a[j][i] = Mul(Add(a[i][j], a[j][i]), Exact(.5));
        }
    }
    return a;
}
I Quadratic(const M& a, V x)
{
    I out = Exact(0);
    for (unsigned i = 0; i < 3; ++i)
    {
        out = Add(out, Mul(a[i][i], Square(x[i])));
        for (unsigned j = i + 1; j < 3; ++j)
        {
            out = Add(out, Mul(Exact(2), Mul(a[i][j], Mul(x[i], x[j]))));
        }
    }
    return out;
}
I QuadraticDerivative(const M& a, V x, V dx)
{
    return Mul(Exact(2), Inner(x, Apply(a, dx)));
}

class Work
{
public:

    const terminal_spline::Limits& limits;
    const terminal_spline::Charge& charge;
    bool callbackActive = false;
    std::size_t slots = 0;
    Result<void> Spend(std::size_t units)
    {
        callbackActive = true;
        auto result = charge(units);
        callbackActive = false;
        // A cooperative callback runs application code on this thread. Recheck
        // AFTER every successful callback: a changed rounding/FTZ environment
        // cannot reuse the entry probe and accidentally qualify later bounds.
        if (result && !SurfaceProofNumericEnvironment())
        {
            return Refuse("GeneralComposedPoleNumericEnvironment");
        }
        return result;
    }
    Result<void> Reserve(std::size_t count)
    {
        if (count > limits.controlSlots - slots)
        {
            return Refuse("GeneralComposedPoleControlCapacity", Status::ResourceLimit);
        }
        CP_OK(Spend(count ? count : 1));
        slots += count;
        return {};
    }
};
struct Spline
{
    // The defining net remains immutable for this synchronous proof. Only the
    // enclosed homogeneous coordinates are shifted, before evaluation, so a
    // large world origin does not have to cancel a rounded Euclidean point.
    const Net& source;
    unsigned degree = 0;
    double minWeight = 0, maxWeight = 0;
    std::vector<H> controls;
};
Result<Spline> Read(const Net& net, V shift, Work& work)
{
    const auto& b = net.basis;
    if (b.degreeV || b.countV != 1 || !b.knotsV.empty() || b.degreeU < 1 || b.degreeU > 17 || b.countU <= b.degreeU ||
        b.countU > work.limits.controlSlots || net.homogeneous.size() != b.countU ||
        b.countU > std::numeric_limits<std::size_t>::max() - b.degreeU - 1 || b.knotsU.size() != b.countU + b.degreeU + 1)
    {
        return Refuse("GeneralComposedPoleSplineLayout", Status::Unsupported);
    }
    CP_OK(work.Reserve(b.countU + b.knotsU.size()));
    const auto& knots = b.knotsU;
    for (std::size_t i = 0; i < knots.size(); ++i)
    {
        CP_OK(work.Spend(1));
        if (!std::isfinite(knots[i]) || (i && knots[i] < knots[i - 1]))
        {
            return Refuse("GeneralComposedPoleKnots");
        }
    }
    if (!(knots[b.degreeU] < knots[b.countU]))
    {
        return Refuse("GeneralComposedPoleNaturalInterval");
    }
    if (!(knots[b.degreeU] < knots[b.degreeU + 1]) || !(knots[b.countU - 1] < knots[b.countU]))
    {
        return Refuse("GeneralComposedPoleClamping", Status::Unsupported);
    }
    for (unsigned i = 0; i <= b.degreeU; ++i)
    {
        if (knots[i] != knots[b.degreeU] || knots[b.countU + i] != knots[b.countU])
        {
            return Refuse("GeneralComposedPoleClamping", Status::Unsupported);
        }
    }
    for (std::size_t i = b.degreeU + 1; i < b.countU;)
    {
        std::size_t j = i + 1;
        while (j < b.countU && knots[j] == knots[i])
        {
            ++j;
        }
        if (j - i > b.degreeU)
        {
            return Refuse("GeneralComposedPoleDiscontinuity", Status::Unsupported);
        }
        i = j;
    }
    Spline out{ net, b.degreeU, std::numeric_limits<double>::max(), 0, {} };
    out.controls.reserve(b.countU);
    for (const auto& cp : net.homogeneous)
    {
        CP_OK(work.Spend(48));
        if (!(cp[3] > 0) || !std::isfinite(cp[3]))
        {
            return Refuse("GeneralComposedPolePositiveWeights");
        }
        H value{};
        value[3] = Exact(cp[3]);
        for (unsigned a = 0; a < 3; ++a)
        {
            value[a] = Sub(Exact(cp[a]), Mul(shift[a], value[3]));
        }
        out.controls.push_back(value);
        out.minWeight = std::min(out.minWeight, cp[3]);
        out.maxWeight = std::max(out.maxWeight, cp[3]);
    }
    return out;
}
struct Jet
{
    V value{}, derivative{};
};
Result<V> BezierValueHull(const Spline& spline, I parameter, Work& work)
{
    const unsigned p = spline.degree;
    const auto& knots = spline.source.basis.knotsU;
    CP_OK(work.Reserve(3 * (p + 1)));
    CP_OK(work.Spend(128 * (p + 1) * (p + 1)));
    using Polygon = std::array<H, 18>;
    Polygon polygon{};
    for (unsigned i = 0; i <= p; ++i) { polygon[i] = spline.controls[i]; }
    const auto unit = [](I x) -> I { return { std::max(0., x.low), std::min(1., x.high) }; };
    const auto split = [&](Polygon input, I alpha, bool right) -> Polygon
    {
        // Every exact split coefficient is in [0,1]. Repeated occurrences of
        // its outward interval may lose correlation, but cannot lose the exact
        // restricted net. This is de Casteljau, not sampling or fitting.
        alpha = unit(alpha);
        const auto beta = unit(Sub(Exact(1), alpha));
        Polygon result{};
        result[right ? p : 0] = input[right ? p : 0];
        for (unsigned level = 1; level <= p; ++level)
        {
            for (unsigned i = 0; i <= p - level; ++i)
            {
                for (unsigned a = 0; a < 4; ++a)
                {
                    input[i][a] = Add(Mul(beta, input[i][a]), Mul(alpha, input[i + 1][a]));
                }
            }
            result[right ? p - level : level] = input[right ? p - level : 0];
        }
        return result;
    };
    const double first = knots[p], last = knots[spline.source.basis.countU];
    if (parameter.high < last)
    {
        polygon = split(polygon, Divide(Sub(Exact(parameter.high), Exact(first)), Sub(Exact(last), Exact(first))), false);
    }
    if (parameter.low > first)
    {
        polygon = split(polygon, Divide(Sub(Exact(parameter.low), Exact(first)), Sub(Exact(parameter.high), Exact(first))), true);
    }
    V hull{};
    for (unsigned i = 0; i <= p; ++i)
    {
        // Restricted weights are convex combinations of original positive
        // weights. Intersect that independent bound before projecting the
        // homogeneous control hull to Euclidean coordinates.
        polygon[i][3] = { std::max(polygon[i][3].low, spline.minWeight), std::min(polygon[i][3].high, spline.maxWeight) };
        if (!(polygon[i][3].low > 0) || polygon[i][3].low > polygon[i][3].high)
        {
            return Refuse("GeneralComposedPoleWeightBounds");
        }
        for (unsigned a = 0; a < 3; ++a)
        {
            const auto point = Divide(polygon[i][a], polygon[i][3]);
            hull[a] = i ? Hull(hull[a], point) : point;
        }
    }
    return hull;
}
// De Boor encloses an entire parameter interval on one nonzero knot span.
// The homogeneous derivative net is evaluated separately; the positive-weight
// hull supplies an independent denominator bound despite interval dependency.
Result<Jet> BoundSpan(const Spline& spline, std::size_t span, I parameter, Work& work)
{
    const auto p = spline.degree;
    CP_OK(work.Spend(256 * (p + 1) * (p + 1)));
    CP_OK(work.Reserve(2 * (p + 1)));
    const auto& knots = spline.source.basis.knotsU;
    const auto evaluate = [&](bool derivative) -> H
    {
        std::array<H, 18> d{};
        const auto degree = derivative ? p - 1 : p;
        for (unsigned j = 0; j <= degree; ++j)
        {
            const auto i = span - p + j;
            if (!derivative)
            {
                d[j] = spline.controls[i];
            }
            else
            {
                const auto factor = Divide(Exact(p), Sub(Exact(knots[i + p + 1]), Exact(knots[i + 1])));
                for (unsigned a = 0; a < 4; ++a)
                {
                    d[j][a] = Mul(factor, Sub(spline.controls[i + 1][a], spline.controls[i][a]));
                }
            }
        }
        for (unsigned level = 1; level <= degree; ++level)
        {
            for (unsigned j = degree + 1; j-- > level;)
            {
                const auto i = span - p + j;
                const auto lo = Exact(knots[i + (derivative ? 1 : 0)]);
                const auto hi = Exact(knots[i + p + 1 - level]);
                auto alpha = Divide(Sub(parameter, lo), Sub(hi, lo));
                // All real parameters lie in this knot span, hence each
                // de Boor mixing parameter is in [0,1]. This is an enclosure
                // intersection, not clamping an out-of-domain source value.
                alpha = { std::max(0., alpha.low), std::min(1., alpha.high) };
                if (alpha.low > alpha.high)
                {
                    throw std::domain_error("ComposedPoleMixingBounds");
                }
                const auto beta = Sub(Exact(1), alpha);
                for (unsigned a = 0; a < 4; ++a)
                {
                    d[j][a] = Add(Mul(beta, d[j - 1][a]), Mul(alpha, d[j][a]));
                }
            }
        }
        return d[degree];
    };
    auto value = evaluate(false);
    const auto derivative = evaluate(true);
    value[3] = { std::max(value[3].low, spline.minWeight), std::min(value[3].high, spline.maxWeight) };
    if (!(value[3].low > 0) || value[3].low > value[3].high)
    {
        return Refuse("GeneralComposedPoleWeightBounds");
    }
    Jet jet;
    for (unsigned a = 0; a < 3; ++a)
    {
        jet.value[a] = Divide(value[a], value[3]);
        jet.derivative[a] = Divide(Sub(Mul(derivative[a], value[3]), Mul(value[a], derivative[3])), Square(value[3]));
    }
    if (spline.source.basis.countU == p + 1 && parameter.low < parameter.high)
    {
        CP_TRY(hull, BezierValueHull(spline, parameter, work));
        for (unsigned a = 0; a < 3; ++a)
        {
            jet.value[a] = { std::max(jet.value[a].low, hull[a].low), std::min(jet.value[a].high, hull[a].high) };
            if (jet.value[a].low > jet.value[a].high) { return Refuse("GeneralComposedPoleBezierHull"); }
        }
    }
    return jet;
}
Result<Jet> BoundSpline(const Spline& spline, I parameter, Work& work)
{
    const auto& b = spline.source.basis;
    bool found = false;
    Jet out;
    for (std::size_t i = spline.degree; i < b.countU; ++i)
    {
        CP_OK(work.Spend(1));
        const double lo = std::max(parameter.low, b.knotsU[i]), hi = std::min(parameter.high, b.knotsU[i + 1]);
        if (!(b.knotsU[i] < b.knotsU[i + 1]) || lo > hi)
        {
            continue;
        }
        CP_TRY(piece, BoundSpan(spline, i, { lo, hi }, work));
        if (!found)
        {
            out = piece;
        }
        else
        {
            for (unsigned a = 0; a < 3; ++a)
            {
                out.value[a] = Hull(out.value[a], piece.value[a]);
                out.derivative[a] = Hull(out.derivative[a], piece.derivative[a]);
            }
        }
        found = true;
    }
    if (!found)
    {
        return Refuse("GeneralComposedPoleParameterCoverage");
    }
    return out;
}
struct Geometry
{
    M a, b, c, g, h, k, l;
    V delta{}, linearK{}, linearL{};
    I angleScale{}, radiusSquared{};
};
Geometry Prepare(const RevolutionCurveDefinition& source, V pole, double radius)
{
    const auto x = Exact(source.frame[0]), y = Exact(source.frame[1]), z = Exact(source.frame[2]);
    Geometry g;
    for (unsigned i = 0; i < 3; ++i)
    {
        for (unsigned j = 0; j < 3; ++j)
        {
            g.a[i][j] = Add(Mul(x[i], x[j]), Mul(y[i], y[j]));
            g.b[i][j] = Sub(Mul(y[i], x[j]), Mul(x[i], y[j]));
            g.c[i][j] = Mul(z[i], z[j]);
        }
    }
    const auto at = Transpose(g.a), bt = Transpose(g.b);
    g.g = Symmetrize(Difference(Product(at, g.a), Product(bt, g.b)));
    g.h = Symmetrize(Product(at, g.b));
    g.k = Symmetrize(Product(at, g.c));
    g.l = Symmetrize(Product(bt, g.c));
    for (unsigned i = 0; i < 3; ++i)
    {
        g.delta[i] = Sub(Exact(source.originMm[i]), pole[i]);
    }
    g.linearK = Apply(at, g.delta);
    g.linearL = Apply(bt, g.delta);
    g.angleScale = Divide(Exact(source.angleNumerator), Exact(180));
    g.radiusSquared = Square(Exact(radius));
    return g;
}
struct Bounds
{
    I distance{}, derivative{};
};
Result<Bounds> Bound(const Spline& uv, const Spline& generator, const Geometry& g, I parameter, Work& work)
{
    CP_TRY(uvJet, BoundSpline(uv, parameter, work));
    const auto& knots = generator.source.basis.knotsU;
    // Global control-hull containment was proved before recursion. Intersect
    // harmless outward rounding at source endpoints with that independent fact.
    const I v{ std::max(uvJet.value[1].low, knots[generator.degree]), std::min(uvJet.value[1].high, knots[generator.source.basis.countU]) };
    if (v.low > v.high)
    {
        return Refuse("GeneralComposedPoleGeneratorCoverage");
    }
    CP_TRY(genJet, BoundSpline(generator, v, work));
    const auto q = genJet.value, dq = Scale(genJet.derivative, uvJet.derivative[1]);
    CP_OK(work.Spend(4096)); // Fixed matrix/interval work, before the arithmetic.
    const auto b = Apply(g.b, q), d = Plus(g.delta, Apply(g.c, q));
    const std::array<I, 4> residual = { Quadratic(g.g, q),
                                        Quadratic(g.h, q),
                                        Add(Quadratic(g.k, q), Inner(g.linearK, q)),
                                        Add(Quadratic(g.l, q), Inner(g.linearL, q)) };
    const std::array<I, 4> residualDerivative = { QuadraticDerivative(g.g, q, dq),
                                                  QuadraticDerivative(g.h, q, dq),
                                                  Add(QuadraticDerivative(g.k, q, dq), Inner(g.linearK, dq)),
                                                  Add(QuadraticDerivative(g.l, q, dq), Inner(g.linearL, dq)) };
    // |C-P|^2 = |d|^2+|b|^2 + G cos^2 + 2H cos sin + 2K cos + 2L sin.
    // Form the small Gram residuals BEFORE applying generator interval bounds;
    // bounding each full rotated coordinate would destroy the angular cancellations.
    I error = Exact(0), derivativeError = Exact(0), angleError = Exact(0);
    for (unsigned i = 0; i < 4; ++i)
    {
        error = Add(error, Mul(Exact(i ? 2 : 1), Exact(Magnitude(residual[i]))));
        derivativeError = Add(derivativeError, Mul(Exact(i ? 2 : 1), Exact(Magnitude(residualDerivative[i]))));
        angleError = Add(angleError, Mul(Exact(2), Exact(Magnitude(residual[i]))));
    }
    const auto angularSpeed = Mul(g.angleScale, uvJet.derivative[0]);
    derivativeError = Add(derivativeError, Mul(Exact(Magnitude(angularSpeed)), angleError));
    Bounds out;
    out.distance = Add(Sub(Add(NormSquared(d), NormSquared(b)), g.radiusSquared), Symmetric(error.high));
    const auto baseDerivative = Mul(Exact(2), Add(Inner(d, Apply(g.c, dq)), Inner(b, Apply(g.b, dq))));
    out.derivative = Add(baseDerivative, Symmetric(derivativeError.high));
    return out;
}
enum class Phase
{
    First,
    Outside,
    Last
};
struct State
{
    Phase phase = Phase::Outside;
    bool outside = false;
    ComposedPoleProof proof;
};
Result<Bounds> Endpoint(
    const Spline& uv,
    const Spline& generator,
    const Geometry& geometry,
    double parameter,
    std::optional<Bounds>& cached,
    Work& work
)
{
    if (!cached)
    {
        CP_TRY(bound, Bound(uv, generator, geometry, Exact(parameter), work));
        cached = bound; // Failed/cancelled bounds never enter the cache.
    }
    return *cached;
}
Result<bool> Visit(
    const Spline& uv,
    const Spline& generator,
    const Geometry& g,
    I interval,
    std::optional<Bounds>& firstEndpoint,
    std::optional<Bounds>& lastEndpoint,
    const terminal_spline::PoleVisits& visits,
    State& state,
    unsigned depth,
    Work& work
)
{
    CP_TRY(bounds, Bound(uv, generator, g, interval, work));
    ++state.proof.regions;
    if (bounds.distance.low > 0)
    {
        if (state.phase == Phase::Last)
        {
            return false;
        }
        state.phase = Phase::Outside;
        state.outside = true;
        return true;
    }
    if (bounds.distance.high <= 0)
    {
        if (state.phase == Phase::Outside)
        {
            if (!visits.lastAllowed)
            {
                return false;
            }
            state.phase = Phase::Last;
        }
        return true;
    }
    // Endpoint enclosures are continuous interval proofs at exact stored t,
    // never samples used to accept a span. Unknown endpoint signs stay unknown.
    CP_TRY(first, Endpoint(uv, generator, g, interval.low, firstEndpoint, work));
    CP_TRY(last, Endpoint(uv, generator, g, interval.high, lastEndpoint, work));
    const bool increasing = bounds.derivative.low >= 0, decreasing = bounds.derivative.high <= 0;
    if ((increasing && first.distance.low > 0) || (decreasing && last.distance.low > 0))
    {
        if (state.phase == Phase::Last)
        {
            return false;
        }
        state.phase = Phase::Outside;
        state.outside = true;
        return true;
    }
    if (state.phase == Phase::First && increasing)
    {
        state.outside = state.outside || last.distance.low > 0;
        return true;
    }
    if (visits.lastAllowed && decreasing)
    {
        state.outside = state.outside || first.distance.low > 0;
        state.phase = Phase::Last;
        return true;
    }
    if ((state.phase == Phase::Outside && !visits.lastAllowed && (first.distance.high <= 0 || last.distance.high <= 0)) ||
        (state.phase == Phase::Last && (first.distance.low > 0 || last.distance.low > 0)) || !depth)
    {
        return false;
    }
    const double middle = std::midpoint(interval.low, interval.high);
    if (!(interval.low < middle && middle < interval.high))
    {
        return false;
    }
    // A parent's exact-t enclosure is the same mathematical query in either
    // child. Share it lazily, including both one-sided knot derivatives from
    // BoundSpline. Interval derivative bounds and traversal order stay intact.
    // The fixed depth bounds this stack storage; saved arithmetic is not charged
    // as performed work. Every actual Charge still rechecks the numeric FENV.
    std::optional<Bounds> midpointEndpoint;
    CP_TRY(left, Visit(uv, generator, g, { interval.low, middle }, firstEndpoint, midpointEndpoint, visits, state, depth - 1, work));
    if (!left)
    {
        return false;
    }
    return Visit(uv, generator, g, { middle, interval.high }, midpointEndpoint, lastEndpoint, visits, state, depth - 1, work);
}
} // namespace
Result<ComposedPoleProof> CheckComposedPoleVisits(
    const RevolutionCurveDefinition& curve,
    const terminal_spline::PoleVisits& visits,
    const terminal_spline::Limits& limits,
    const terminal_spline::Charge& charge
)
{
    if (!charge || !limits.controlSlots)
    {
        return Refuse("GeneralComposedPoleCapacityConfiguration", Status::ResourceLimit);
    }
    Work work{ limits, charge };
    try
    {
        CP_OK(work.Spend(1));
        if (!SurfaceProofNumericEnvironment())
        {
            return Refuse("GeneralComposedPoleNumericEnvironment");
        }
        if (!(visits.radiusMm > 0) || !std::isfinite(visits.radiusMm) || !(curve.angleNumerator > 0) || !std::isfinite(curve.angleNumerator))
        {
            return Refuse("GeneralComposedPoleRadiusOrAngle");
        }
        if (!std::isfinite(visits.interval[0]) || !std::isfinite(visits.interval[1]) || !(visits.interval[0] < visits.interval[1]))
        {
            return Refuse("GeneralComposedPoleSelectedInterval");
        }
        CP_OK(work.Reserve(128));
        CP_OK(work.Spend(8192)); // Fixed frame/Gram preparation below.
        const auto origin = Exact(curve.originMm);
        const auto pole = Exact(visits.poleMm);
        const auto geometry = Prepare(curve, pole, visits.radiusMm);
        CP_TRY(uv, Read(curve.uv, Exact(Point{}), work));
        CP_TRY(generator, Read(curve.generator, origin, work));
        const auto& knots = curve.uv.basis.knotsU;
        if (visits.interval[0] < knots[uv.degree] || visits.interval[1] > knots[curve.uv.basis.countU])
        {
            return Refuse("GeneralComposedPoleSelectedInterval");
        }
        const auto& gk = curve.generator.basis.knotsU;
        const auto& domain = curve.stepDomain;
        for (double x : domain)
        {
            if (!std::isfinite(x))
            {
                return Refuse("GeneralComposedPoleSupportDomain");
            }
        }
        if (!(domain[0] < domain[2] && domain[1] < domain[3]) || domain[1] < gk[generator.degree] || domain[3] > gk[curve.generator.basis.countU])
        {
            return Refuse("GeneralComposedPoleSupportDomain");
        }
        for (const auto& point : uv.controls)
        {
            CP_OK(work.Spend(32));
            const auto u = Divide(point[0], point[3]), v = Divide(point[1], point[3]);
            if (u.low < domain[0] || u.high > domain[2] || v.low < domain[1] || v.high > domain[3])
            {
                return Refuse("GeneralComposedPoleControlHullCoverage");
            }
        }
        State state;
        state.phase = visits.firstAllowed ? Phase::First : Phase::Outside;
        // Every distinct selected UV span is visited in source order. BoundSpline
        // covers ALL intersecting generator spans and one-sided derivatives, so
        // V crossing a generator knot cannot leave an unchecked parameter gap.
        double first = visits.interval[0];
        for (std::size_t i = uv.degree + 1; i <= curve.uv.basis.countU; ++i)
        {
            CP_OK(work.Spend(1));
            const double last = std::min(knots[i], visits.interval[1]);
            if (last <= first)
            {
                continue;
            }
            std::optional<Bounds> firstEndpoint, lastEndpoint;
            CP_TRY(valid, Visit(uv, generator, geometry, { first, last }, firstEndpoint, lastEndpoint, visits, state, 16, work));
            if (!valid)
            {
                return state.proof;
            }
            first = last;
            if (first == visits.interval[1])
            {
                break;
            }
        }
        state.proof.established = first == visits.interval[1] && state.proof.regions > 0 && state.outside;
        return state.proof;
    }
    catch (const std::overflow_error&)
    {
        if (work.callbackActive)
        {
            throw;
        }
        return Refuse("GeneralComposedPoleNumericCapacity", Status::ResourceLimit);
    }
    catch (const std::domain_error&)
    {
        if (work.callbackActive)
        {
            throw;
        }
        return Refuse("GeneralComposedPoleNumericBounds");
    }
}
} // namespace step_import::detail
