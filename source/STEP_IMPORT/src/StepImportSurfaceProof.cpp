// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportSurfaceProof.h"

#include "StepImportProofEnvironment.h"

#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmLine.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace step_import::detail
{
namespace
{
struct Unestablished
{
};
struct WorkLimit
{
};
double Down(double x)
{
    if (!std::isfinite(x))
    {
        throw Unestablished{};
    }
    const double result = std::nextafter(x, -std::numeric_limits<double>::infinity());
    if (!std::isfinite(result))
    {
        throw Unestablished{};
    }
    return result;
}
double Up(double x)
{
    if (!std::isfinite(x))
    {
        throw Unestablished{};
    }
    const double result = std::nextafter(x, std::numeric_limits<double>::infinity());
    if (!std::isfinite(result))
    {
        throw Unestablished{};
    }
    return result;
}
struct I
{
    double lo = 0, hi = 0;
    I() = default;
    explicit I(double x) : lo(x), hi(x)
    {
        if (!std::isfinite(x))
        {
            throw Unestablished{};
        }
    }
    I(double l, double h) : lo(l), hi(h)
    {
        if (!std::isfinite(l) || !std::isfinite(h) || l > h)
        {
            throw Unestablished{};
        }
    }
    bool Zero() const
    {
        return lo == 0 && hi == 0;
    }
};
I operator+(I a, I b)
{
    if (a.Zero())
    {
        return b;
    }
    if (b.Zero())
    {
        return a;
    }
    return { Down(a.lo + b.lo), Up(a.hi + b.hi) };
}
I operator-(I a, I b)
{
    if (b.Zero())
    {
        return a;
    }
    return { Down(a.lo - b.hi), Up(a.hi - b.lo) };
}
I operator*(I a, I b)
{
    if (a.Zero() || b.Zero())
    {
        return {};
    }
    const std::array v{ a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi };
    for (double x : v)
    {
        if (!std::isfinite(x))
        {
            throw Unestablished{};
        }
    }
    return { Down(*std::min_element(v.begin(), v.end())), Up(*std::max_element(v.begin(), v.end())) };
}
I operator/(I a, I b)
{
    if (!(b.lo > 0))
    {
        throw Unestablished{};
    }
    return a * I(Down(1 / b.hi), Up(1 / b.lo));
}
using C = std::array<I, 4>;
struct Net
{
    unsigned p = 0, q = 0;
    std::size_t nu = 0, nv = 0;
    bool polynomial = false;
    std::vector<double> u, v;
    std::vector<C> cp;
};
struct Budget
{
    std::size_t controls, products, used = 0;
    const ExecutionCheckpoint& checkpoint;
    void ControlCount(std::size_t u, std::size_t v) const
    {
        if (u == 0 || v == 0 || u > controls / v)
        {
            throw WorkLimit{};
        }
    }
    [[nodiscard]] Result<void> Product()
    {
        if (used == products)
        {
            throw WorkLimit{};
        }
        ++used;
        if ((used & 255) == 1)
        {
            if (auto operation = checkpoint(); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
        }
        return {};
    }
};
std::vector<double> Knots(const SmBSplineSurface& s, SmSurfParamType axis)
{
    SmTArray<double> values;
    SmTArray<ULONG> mult;
    if (s.GetKnots(axis, values, &mult) != SM_SUCCESS)
    {
        throw Unestablished{};
    }
    std::vector<double> out;
    for (ULONG i = 0; i < values.GetSize(); ++i)
    {
        if (!std::isfinite(values[i]) || (i && !(values[i] > values[i - 1])))
        {
            throw Unestablished{};
        }
        if (mult[i] == 0 || mult[i] > 33)
        {
            throw Unestablished{};
        }
        out.insert(out.end(), mult[i], values[i]);
    }
    return out;
}
Net Read(const SmBSplineSurface& s, Budget& budget)
{
    Net out;
    out.p = s.GetDegree(SM_SP_U);
    out.q = s.GetDegree(SM_SP_V);
    if (out.p == 0 || out.q == 0 || out.p > 32 || out.q > 32)
    {
        throw Unestablished{};
    }
    ULONG nu = 0, nv = 0;
    double* data = nullptr;
    if (s.GetControlPointsPointer(nu, nv, data) != SM_SUCCESS || !data)
    {
        throw Unestablished{};
    }
    budget.ControlCount(nu, nv);
    out.nu = nu;
    out.nv = nv;
    out.polynomial = !s.IsRational();
    out.u = Knots(s, SM_SP_U);
    out.v = Knots(s, SM_SP_V);
    if (out.u.size() != out.nu + out.p + 1 || out.v.size() != out.nv + out.q + 1)
    {
        throw Unestablished{};
    }
    const auto validMultiplicity = [](const std::vector<double>& knots, unsigned degree)
    {
        std::size_t repeated = 0;
        double previous = 0;
        for (double knot : knots)
        {
            repeated = repeated && knot == previous ? repeated + 1 : 1;
            if (repeated > degree + 1)
            {
                return false;
            }
            previous = knot;
        }
        return true;
    };
    if (!validMultiplicity(out.u, out.p) || !validMultiplicity(out.v, out.q))
    {
        throw Unestablished{};
    }
    const auto clamped = [](const std::vector<double>& k, unsigned degree)
    {
        for (unsigned i = 0; i <= degree; ++i)
        {
            if (k[i] != k[degree] || k[k.size() - 1 - i] != k[k.size() - 1 - degree])
            {
                return false;
            }
        }
        return k[degree] < k[k.size() - 1 - degree];
    };
    if (!clamped(out.u, out.p) || !clamped(out.v, out.q))
    {
        throw Unestablished{};
    }
    out.cp.reserve(out.nu * out.nv);
    for (std::size_t i = 0; i < out.nu * out.nv; ++i)
    {
        const double w = out.polynomial ? 1. : data[4 * i + 3];
        if (!(w > 0))
        {
            throw Unestablished{};
        }
        out.cp.push_back({ I(data[4 * i]), I(data[4 * i + 1]), I(data[4 * i + 2]), I(w) });
    }
    return out;
}
void NaturalDomain(const SmBSplineSurface& surface, const Net& net)
{
    const auto domain = surface.GetNaturalUVDomain();
    if (domain.GetMin().x != net.u[net.p] || domain.GetMax().x != net.u[net.nu] || domain.GetMin().y != net.v[net.q] ||
        domain.GetMax().y != net.v[net.nv])
    {
        throw Unestablished{};
    }
}
[[nodiscard]] Result<std::vector<C>> InsertCurve(
    const std::vector<C>& cp,
    const std::vector<double>& knots,
    unsigned p,
    double value,
    bool polynomial,
    Budget& budget
)
{
    const auto k = static_cast<std::size_t>(std::upper_bound(knots.begin(), knots.end(), value) - knots.begin() - 1);
    const auto s = static_cast<std::size_t>(std::count(knots.begin(), knots.end(), value));
    if (s >= p || k < p || k < s || k - s >= cp.size())
    {
        throw Unestablished{};
    }
    std::vector<C> result(cp.size() + 1);
    for (std::size_t i = 0; i <= k - p; ++i)
    {
        result[i] = cp[i];
    }
    for (std::size_t i = k - s; i < cp.size(); ++i)
    {
        result[i + 1] = cp[i];
    }
    for (std::size_t i = k - p + 1; i <= k - s; ++i)
    {
        if (auto operation = budget.Product(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const I alpha = (I(value) - I(knots[i])) / (I(knots[i + p]) - I(knots[i]));
        for (std::size_t axis = 0; axis < 4; ++axis)
        {
            result[i][axis] = (I(1) - alpha) * cp[i - 1][axis] + alpha * cp[i][axis];
        }
        // A polynomial's weight is exactly one by construction; do not invent
        // uncertainty in that known invariant while refining its XYZ controls.
        if (polynomial)
        {
            result[i][3] = I(1);
        }
    }
    return result;
}
[[nodiscard]] Result<void> Refine(Net& net, bool uAxis, const std::vector<double>& values, Budget& budget)
{
    auto& knots = uAxis ? net.u : net.v;
    const unsigned p = uAxis ? net.p : net.q;
    for (double value : values)
    {
        if (value <= knots[p] || value >= knots[knots.size() - 1 - p])
        {
            continue;
        }
        while (static_cast<unsigned>(std::count(knots.begin(), knots.end(), value)) < p)
        {
            if (auto operation = budget.checkpoint(); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto newU = net.nu + (uAxis ? 1 : 0), newV = net.nv + (uAxis ? 0 : 1);
            budget.ControlCount(newU, newV);
            std::vector<C> result(newU * newV);
            const auto curves = uAxis ? net.nv : net.nu;
            const auto length = uAxis ? net.nu : net.nv;
            for (std::size_t row = 0; row < curves; ++row)
            {
                std::vector<C> cp;
                cp.reserve(length);
                for (std::size_t i = 0; i < length; ++i)
                {
                    cp.push_back(uAxis ? net.cp[i * net.nv + row] : net.cp[row * net.nv + i]);
                }
                auto insertedResult = InsertCurve(cp, knots, p, value, net.polynomial, budget);
                if (!insertedResult)
                {
                    return tl::unexpected(std::move(insertedResult.error()));
                }
                const auto inserted = std::move(*insertedResult);
                for (std::size_t i = 0; i < inserted.size(); ++i)
                {
                    result[uAxis ? i * newV + row : row * newV + i] = inserted[i];
                }
            }
            const auto position = std::upper_bound(knots.begin(), knots.end(), value);
            knots.insert(position, value);
            net.nu = newU;
            net.nv = newV;
            net.cp = std::move(result);
        }
    }
    return {};
}
std::vector<double> Union(std::vector<double> a, const std::vector<double>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    std::sort(a.begin(), a.end());
    a.erase(std::unique(a.begin(), a.end()), a.end());
    return a;
}
using Choose = std::array<std::array<std::uint64_t, 65>, 65>;
Choose Binomials()
{
    Choose result{};
    for (std::size_t n = 0; n < result.size(); ++n)
    {
        result[n][0] = result[n][n] = 1;
        for (std::size_t i = 1; i < n; ++i)
        {
            result[n][i] = result[n - 1][i - 1] + result[n - 1][i];
        }
    }
    return result;
}
I Integer(std::uint64_t x)
{
    const double d = static_cast<double>(x);
    if (x <= (std::uint64_t(1) << 53))
    {
        return I(d);
    }
    return { Down(d), Up(d) };
}
double Magnitude(I x)
{
    return std::max(std::abs(x.lo), std::abs(x.hi));
}
[[nodiscard]] Result<double>
PatchBound(const Net& a, const Net& b, std::size_t au, std::size_t av, std::size_t bu, std::size_t bv, Budget& budget, const Choose& choose)
{
    const unsigned p = a.p, q = a.q;
    const auto control = [](const Net& n, std::size_t u, std::size_t v, unsigned i, unsigned j) -> const C&
    {
        return n.cp[(u - n.p + i) * n.nv + v - n.q + j];
    };
    std::array<double, 3> maximum{};
    double minA = INFINITY, minB = INFINITY;
    for (unsigned i = 0; i <= p; ++i)
    {
        for (unsigned j = 0; j <= q; ++j)
        {
            minA = std::min(minA, control(a, au, av, i, j)[3].lo);
            minB = std::min(minB, control(b, bu, bv, i, j)[3].lo);
        }
    }
    if (!(minA > 0) || !(minB > 0))
    {
        throw Unestablished{};
    }
    if (a.polynomial && b.polynomial)
    {
        for (unsigned i = 0; i <= p; ++i)
        {
            for (unsigned j = 0; j <= q; ++j)
            {
                if (auto operation = budget.Product(); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                const auto& x = control(a, au, av, i, j);
                const auto& y = control(b, bu, bv, i, j);
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    maximum[axis] = std::max(maximum[axis], Magnitude(x[axis] - y[axis]));
                }
            }
        }
    }
    else
    {
        // Bernstein product coefficients of X*V-Y*W; positive control weights
        // bound W*V below throughout this whole tensor patch.
        for (unsigned r = 0; r <= 2 * p; ++r)
        {
            for (unsigned s = 0; s <= 2 * q; ++s)
            {
                std::array<I, 3> sum{};
                for (unsigned i = r > p ? r - p : 0; i <= std::min(p, r); ++i)
                {
                    for (unsigned j = s > q ? s - q : 0; j <= std::min(q, s); ++j)
                    {
                        if (auto operation = budget.Product(); !operation)
                        {
                            return tl::unexpected(std::move(operation.error()));
                        }
                        const unsigned k = r - i, l = s - j;
                        const I coefficient = (Integer(choose[p][i]) * Integer(choose[p][k]) / Integer(choose[2 * p][r])) *
                                              (Integer(choose[q][j]) * Integer(choose[q][l]) / Integer(choose[2 * q][s]));
                        const auto& x = control(a, au, av, i, j);
                        const auto& y = control(b, bu, bv, k, l);
                        for (unsigned axis = 0; axis < 3; ++axis)
                        {
                            sum[axis] = sum[axis] + coefficient * (x[axis] * y[3] - y[axis] * x[3]);
                        }
                    }
                }
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    maximum[axis] = std::max(maximum[axis], Magnitude(sum[axis]));
                }
            }
        }
        const double denominator = Down(minA * minB);
        if (!(denominator > 0))
        {
            throw Unestablished{};
        }
        for (auto& x : maximum)
        {
            x = Up(x / denominator);
        }
    }
    // L1 encloses Euclidean distance without relying on a libm square-root
    // accuracy assumption. Every addition is rounded outward explicitly.
    double bound = 0;
    for (double x : maximum)
    {
        bound = Up(bound + x);
    }
    return bound;
}
std::size_t Span(const std::vector<double>& knots, unsigned p, std::size_t count, double begin, double end)
{
    for (std::size_t i = p; i < count; ++i)
    {
        if (knots[i] == begin && knots[i + 1] == end)
        {
            return i;
        }
    }
    throw Unestablished{};
}
} // namespace
[[nodiscard]] Result<SurfaceRestrictionProof> ProveSurfaceRestriction(
    const SmBSplineSurface& original,
    const SmBSplineSurface& restricted,
    double maximumMm,
    std::size_t maximumControls,
    std::size_t maximumProducts,
    const ExecutionCheckpoint& checkpoint
)
{
    SurfaceRestrictionProof result;
    if (!checkpoint || !std::isfinite(maximumMm) || !(maximumMm > 0) || !maximumControls || !maximumProducts || !SurfaceProofNumericEnvironment())
    {
        return result;
    }
    // FE_TONEAREST alone does not rule out x86 DAZ/FTZ. Inspect integer bits of
    // both a subnormal input operation and an inexact subnormal result; refusal
    // is preferable to an interval bound weakened by flushed underflow.
    const ExecutionCheckpoint checkedCheckpoint = [&]() -> Result<void>
    {
        if (auto operation = checkpoint(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        } // Caller exceptions remain execution/cancellation failures.
        if (!SurfaceProofNumericEnvironment())
        {
            throw Unestablished{};
        }
        return {};
    };
    Budget budget{ maximumControls, maximumProducts, 0, checkedCheckpoint };
    try
    {
        if (auto checked = checkedCheckpoint(); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        Net a = Read(original, budget), b = Read(restricted, budget);
        if (a.p != b.p || a.q != b.q)
        {
            return result;
        }
        const std::array domain{ b.u[b.p], b.u[b.nu], b.v[b.q], b.v[b.nv] };
        if (domain[0] < a.u[a.p] || domain[1] > a.u[a.nu] || domain[2] < a.v[a.q] || domain[3] > a.v[a.nv])
        {
            return result;
        }
        const auto u = Union(a.u, b.u), v = Union(a.v, b.v);
        if (auto operation = Refine(a, true, u, budget); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto operation = Refine(a, false, v, budget); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto operation = Refine(b, true, u, budget); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto operation = Refine(b, false, v, budget); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const auto choose = Binomials();
        double maximum = 0;
        for (std::size_t i = a.p; i < a.nu; ++i)
        {
            if (a.u[i] < a.u[i + 1] && a.u[i] >= domain[0] && a.u[i + 1] <= domain[1])
            {
                for (std::size_t j = a.q; j < a.nv; ++j)
                {
                    if (a.v[j] < a.v[j + 1] && a.v[j] >= domain[2] && a.v[j + 1] <= domain[3])
                    {
                        if (auto checked = checkedCheckpoint(); !checked)
                        {
                            return tl::unexpected(std::move(checked.error()));
                        }
                        const auto bi = Span(b.u, b.p, b.nu, a.u[i], a.u[i + 1]);
                        const auto bj = Span(b.v, b.q, b.nv, a.v[j], a.v[j + 1]);
                        auto bound = PatchBound(a, b, i, j, bi, bj, budget, choose);
                        if (!bound)
                        {
                            return tl::unexpected(std::move(bound.error()));
                        }
                        maximum = std::max(maximum, *bound);
                        ++result.patches;
                    }
                }
            }
        }
        if (result.patches == 0)
        {
            return result;
        }
        result.maximumMm = maximum;
        result.established = maximum <= Down(maximumMm);
    }
    catch (const WorkLimit&)
    {
        result.workLimit = true;
    }
    catch (const Unestablished&)
    {
    }
    return result;
}
namespace
{
using V3 = std::array<I, 3>;
I Dot(const V3& v, const std::array<double, 3>& row)
{
    I result;
    for (std::size_t k = 0; k < 3; ++k)
    {
        result = result + v[k] * I(row[k]);
    }
    return result;
}
void Include(I& range, I value, bool first)
{
    range = first ? value : I(std::min(range.lo, value.lo), std::max(range.hi, value.hi));
}
void Continuous(const std::vector<double>& knots, unsigned degree, std::size_t count)
{
    if (count <= degree)
    {
        throw Unestablished{};
    }
    for (std::size_t i = degree + 1; i < count;)
    {
        std::size_t next = i + 1;
        while (next < knots.size() && knots[next] == knots[i])
        {
            ++next;
        }
        if (next - i > degree)
        {
            throw Unestablished{};
        }
        i = next;
    }
}
ExecutionCheckpoint Checked(const ExecutionCheckpoint& checkpoint)
{
    return [checkpoint]() -> Result<void>
    {
        if (auto operation = checkpoint(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (!SurfaceProofNumericEnvironment())
        {
            throw Unestablished{};
        }
        return {};
    };
}
struct CurveNet
{
    unsigned degree = 0;
    bool polynomial = false;
    std::vector<double> knots;
    std::vector<C> cp;
    bool analyticLine = false;
    V3 lineOrigin{}, lineSlope{};
    double lineBase = 0, lineStepBegin = 0;
};
[[nodiscard]] Result<CurveNet> ReadCurve(const SmCurve& curve, Budget& budget)
{
    // Analytic conics can expose a NURBS base with a different evaluator
    // parameter. Reading that base as curve(t) would be a false certificate.
    if (curve.GetType() != SmBSplineCurve_TYPE && curve.GetType() != SmLine_TYPE)
    {
        throw Unestablished{};
    }
    const auto* spline = dynamic_cast<const SmBSplineCurve*>(&curve);
    if (!spline || (curve.GetDim() != 2 && curve.GetDim() != 3))
    {
        throw Unestablished{};
    }
    CurveNet net;
    net.degree = spline->GetDegree();
    net.polynomial = !spline->IsRational();
    ULONG count = 0;
    double* data = nullptr;
    SmTArray<double> knots;
    if (!net.degree || net.degree > 32 || spline->GetControlPointsPointer(count, data) != SM_SUCCESS || !data ||
        spline->GetKnotsAll(knots) != SM_SUCCESS)
    {
        throw Unestablished{};
    }
    budget.ControlCount(count, 1);
    net.knots.assign(knots.GetDataArray(), knots.GetDataArray() + knots.GetSize());
    if (net.knots.size() != count + net.degree + 1 ||
        !std::all_of(
            net.knots.begin(),
            net.knots.end(),
            [](double t)
            {
                return std::isfinite(t);
            }
        ) ||
        !std::is_sorted(net.knots.begin(), net.knots.end()))
    {
        throw Unestablished{};
    }
    Continuous(net.knots, net.degree, count);
    const auto domain = curve.GetNaturalInterval();
    if (domain.GetMin() != net.knots[net.degree] || domain.GetMax() != net.knots[count] || !(domain.GetMax() > domain.GetMin()))
    {
        throw Unestablished{};
    }
    for (unsigned i = 0; i <= net.degree; ++i)
    {
        if (net.knots[i] != domain.GetMin() || net.knots[net.knots.size() - 1 - i] != domain.GetMax())
        {
            throw Unestablished{};
        }
    }
    net.cp.reserve(count);
    for (ULONG i = 0; i < count; ++i)
    {
        if (auto operation = budget.Product(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const auto w = net.polynomial ? 1. : data[4 * i + 3];
        if (!(w > 0))
        {
            throw Unestablished{};
        }
        net.cp.push_back({ I(data[4 * i]), I(data[4 * i + 1]), curve.GetDim() == 2 ? I() : I(data[4 * i + 2]), I(w) });
    }
    if (const auto line = dynamic_cast<const SmLine*>(&curve))
    {
        // SmLine::EvaluatePoint uses its analytic affine definition, not the
        // rounded endpoints of its NURBS cache. Enclose those actual operands
        // and the native->STEP map explicitly; no parameter identity is guessed.
        const auto step = line->GetSTEPInterval();
        if (!line->IsBounded() || !std::isfinite(domain.GetLength()) || SM_IS_ZERO(domain.GetLength()) || !std::isfinite(step.GetMin()) ||
            !std::isfinite(step.GetLength()) || !(step.GetLength() > 0))
        {
            throw Unestablished{};
        }
        net.analyticLine = true;
        net.lineBase = line->GetInsideOut() ? domain.GetMax() : domain.GetMin();
        net.lineStepBegin = step.GetMin();
        const auto origin = line->GetLinePoint();
        const auto direction = line->GetLineVector();
        const std::array p{ origin.x, origin.y, origin.z }, d{ direction.x, direction.y, direction.z };
        I slope = step.GetLength() == domain.GetLength() ? I(1) : I(step.GetLength()) / I(domain.GetLength());
        if (line->GetInsideOut())
        {
            slope = I(-slope.hi, -slope.lo);
        }
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const I vector = axis == 2 && curve.GetDim() == 2 ? I() :
                             line->GetLineScale() == 1        ? I(d[axis]) :
                             d[axis] == 1                     ? I(line->GetLineScale()) :
                                                                I(line->GetLineScale()) * I(d[axis]);
            net.lineOrigin[axis] = (axis == 2 && curve.GetDim() == 2 ? I() : I(p[axis])) + vector * I(net.lineStepBegin);
            net.lineSlope[axis] = slope.lo == 1 && slope.hi == 1 ? vector : vector * slope;
        }
    }
    return net;
}
[[nodiscard]] Result<C>
DeBoor(std::vector<C> work, const std::vector<double>& knots, unsigned degree, std::size_t span, I t, bool polynomial, Budget& budget)
{
    C hull = work[0];
    for (std::size_t j = 1; j < work.size(); ++j)
    {
        for (std::size_t axis = 0; axis < 4; ++axis)
        {
            Include(hull[axis], work[j][axis], false);
        }
    }
    for (unsigned level = 1; level <= degree; ++level)
    {
        for (unsigned j = degree; j >= level; --j)
        {
            if (auto operation = budget.Product(); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto i = span - degree + j;
            const I alpha = (t - I(knots[i])) / (I(knots[i + degree + 1 - level]) - I(knots[i]));
            for (std::size_t axis = 0; axis < 4; ++axis)
            {
                work[j][axis] = (I(1) - alpha) * work[j - 1][axis] + alpha * work[j][axis];
            }
            if (polynomial)
            {
                work[j][3] = I(1);
            }
        }
    }
    C result = work[degree];
    // Nonnegative B-spline basis weights give a second, independent enclosure.
    // Intersection preserves exact constant/zero boundary coordinates instead
    // of inventing a small parameter excursion from arithmetic roundoff alone.
    for (std::size_t axis = 0; axis < 4; ++axis)
    {
        result[axis] = I(std::max(result[axis].lo, hull[axis].lo), std::min(result[axis].hi, hull[axis].hi));
    }
    return result;
}
std::size_t CurveSpan(const CurveNet& net, double t)
{
    auto index = static_cast<std::size_t>(std::upper_bound(net.knots.begin(), net.knots.end(), t) - net.knots.begin() - 1);
    return std::clamp(index, static_cast<std::size_t>(net.degree), net.cp.size() - 1);
}
[[nodiscard]] Result<C> CurveRange(const CurveNet& net, I t, Budget& budget)
{
    if (net.analyticLine)
    {
        C result;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            result[axis] = net.lineOrigin[axis] + net.lineSlope[axis] * (t - I(net.lineBase));
        }
        result[3] = I(1);
        return result;
    }
    const auto span = CurveSpan(net, std::midpoint(t.lo, t.hi));
    if (t.lo < net.knots[span] || t.hi > net.knots[span + 1])
    {
        throw Unestablished{};
    }
    return DeBoor({ net.cp.begin() + span - net.degree, net.cp.begin() + span + 1 }, net.knots, net.degree, span, t, net.polynomial, budget);
}
I DivideWeight(I value, I weight)
{
    return weight.lo == 1 && weight.hi == 1 ? value : value / weight;
}
[[nodiscard]] Result<CurveNet> Derivative(const CurveNet& source, Budget& budget)
{
    CurveNet out;
    if (source.analyticLine)
    {
        out.knots = { source.knots[source.degree], source.knots[source.cp.size()] };
        out.cp.push_back({ source.lineSlope[0], source.lineSlope[1], source.lineSlope[2], I() });
        return out;
    }
    out.degree = source.degree - 1;
    // These are homogeneous derivatives, with w'=0 for polynomial curves.
    // Do not apply the point evaluator's invariant w=1 to derivative controls.
    out.knots.assign(source.knots.begin() + 1, source.knots.end() - 1);
    for (std::size_t i = 0; i + 1 < source.cp.size(); ++i)
    {
        if (auto operation = budget.Product(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const I factor = I(source.degree) / (I(source.knots[i + source.degree + 1]) - I(source.knots[i + 1]));
        C value;
        for (std::size_t axis = 0; axis < 4; ++axis)
        {
            value[axis] = (source.cp[i + 1][axis] - source.cp[i][axis]) * factor;
        }
        out.cp.push_back(value);
    }
    return out;
}
[[nodiscard]] Result<V3> EuclideanDerivative(const CurveNet& source, const CurveNet& derivative, I t, Budget& budget)
{
    auto value = CurveRange(source, t, budget);
    if (!value)
    {
        return tl::unexpected(std::move(value.error()));
    }
    auto tangent = CurveRange(derivative, t, budget);
    if (!tangent)
    {
        return tl::unexpected(std::move(tangent.error()));
    }
    V3 result;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        result[axis] = source.polynomial ? (*tangent)[axis] :
                                           ((*tangent)[axis] * (*value)[3] - (*value)[axis] * (*tangent)[3]) / ((*value)[3] * (*value)[3]);
    }
    return result;
}
[[nodiscard]] Result<Net> Derivative(const Net& source, bool uAxis, Budget& budget)
{
    Net out;
    out.polynomial = true;
    out.nu = source.nu - (uAxis ? 1 : 0);
    out.nv = source.nv - (uAxis ? 0 : 1);
    out.p = source.p - (uAxis ? 1 : 0);
    out.q = source.q - (uAxis ? 0 : 1);
    out.u = source.u;
    out.v = source.v;
    auto& knots = uAxis ? out.u : out.v;
    knots.erase(knots.begin());
    knots.pop_back();
    out.cp.reserve(out.nu * out.nv);
    const auto& original = uAxis ? source.u : source.v;
    const auto degree = uAxis ? source.p : source.q;
    for (std::size_t i = 0; i < out.nu; ++i)
    {
        for (std::size_t j = 0; j < out.nv; ++j)
        {
            if (auto operation = budget.Product(); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto k = uAxis ? i : j;
            const I factor = I(degree) / (I(original[k + degree + 1]) - I(original[k + 1]));
            C value;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                value[axis] = (source.cp[(i + (uAxis ? 1 : 0)) * source.nv + j + (uAxis ? 0 : 1)][axis] - source.cp[i * source.nv + j][axis]) *
                              factor;
            }
            value[3] = I(1);
            out.cp.push_back(value);
        }
    }
    return out;
}
[[nodiscard]] Result<V3> SurfaceRange(const Net& net, I u, I v, Budget& budget)
{
    if (u.lo < net.u[net.p] || u.hi > net.u[net.nu] || v.lo < net.v[net.q] || v.hi > net.v[net.nv])
    {
        throw Unestablished{};
    }
    V3 result{};
    bool first = true;
    for (std::size_t ku = net.p; ku < net.nu; ++ku)
    {
        if (!(net.u[ku] < net.u[ku + 1]) || net.u[ku] > u.hi || net.u[ku + 1] < u.lo)
        {
            continue;
        }
        const I us(std::max(u.lo, net.u[ku]), std::min(u.hi, net.u[ku + 1]));
        for (std::size_t kv = net.q; kv < net.nv; ++kv)
        {
            if (!(net.v[kv] < net.v[kv + 1]) || net.v[kv] > v.hi || net.v[kv + 1] < v.lo)
            {
                continue;
            }
            std::vector<C> column;
            for (std::size_t j = kv - net.q; j <= kv; ++j)
            {
                std::vector<C> row;
                for (std::size_t i = ku - net.p; i <= ku; ++i)
                {
                    row.push_back(net.cp[i * net.nv + j]);
                }
                auto rowResult = DeBoor(std::move(row), net.u, net.p, ku, us, true, budget);
                if (!rowResult)
                {
                    return tl::unexpected(std::move(rowResult.error()));
                }
                column.push_back(std::move(*rowResult));
            }
            auto valueResult = DeBoor(std::move(column), net.v, net.q, kv, I(std::max(v.lo, net.v[kv]), std::min(v.hi, net.v[kv + 1])), true, budget);
            if (!valueResult)
            {
                return tl::unexpected(std::move(valueResult.error()));
            }
            const auto value = std::move(*valueResult);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                Include(result[axis], value[axis], first);
            }
            first = false;
        }
    }
    if (first)
    {
        throw Unestablished{};
    }
    return result;
}
} // namespace

[[nodiscard]] Result<SurfaceInjectivityProof> ProveSurfaceInjectivity(
    const SmBSplineSurface& surface,
    std::size_t maximumControls,
    std::size_t maximumProducts,
    const ExecutionCheckpoint& checkpoint
)
{
    SurfaceInjectivityProof result;
    if (!checkpoint || !maximumControls || !maximumProducts || surface.GetType() != SmBSplineSurface_TYPE || !SurfaceProofNumericEnvironment())
    {
        return result;
    }
    const auto check = Checked(checkpoint);
    Budget budget{ maximumControls, maximumProducts, 0, check };
    const auto finish = [&]
    {
        result.productsUsed = budget.used;
        return result;
    };
    try
    {
        if (auto operation = check(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const auto net = Read(surface, budget);
        NaturalDomain(surface, net);
        if (!net.polynomial)
        {
            return finish();
        }
        Continuous(net.u, net.p, net.nu);
        Continuous(net.v, net.q, net.nv);
        if ((net.nu - 1) * net.nv > maximumControls - net.cp.size() ||
            net.nu * (net.nv - 1) > maximumControls - net.cp.size() - (net.nu - 1) * net.nv)
        {
            throw WorkLimit{};
        }
        result.domain = { net.u[net.p], net.u[net.nu], net.v[net.q], net.v[net.nv] };
        std::vector<V3> du, dv;
        const auto derivative = [&](bool uAxis, std::vector<V3>& values) -> Result<void>
        {
            const auto countU = net.nu - (uAxis ? 1 : 0), countV = net.nv - (uAxis ? 0 : 1);
            const auto& knots = uAxis ? net.u : net.v;
            const auto degree = uAxis ? net.p : net.q;
            const auto count = uAxis ? net.nu : net.nv;
            for (std::size_t i = 0; i < countU; ++i)
            {
                for (std::size_t j = 0; j < countV; ++j)
                {
                    if (auto operation = budget.Product(); !operation)
                    {
                        return tl::unexpected(std::move(operation.error()));
                    }
                    const auto k = uAxis ? i : j;
                    const I factor = I(degree) * (I(knots[count]) - I(knots[degree])) / (I(knots[k + degree + 1]) - I(knots[k + 1]));
                    V3 value;
                    for (std::size_t axis = 0; axis < 3; ++axis)
                    {
                        value[axis] = (net.cp[(i + (uAxis ? 1 : 0)) * net.nv + j + (uAxis ? 0 : 1)][axis] - net.cp[i * net.nv + j][axis]) * factor;
                    }
                    values.push_back(value);
                }
            }
            return {};
        };
        if (auto differentiated = derivative(true, du); !differentiated)
        {
            return tl::unexpected(std::move(differentiated.error()));
        }
        if (auto differentiated = derivative(false, dv); !differentiated)
        {
            return tl::unexpected(std::move(differentiated.error()));
        }
        std::array<double, 3> a{}, b{};
        for (const auto& v : du)
        {
            for (std::size_t k = 0; k < 3; ++k)
            {
                a[k] += std::midpoint(v[k].lo, v[k].hi) / static_cast<double>(du.size());
            }
        }
        for (const auto& v : dv)
        {
            for (std::size_t k = 0; k < 3; ++k)
            {
                b[k] += std::midpoint(v[k].lo, v[k].hi) / static_cast<double>(dv.size());
            }
        }
        double scale = 0;
        for (std::size_t k = 0; k < 3; ++k)
        {
            scale = std::max({ scale, std::abs(a[k]), std::abs(b[k]) });
        }
        if (!(scale > 0) || !std::isfinite(scale))
        {
            return finish();
        }
        double aa = 0, ab = 0, bb = 0;
        for (std::size_t k = 0; k < 3; ++k)
        {
            a[k] /= scale;
            b[k] /= scale;
            aa += a[k] * a[k];
            ab += a[k] * b[k];
            bb += b[k] * b[k];
        }
        const double determinant = aa * bb - ab * ab;
        if (!(determinant > 0) || !std::isfinite(determinant))
        {
            return finish();
        }
        for (std::size_t k = 0; k < 3; ++k)
        {
            result.projection[0][k] = (bb * a[k] - ab * b[k]) / determinant / scale;
            result.projection[1][k] = (aa * b[k] - ab * a[k]) / determinant / scale;
        }
        // The approximate inverse above only chooses a fixed binary64 map L.
        // The certificate below treats its coefficients as exact operands.
        // Convex derivative-control hulls enclose J(L*S) on EVERY knot cell.
        // C0 continuity plus a uniformly positive symmetric part gives global
        // strong monotonicity on the convex rectangle, hence injectivity of S.
        I j00, j10, j01, j11;
        bool first = true;
        for (const auto& v : du)
        {
            if (auto operation = budget.Product(); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            Include(j00, Dot(v, result.projection[0]), first);
            Include(j10, Dot(v, result.projection[1]), first);
            first = false;
        }
        first = true;
        for (const auto& v : dv)
        {
            if (auto operation = budget.Product(); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            Include(j01, Dot(v, result.projection[0]), first);
            Include(j11, Dot(v, result.projection[1]), first);
            first = false;
        }
        const I off = (j01 + j10) * I(.5);
        const double radius = std::max(std::abs(off.lo), std::abs(off.hi));
        result.firstDiagonalLower = j00.lo;
        result.secondDiagonalLower = j11.lo;
        result.symmetricDeterminantLower = (I(j00.lo) * I(j11.lo) - I(radius) * I(radius)).lo;
        result.established = j00.lo > 0 && j11.lo > 0 && *result.symmetricDeterminantLower > 0;
    }
    catch (const WorkLimit&)
    {
        result.workLimit = true;
    }
    catch (const Unestablished&)
    {
    }
    return finish();
}

[[nodiscard]] Result<CurveLiftProof> ProveCurveLift(
    const SmCurve& edge,
    const SmCurve& uv,
    const SmBSplineSurface& surface,
    double begin,
    double end,
    double maximumMm,
    std::size_t maximumControls,
    std::size_t maximumProducts,
    const ExecutionCheckpoint& checkpoint
)
{
    CurveLiftProof result;
    if (!checkpoint || !maximumControls || !maximumProducts || !std::isfinite(begin) || !std::isfinite(end) || !(end > begin) ||
        !std::isfinite(maximumMm) || !(maximumMm > 0) || surface.GetType() != SmBSplineSurface_TYPE || !SurfaceProofNumericEnvironment())
    {
        return result;
    }
    const auto check = Checked(checkpoint);
    Budget budget{ maximumControls, maximumProducts, 0, check };
    const auto finish = [&]
    {
        result.productsUsed = budget.used;
        return result;
    };
    try
    {
        if (auto operation = check(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const auto net = Read(surface, budget);
        NaturalDomain(surface, net);
        if (!net.polynomial || edge.GetDim() != 3 || uv.GetDim() != 2)
        {
            return finish();
        }
        auto curveResult = ReadCurve(edge, budget);
        if (!curveResult)
        {
            return tl::unexpected(std::move(curveResult.error()));
        }
        const auto curve = std::move(*curveResult);
        auto trimResult = ReadCurve(uv, budget);
        if (!trimResult)
        {
            return tl::unexpected(std::move(trimResult.error()));
        }
        const auto trim = std::move(*trimResult);
        std::size_t total = 0;
        for (const auto count :
             { net.cp.size(), (net.nu - 1) * net.nv, net.nu * (net.nv - 1), curve.cp.size(), trim.cp.size(), curve.cp.size() - 1, trim.cp.size() - 1 })
        {
            if (count > maximumControls - total)
            {
                throw WorkLimit{};
            }
            total += count;
        }
        auto dcResult = Derivative(curve, budget);
        if (!dcResult)
        {
            return tl::unexpected(std::move(dcResult.error()));
        }
        const auto dc = std::move(*dcResult);
        auto dtResult = Derivative(trim, budget);
        if (!dtResult)
        {
            return tl::unexpected(std::move(dtResult.error()));
        }
        const auto dt = std::move(*dtResult);
        auto suResult = Derivative(net, true, budget);
        if (!suResult)
        {
            return tl::unexpected(std::move(suResult.error()));
        }
        const auto su = std::move(*suResult);
        auto svResult = Derivative(net, false, budget);
        if (!svResult)
        {
            return tl::unexpected(std::move(svResult.error()));
        }
        const auto sv = std::move(*svResult);
        if (begin < curve.knots[curve.degree] || end > curve.knots[curve.cp.size()] || begin < trim.knots[trim.degree] ||
            end > trim.knots[trim.cp.size()])
        {
            return finish();
        }
        std::vector<double> breaks{ begin, end };
        for (const auto* knots : { &curve.knots, &trim.knots })
        {
            for (double t : *knots)
            {
                if (t > begin && t < end)
                {
                    breaks.push_back(t);
                }
            }
        }
        std::sort(breaks.begin(), breaks.end());
        breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
        const double allowed = (I(Down(maximumMm)) * I(Down(maximumMm))).lo;
        double maximumSquared = 0;
        std::vector<I> pending;
        for (std::size_t i = 1; i < breaks.size(); ++i)
        {
            pending.emplace_back(breaks[i - 1], breaks[i]);
        }
        while (!pending.empty())
        {
            if (auto operation = budget.Product(); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const I t = pending.back();
            pending.pop_back();
            bool bounded = false;
            double squared = 0;
            try
            {
                auto cResult = CurveRange(curve, t, budget);
                if (!cResult)
                {
                    return tl::unexpected(std::move(cResult.error()));
                }
                const auto c = std::move(*cResult);
                auto pResult = CurveRange(trim, t, budget);
                if (!pResult)
                {
                    return tl::unexpected(std::move(pResult.error()));
                }
                const auto p = std::move(*pResult);
                const I u = DivideWeight(p[0], p[3]), v = DivideWeight(p[1], p[3]);
                auto liftedResult = SurfaceRange(net, u, v, budget);
                if (!liftedResult)
                {
                    return tl::unexpected(std::move(liftedResult.error()));
                }
                const auto lifted = std::move(*liftedResult);
                const I middle(std::midpoint(t.lo, t.hi));
                auto cmResult = CurveRange(curve, middle, budget);
                if (!cmResult)
                {
                    return tl::unexpected(std::move(cmResult.error()));
                }
                const auto cm = std::move(*cmResult);
                auto pmResult = CurveRange(trim, middle, budget);
                if (!pmResult)
                {
                    return tl::unexpected(std::move(pmResult.error()));
                }
                const auto pm = std::move(*pmResult);
                auto smResult = SurfaceRange(net, DivideWeight(pm[0], pm[3]), DivideWeight(pm[1], pm[3]), budget);
                if (!smResult)
                {
                    return tl::unexpected(std::move(smResult.error()));
                }
                const auto sm = std::move(*smResult);
                auto cpResult = EuclideanDerivative(curve, dc, t, budget);
                if (!cpResult)
                {
                    return tl::unexpected(std::move(cpResult.error()));
                }
                const auto cp = std::move(*cpResult);
                auto tpResult = EuclideanDerivative(trim, dt, t, budget);
                if (!tpResult)
                {
                    return tl::unexpected(std::move(tpResult.error()));
                }
                const auto tp = std::move(*tpResult);
                auto supResult = SurfaceRange(su, u, v, budget);
                if (!supResult)
                {
                    return tl::unexpected(std::move(supResult.error()));
                }
                const auto sup = std::move(*supResult);
                auto svpResult = SurfaceRange(sv, u, v, budget);
                if (!svpResult)
                {
                    return tl::unexpected(std::move(svpResult.error()));
                }
                const auto svp = std::move(*svpResult);
                I error;
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    const auto direct = DivideWeight(c[axis], c[3]) - lifted[axis];
                    // A centered mean-value enclosure keeps paired t dependence.
                    // Independent point hull subtraction alone would require
                    // refinement proportional to edge length / error budget.
                    // The residual derivative includes the UV chain rule and
                    // every crossed surface-knot cell through SurfaceRange.
                    const auto taylor = DivideWeight(cm[axis], cm[3]) - sm[axis] + (cp[axis] - sup[axis] * tp[0] - svp[axis] * tp[1]) * (t - middle);
                    const I delta(std::max(direct.lo, taylor.lo), std::min(direct.hi, taylor.hi));
                    const double absolute = std::max(std::abs(delta.lo), std::abs(delta.hi));
                    error = error + I(absolute) * I(absolute);
                }
                squared = error.hi;
                bounded = squared <= allowed;
            }
            catch (const Unestablished&)
            {
            }
            if (bounded)
            {
                maximumSquared = std::max(maximumSquared, squared);
                ++result.cells;
            }
            else
            {
                const auto middle = std::midpoint(t.lo, t.hi);
                if (!(middle > t.lo && middle < t.hi))
                {
                    return finish();
                }
                // Depth-first refinement retains O(depth + knot count) nodes;
                // work is shared with coefficient evaluation and never resets.
                pending.emplace_back(middle, t.hi);
                pending.emplace_back(t.lo, middle);
            }
        }
        result.maximumMm = Up(std::sqrt(maximumSquared));
        result.established = result.cells > 0 && *result.maximumMm <= maximumMm;
    }
    catch (const WorkLimit&)
    {
        result.workLimit = true;
    }
    catch (const Unestablished&)
    {
    }
    return finish();
}
} // namespace step_import::detail
