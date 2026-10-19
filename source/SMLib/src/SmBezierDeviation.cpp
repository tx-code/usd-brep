// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "SmBezierDeviation.h"

#include <SmBSplineCurve.h>
#include <SmCurveTypes.h>
#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <limits>
#include <nurbs.h>
#if defined(_M_X64) || defined(__SSE2__)
#    include <xmmintrin.h>
#endif
#if (defined(_M_X64) || defined(__SSE2__)) && !defined(__FAST_MATH__) && !defined(_M_FP_FAST) &&                                                     \
    (!defined(__FINITE_MATH_ONLY__) || __FINITE_MATH_ONLY__ == 0)
#    define SM_BEZIER_CERT_ARITHMETIC 1
#else
#    define SM_BEZIER_CERT_ARITHMETIC 0
#endif
#if defined(_MSC_VER) && SM_BEZIER_CERT_ARITHMETIC
#    pragma float_control(precise, on, push)
#endif
namespace sm_bezier_deviation
{
namespace
{
#if SM_BEZIER_CERT_ARITHMETIC
constexpr double epsilon = std::numeric_limits<double>::epsilon();
constexpr double infinity = std::numeric_limits<double>::infinity();
double up(double value)
{
    return std::nextafter(value, infinity);
}
double down(double value)
{
    return std::nextafter(value, -infinity);
}
using Quadratic = std::array<double, 3>;
constexpr std::array<std::array<unsigned, 2>, 3> restrictions{ { { 0, 0 }, { 0, 1 }, { 1, 1 } } };
static_assert(restrictions[0][0] == 0 && restrictions[2][1] == 1 && restrictions[1][0] != restrictions[1][1]);
constexpr double coordinateLimit = 0x1p100;
constexpr double knotLimit = 0x1p60;
constexpr double minimumSpan = 0x1p-30;
// N_BasisEval has 3 inner iterations at degree 2, followed by three
// homogeneous multiply/adds and projection in N_CrvEval. Even counting
// the entire basis loop as one serial path takes <64 rounded operations.
consteval double gamma(unsigned operations)
{
    return operations * epsilon / (1 - operations * epsilon);
}
static_assert(gamma(64) < 128 * epsilon);
static_assert(gamma(32) < 63 * epsilon);
bool supportedArithmetic()
{
    const auto state = _mm_getcsr();
    return std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53 && std::fegetround() == FE_TONEAREST &&
           !(state & ((1U << 15) | (1U << 6) | 0x6000U)) && (state & 0x1F80U) == 0x1F80U;
}
struct View
{
    const gw_CURVE* curve = nullptr;
    int dimension = 0, count = 0;
    bool polynomial = false;
    double maxWeight = 0, ratio = 0, scale = 0;
    double weight(int i) const
    {
        return polynomial ? 1 : curve->pol->Pw[i].w;
    }
    double coordinate(int i, int axis) const
    {
        const auto& p = curve->pol->Pw[i];
        return axis == 0 ? p.x : axis == 1 ? p.y : dimension == 2 ? 0 : p.z;
    }
};
bool view(const SmBSplineCurve& spline, View& result)
{
    const auto* raw = spline.GetGwNurbPointer();
    const auto dimension = spline.GetDim();
    if (!raw || dimension < 2 || dimension > 3 || raw->p != 2 || !raw->pol || !raw->knt || !raw->pol->Pw || !raw->knt->U || raw->pol->n < 2 ||
        raw->pol->n > 32 || raw->pol->n % 2 || raw->knt->m != raw->pol->n + 3)
    {
        return false;
    }
    View v;
    v.curve = raw;
    v.dimension = int(dimension);
    v.count = int(raw->pol->n + 1);
    v.polynomial = raw->pol->Pw[0].w == NL_NOW;
    const auto* u = raw->knt->U;
    const int end = int(raw->knt->m);
    for (int i = 0; i <= end; ++i)
    {
        if (!std::isfinite(u[i]) || std::abs(u[i]) > knotLimit)
        {
            return false;
        }
    }
    for (int i = 0; i <= 2; ++i)
    {
        if (u[i] != u[0] || u[end - i] != u[end])
        {
            return false;
        }
    }
    for (int i = 0; i < v.count - 1; i += 2)
    {
        const double a = u[i + 2], b = u[i + 3];
        if (a >= b || u[i + 4] != b || down(b - a) < minimumSpan || up(b - a) > knotLimit)
        {
            return false;
        }
    }
    double minWeight = infinity;
    v.scale = 1;
    for (int i = 0; i < v.count; ++i)
    {
        const auto& p = raw->pol->Pw[i];
        if ((p.w == NL_NOW) != v.polynomial || (dimension == 2 && p.z != 0 && p.z != NL_NOZ) || (dimension == 3 && p.z == NL_NOZ))
        {
            return false;
        }
        const double w = v.weight(i);
        // Stay far from NLib's zero-weight arithmetic and avoid under/overflow
        // in its evaluator. Global homogeneous rescalings outside this proof
        // envelope, despite representing the same mathematics, use the old path.
        if (!std::isfinite(w) || w < 0x1p-8 || w > 0x1p8)
        {
            return false;
        }
        minWeight = std::min(minWeight, w);
        v.maxWeight = std::max(v.maxWeight, w);
        for (int axis = 0; axis < int(dimension); ++axis)
        {
            const double x = v.coordinate(i, axis);
            if (!std::isfinite(x))
            {
                return false;
            }
            const double size = up(std::abs(x / w));
            if (!std::isfinite(size) || size > coordinateLimit)
            {
                return false;
            }
            v.scale = std::max(v.scale, size);
        }
    }
    v.ratio = up(v.maxWeight / minWeight);
    if (v.ratio > 4)
    {
        return false;
    }
    result = v;
    return true;
}
Quadratic restrictTo(const Quadratic& p, double a, double b)
{
    const std::array parameters{ a, b };
    auto blend = [](double x, double y, double t)
    {
        return (1 - t) * x + t * y;
    };
    Quadratic result{};
    for (unsigned i = 0; i < 3; ++i)
    {
        const double t = parameters[restrictions[i][0]], u = parameters[restrictions[i][1]];
        result[i] = blend(blend(p[0], p[1], t), blend(p[1], p[2], t), u);
    }
    return result;
}
double multiplyUp(double a, double b)
{
    return up(a * b);
}
double addUp(double a, double b)
{
    return up(a + b);
}
double roundingAllowance(const View& v)
{
    // In a separated quadratic span the basis loop uses only a<=u<=b,
    // with each positive denominator >=2^-30 (also outside the 1e-12
    // degeneracy hack in N_BasisFindSpan). Weights are in [2^-8,2^8]
    // with ratio <=4 and Euclidean pole coordinates <=2^100. No overflow,
    // zero-weight special case or near-zero-denominator failure is possible.
    // Positive basis error, three-term accumulation and rational division
    // contribute <128*epsilon*ratio^2*scale per coordinate. The 4096 envelope
    // also dominates gradual-underflow absolute terms, including basis terms
    // arbitrarily near endpoints. End-point TO_EUCLID needs only a division.
    // Three coordinates cover the Euclidean norm by its L1 upper bound.
    return multiplyUp(3, multiplyUp(multiplyUp(4096 * epsilon, multiplyUp(v.ratio, v.ratio)), v.scale));
}
#endif
} // namespace
std::optional<QuadraticReference> Prepare(const SmCurve& source)
{
    const auto type = source.GetType();
    if (type != SmBSplineCurve_TYPE && type != SmCircle_TYPE && type != SmEllipse_TYPE)
    {
        return {};
    }
#if SM_BEZIER_CERT_ARITHMETIC
    if (!supportedArithmetic())
    {
        return {};
    }
    View original;
    if (!view(static_cast<const SmBSplineCurve&>(source), original))
    {
        return {};
    }
    QuadraticReference prepared;
    prepared.dimension = original.dimension;
    prepared.count = original.count;
    prepared.rounding = roundingAllowance(original);
    prepared.scale = original.scale;
    for (int i = 0; i < original.count + 3; ++i)
    {
        prepared.knots[i] = original.curve->knt->U[i];
    }
    for (int i = 0; i < original.count; ++i)
    {
        for (int axis = 0; axis < original.dimension; ++axis)
        {
            prepared.coefficients[i][axis] = original.coordinate(i, axis) / original.maxWeight;
        }
        prepared.coefficients[i][3] = original.weight(i) / original.maxWeight;
    }
    return prepared;
#else
    return {};
#endif
}
std::optional<double> Bound(const SmCurve& source, const SmBSplineCurve& leaf, const SmExtent1d& interval, double tolerance)
{
    const auto prepared = Prepare(source);
    return prepared ? Bound(*prepared, leaf, interval, tolerance) : std::nullopt;
}
std::optional<double> Bound(const QuadraticReference& original, const SmBSplineCurve& leaf, const SmExtent1d& interval, double tolerance)
{
    if (leaf.GetType() != SmBSplineCurve_TYPE)
    {
        return {};
    }
#if SM_BEZIER_CERT_ARITHMETIC
    if (!supportedArithmetic() || !std::isfinite(tolerance) || tolerance <= 0 || tolerance > coordinateLimit)
    {
        return {};
    }
    View segment;
    if (!view(leaf, segment) || segment.count != 3 || original.dimension != segment.dimension)
    {
        return {};
    }
    const double first = interval.GetMin(), last = interval.GetMax();
    const auto* knots = original.knots.data();
    if (!std::isfinite(first) || !std::isfinite(last) || first >= last || first != segment.curve->knt->U[0] || last != segment.curve->knt->U[5])
    {
        return {};
    }
    const double rounding = addUp(original.rounding, roundingAllowance(segment));
    if (!std::isfinite(rounding) || rounding > tolerance)
    {
        return {};
    }
    int start = -1;
    for (int i = 0; i < original.count - 1; i += 2)
    {
        if (first >= knots[i + 2] && last <= knots[i + 3])
        {
            start = i;
            break;
        }
    }
    if (start < 0)
    {
        return {};
    }
    const double a = knots[start + 2], b = knots[start + 3];
    const double ta = std::clamp((first - a) / (b - a), 0., 1.), tb = std::clamp((last - a) / (b - a), 0., 1.);
    Quadratic originalWeights{};
    for (int i = 0; i < 3; ++i)
    {
        originalWeights[i] = original.coefficients[start + i][3];
    }
    const auto referenceWeights = restrictTo(originalWeights, ta, tb);
    // Any finite positive global homogeneous scale preserves the leaf's curve.
    // Align the first weight to reduce cancellation; the exact choice is not
    // trusted as an equality. Every remaining residual is still enclosed.
    const double leafScale = referenceWeights[0] / segment.weight(0);
    if (!std::isfinite(leafScale) || leafScale <= 0)
    {
        return {};
    }
    double weightDifference = 0, minimumWeight = infinity;
    for (int i = 0; i < 3; ++i)
    {
        const double residual = referenceWeights[i] - leafScale * segment.weight(i);
        weightDifference = std::max(weightDifference, std::abs(residual));
        minimumWeight = std::min(minimumWeight, referenceWeights[i]);
    }
    // Parameter conversion has absolute error <8*epsilon on [0,1]. The
    // blossom's two parameter derivatives are <=2*M, and its rounded path
    // has <16 operations; normalization adds one division. Together their
    // coefficient error is <512*epsilon*M, including gradual underflow.
    // The chosen global leaf scale is an arbitrary positive double (no
    // equality assumption). Its scaled weight is <8 by ratio<=4. Each
    // product/subtraction adds <32*epsilon*M. 4096*epsilon*M covers both
    // errors conservatively. These are coefficient bounds on the whole
    // interval; no sample or root residual can produce a certificate.
    constexpr double weightError = 4096 * epsilon;
    minimumWeight = down(minimumWeight - weightError);
    if (!std::isfinite(weightDifference) || minimumWeight <= 0)
    {
        return {};
    }
    const double coordinateError = weightError * std::max({ 1., original.scale, 8 * segment.scale });
    double coordinateDifference = 0;
    for (int axis = 0; axis < original.dimension; ++axis)
    {
        Quadratic coefficients{};
        for (int i = 0; i < 3; ++i)
        {
            coefficients[i] = original.coefficients[start + i][axis];
        }
        const auto restricted = restrictTo(coefficients, ta, tb);
        for (int i = 0; i < 3; ++i)
        {
            const double residual = restricted[i] - leafScale * segment.coordinate(i, axis);
            coordinateDifference = std::max(coordinateDifference, std::abs(residual));
        }
    }
    // R-L=(DeltaN-L*DeltaW)/W. Positive Bernstein convex hulls bound
    // DeltaN, DeltaW, L and the denominator. L1 covers Euclidean distance.
    // All final bound arithmetic is positive and normal. Multiplying by
    // 1+64*epsilon and rounding upward dominates its <32 rounded operations.
    const double total = up(
        (rounding +
         original.dimension * ((coordinateDifference + coordinateError) + segment.scale * (weightDifference + weightError)) / minimumWeight) *
        (1 + 64 * epsilon)
    );
    if (!std::isfinite(total) || total > tolerance)
    {
        return {};
    }
    return total;
#else
    (void)original;
    (void)leaf;
    (void)interval;
    (void)tolerance;
    return {};
#endif
}
} // namespace sm_bezier_deviation
#if defined(_MSC_VER) && SM_BEZIER_CERT_ARITHMETIC
#    pragma float_control(pop)
#endif
