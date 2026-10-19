// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportSurfaceProof.h"

#include "StepImportProofEnvironment.h"

#include <SmBSplineSurface.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>
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
    const std::function<void()>& checkpoint;
    void ControlCount(std::size_t u, std::size_t v) const
    {
        if (u == 0 || v == 0 || u > controls / v)
        {
            throw WorkLimit{};
        }
    }
    void Product()
    {
        if (used == products)
        {
            throw WorkLimit{};
        }
        ++used;
        if ((used & 255) == 1)
        {
            checkpoint();
        }
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
std::vector<C> InsertCurve(const std::vector<C>& cp, const std::vector<double>& knots, unsigned p, double value, bool polynomial, Budget& budget)
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
        budget.Product();
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
void Refine(Net& net, bool uAxis, const std::vector<double>& values, Budget& budget)
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
            budget.checkpoint();
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
                const auto inserted = InsertCurve(cp, knots, p, value, net.polynomial, budget);
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
double PatchBound(const Net& a, const Net& b, std::size_t au, std::size_t av, std::size_t bu, std::size_t bv, Budget& budget, const Choose& choose)
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
                budget.Product();
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
                        budget.Product();
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
SurfaceRestrictionProof ProveSurfaceRestriction(
    const SmBSplineSurface& original,
    const SmBSplineSurface& restricted,
    double maximumMm,
    std::size_t maximumControls,
    std::size_t maximumProducts,
    const std::function<void()>& checkpoint
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
    const std::function<void()> checkedCheckpoint = [&]
    {
        checkpoint(); // Caller exceptions remain execution/cancellation failures.
        if (!SurfaceProofNumericEnvironment())
        {
            throw Unestablished{};
        }
    };
    Budget budget{ maximumControls, maximumProducts, 0, checkedCheckpoint };
    try
    {
        checkedCheckpoint();
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
        Refine(a, true, u, budget);
        Refine(a, false, v, budget);
        Refine(b, true, u, budget);
        Refine(b, false, v, budget);
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
                        checkedCheckpoint();
                        const auto bi = Span(b.u, b.p, b.nu, a.u[i], a.u[i + 1]);
                        const auto bj = Span(b.v, b.q, b.nv, a.v[j], a.v[j + 1]);
                        maximum = std::max(maximum, PatchBound(a, b, i, j, bi, bj, budget, choose));
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
} // namespace step_import::detail
