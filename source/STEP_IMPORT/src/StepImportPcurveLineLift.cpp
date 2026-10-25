// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPcurveLineLift.h"

#include "StepImportInterval.h"
#include "StepImportNativeStatus.h"
#include "StepImportProofEnvironment.h"

#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <nurbs.h>
#include <vector>

namespace step_import::detail
{
namespace
{
constexpr std::size_t MaximumControls = 4096, MaximumKnots = 8192;
using Pole = std::array<double, 4>;
struct Net
{
    bool rational = false;
    ULONG nu = 0, nv = 0, pu = 0, pv = 0;
    std::vector<Pole> poles;
    std::vector<double> u, v;
    std::array<double, 4> domain{};
};
auto Refuse(const char* key, Status status = Status::Indeterminate)
{
    return tl::unexpected(Failure{ status, key, "Source LINE pcurve conversion evidence is unestablished", {}, "PcurveLine" });
}
#define LINE_CHECK(expression)                                                                                                                       \
    do                                                                                                                                               \
    {                                                                                                                                                \
        auto checked = (expression);                                                                                                                 \
        if (!checked)                                                                                                                                \
        {                                                                                                                                            \
            return tl::unexpected(std::move(checked.error()));                                                                                       \
        }                                                                                                                                            \
    } while (false)
#define LINE_VALUE(name, expression)                                                                                                                 \
    auto name = (expression);                                                                                                                        \
    if (!name)                                                                                                                                       \
    {                                                                                                                                                \
        return tl::unexpected(std::move(name.error()));                                                                                              \
    }
Result<void> Work(const SplineChartCallbacks& callbacks, std::size_t units)
{
    if (!callbacks.chargeWork || !callbacks.beforeNativeCall)
    {
        return Refuse("PcurveLineCallbacksRequired");
    }
    LINE_CHECK(callbacks.chargeWork(units));
    if (!SurfaceProofNumericEnvironment())
    {
        return Refuse("PcurveLineNumericEnvironment");
    }
    return {};
}
Result<void> BeforeNative(const SplineChartCallbacks& callbacks)
{
    LINE_CHECK(Work(callbacks, 1));
    LINE_CHECK(callbacks.beforeNativeCall());
    if (!SurfaceProofNumericEnvironment())
    {
        return Refuse("PcurveLineNumericEnvironment");
    }
    return {};
}
bool Same(double a, double b)
{
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
Result<void> Clamped(const std::vector<double>& k, ULONG p, ULONG n, const SplineChartCallbacks& callbacks)
{
    if (!p || p > 17 || n <= p || k.size() != std::size_t(n) + p + 1 || !(k.front() < k.back()))
    {
        return Refuse("PcurveLineCarrierKnotsUnestablished");
    }
    for (std::size_t i = 0; i < k.size(); ++i)
    {
        LINE_CHECK(Work(callbacks, 1));
        if (!std::isfinite(k[i]) || (i && k[i] < k[i - 1]) || (i <= p && k[i] != k.front()) || (i >= n && k[i] != k.back()) ||
            (i > p && i < n && !(k.front() < k[i] && k[i] < k.back())))
        {
            return Refuse("PcurveLineCarrierKnotsUnestablished");
        }
    }
    return {};
}
Result<Net> ReadSurface(const SmSurface& source, const SplineChartCallbacks& callbacks)
{
    LINE_CHECK(BeforeNative(callbacks));
    if (source.GetType() != SmBSplineSurface_TYPE)
    {
        return Refuse("PcurveLineCarrierTypeUnsupported", Status::Unsupported);
    }
    const auto& surface = static_cast<const SmBSplineSurface&>(source);
    LINE_CHECK(BeforeNative(callbacks));
    if (surface.IsNurbBorrowed())
    {
        return Refuse("PcurveLineCarrierBorrowed");
    }
    Net net;
    LINE_CHECK(BeforeNative(callbacks));
    net.rational = surface.IsRational() != FALSE;
    double *points = nullptr, *u = nullptr, *v = nullptr;
    ULONG ku = 0, kv = 0;
    LINE_CHECK(BeforeNative(callbacks));
    LINE_CHECK(CheckNativeStatus(surface.GetControlPointsPointer(net.nu, net.nv, points), NativeStatusOrigin::GeneralConstruction));
    LINE_CHECK(BeforeNative(callbacks));
    LINE_CHECK(CheckNativeStatus(surface.GetKnotsPointers(ku, kv, u, v), NativeStatusOrigin::GeneralConstruction));
    if (!points || !u || !v || !net.nu || !net.nv || net.nu > MaximumControls / net.nv || !ku || !kv || ku > MaximumKnots || kv > MaximumKnots - ku)
    {
        return Refuse("PcurveLineCarrierCapacity");
    }
    LINE_CHECK(BeforeNative(callbacks));
    net.pu = surface.GetDegree(SM_SP_U);
    LINE_CHECK(BeforeNative(callbacks));
    net.pv = surface.GetDegree(SM_SP_V);
    LINE_CHECK(Work(callbacks, std::size_t(net.nu) * net.nv * 4 + ku + kv));
    net.u.assign(u, u + ku);
    net.v.assign(v, v + kv);
    LINE_CHECK(Clamped(net.u, net.pu, net.nu, callbacks));
    LINE_CHECK(Clamped(net.v, net.pv, net.nv, callbacks));
    for (std::size_t i = 0; i < std::size_t(net.nu) * net.nv; ++i)
    {
        LINE_CHECK(Work(callbacks, 4));
        Pole pole{ points[4 * i], points[4 * i + 1], points[4 * i + 2], points[4 * i + 3] };
        if (!std::all_of(
                pole.begin(),
                pole.end(),
                [](double x)
                {
                    return std::isfinite(x);
                }
            ) ||
            (net.rational ? !(pole[3] > 0) : pole[3] != NL_NOW))
        {
            return Refuse("PcurveLineCarrierPositiveWeightsUnestablished");
        }
        net.poles.push_back(pole);
    }
    LINE_CHECK(BeforeNative(callbacks));
    const auto domain = surface.GetSTEPUVDomain();
    net.domain = { domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y };
    if (net.domain != std::array{ net.u.front(), net.v.front(), net.u.back(), net.v.back() })
    {
        return Refuse("PcurveLineCarrierDomainUnestablished");
    }
    return net;
}
Result<void> ReadUv(const SmCurve& source, const std::array<std::array<double, 2>, 2>& ends, const Net& net, const SplineChartCallbacks& callbacks)
{
    LINE_CHECK(BeforeNative(callbacks));
    if (source.GetType() != SmBSplineCurve_TYPE)
    {
        return Refuse("PcurveLineNativeUvChanged", Status::PostcheckFailure);
    }
    const auto& uv = static_cast<const SmBSplineCurve&>(source);
    LINE_CHECK(BeforeNative(callbacks));
    if (uv.IsNurbBorrowed())
    {
        return Refuse("PcurveLineNativeUvChanged", Status::PostcheckFailure);
    }
    LINE_CHECK(BeforeNative(callbacks));
    if (uv.GetDim() != 2)
    {
        return Refuse("PcurveLineNativeUvChanged", Status::PostcheckFailure);
    }
    LINE_CHECK(BeforeNative(callbacks));
    if (uv.GetDegree() != 1)
    {
        return Refuse("PcurveLineNativeUvChanged", Status::PostcheckFailure);
    }
    LINE_CHECK(BeforeNative(callbacks));
    if (uv.IsRational())
    {
        return Refuse("PcurveLineNativeUvChanged", Status::PostcheckFailure);
    }
    LINE_CHECK(BeforeNative(callbacks));
    const auto interval = uv.GetNaturalInterval();
    if (interval.GetMin() != 0 || interval.GetMax() != 1)
    {
        return Refuse("PcurveLineNativeUvChanged", Status::PostcheckFailure);
    }
    SmTArray<SmPoint3d> points;
    SmTArray<double> weights, knots;
    LINE_CHECK(BeforeNative(callbacks));
    if (uv.GetNumberControlPoints() != 2)
    {
        return Refuse("PcurveLineNativeUvChanged", Status::PostcheckFailure);
    }
    LINE_CHECK(BeforeNative(callbacks));
    if (uv.GetNumberNaturalKnots() != 4)
    {
        return Refuse("PcurveLineNativeUvChanged", Status::PostcheckFailure);
    }
    LINE_CHECK(BeforeNative(callbacks));
    LINE_CHECK(CheckNativeStatus(uv.GetControlPolygon(points, weights), NativeStatusOrigin::GeneralConstruction));
    LINE_CHECK(BeforeNative(callbacks));
    LINE_CHECK(CheckNativeStatus(uv.GetKnotsAll(knots), NativeStatusOrigin::GeneralConstruction));
    if (points.GetSize() != 2 || knots.GetSize() != 4 || knots[0] != 0 || knots[1] != 0 || knots[2] != 1 || knots[3] != 1)
    {
        return Refuse("PcurveLineNativeUvChanged", Status::PostcheckFailure);
    }
    for (std::size_t i = 0; i < 2; ++i)
    {
        LINE_CHECK(Work(callbacks, 3));
        if (!Same(points[ULONG(i)].x, ends[i][0]) || !Same(points[ULONG(i)].y, ends[i][1]) || points[ULONG(i)].z != 0 || !std::isfinite(ends[i][0]) ||
            !std::isfinite(ends[i][1]) || ends[i][0] < net.domain[0] || ends[i][0] > net.domain[2] || ends[i][1] < net.domain[1] ||
            ends[i][1] > net.domain[3])
        {
            return Refuse("PcurveLineNativeUvChanged", Status::PostcheckFailure);
        }
    }
    return {};
}
double AbsUpper(Interval x)
{
    return std::max(std::abs(x.low), std::abs(x.high));
}
struct NumericUnestablished
{
};
template <class Function>
auto Arithmetic(Function&& function)
{
    // Catch only the immediate interval operation. A caller's work/native
    // callback may also throw overflow_error; that exception must unwind with
    // its original identity instead of becoming numerical refusal.
    try
    {
        return function();
    }
    catch (const std::overflow_error&)
    {
        throw NumericUnestablished{};
    }
    catch (const std::domain_error&)
    {
        throw NumericUnestablished{};
    }
}
Result<std::array<double, 2>> DerivativeBounds(const Net& net, const SplineChartCallbacks& callbacks)
{
    // H=(B+O*W,W), S-O=B/W. Positive weights give a global convex
    // bound on |S-O|. The derivative net gives |Bu| and |Wu|, hence
    // |Su| <= (|Bu|+|S-O|*|Wu|)/min(W), and similarly for V.
    // Each coordinate is enclosed outward; an L1 norm bounds Euclidean
    // distance. O is merely a binary translation to reduce cancellation.
    std::array<double, 3> origin{}, range{};
    for (std::size_t k = 0; k < 3; ++k)
    {
        origin[k] = net.poles.front()[k] / (net.rational ? net.poles.front()[3] : 1.);
        if (!std::isfinite(origin[k]))
        {
            return Refuse("PcurveLineDerivativeRangeUnestablished");
        }
    }
    std::vector<std::array<Interval, 4>> shifted;
    double minimumWeight = std::numeric_limits<double>::infinity();
    for (const auto& pole : net.poles)
    {
        LINE_CHECK(Work(callbacks, 16));
        std::array<Interval, 4> h;
        // NL_NOW is a negative storage sentinel, not a homogeneous weight.
        // Keep raw storage in the witness; a polynomial evaluator has W=1.
        const double weight = net.rational ? pole[3] : 1.;
        h[3] = Exact(weight);
        minimumWeight = std::min(minimumWeight, weight);
        for (std::size_t k = 0; k < 3; ++k)
        {
            h[k] = Arithmetic(
                [&]
                {
                    return Exact(pole[k]) - Exact(origin[k]) * h[3];
                }
            );
            range[k] = std::max(
                range[k],
                AbsUpper(Arithmetic(
                    [&]
                    {
                        return h[k] / h[3];
                    }
                ))
            );
        }
        shifted.push_back(h);
    }
    std::array<double, 2> result{};
    for (std::size_t axis = 0; axis < 2; ++axis)
    {
        const auto degree = axis ? net.pv : net.pu;
        const auto& knots = axis ? net.v : net.u;
        const auto count = axis ? net.nv : net.nu;
        std::array<double, 4> maximum{};
        for (ULONG i = 0; i + 1 < count; ++i)
        {
            LINE_CHECK(Work(callbacks, 3));
            const auto denominator = Arithmetic(
                [&]
                {
                    return Exact(knots[i + degree + 1]) - Exact(knots[i + 1]);
                }
            );
            if (!(denominator.low > 0))
            {
                return Refuse("PcurveLineDerivativeContinuityUnestablished");
            }
            const auto factor = Arithmetic(
                [&]
                {
                    return Exact(double(degree)) / denominator;
                }
            );
            const auto rows = axis ? net.nu : net.nv;
            for (ULONG j = 0; j < rows; ++j)
            {
                LINE_CHECK(Work(callbacks, 16));
                const auto first = axis ? std::size_t(j) * net.nv + i : std::size_t(i) * net.nv + j;
                const auto next = axis ? first + 1 : first + net.nv;
                for (std::size_t k = 0; k < 4; ++k)
                {
                    maximum[k] = std::max(
                        maximum[k],
                        AbsUpper(Arithmetic(
                            [&]
                            {
                                return (shifted[next][k] - shifted[first][k]) * factor;
                            }
                        ))
                    );
                }
            }
        }
        Interval sum = Exact(0);
        for (std::size_t k = 0; k < 3; ++k)
        {
            LINE_CHECK(Work(callbacks, 4));
            sum = Arithmetic(
                [&]
                {
                    return sum + (Exact(maximum[k]) + Exact(range[k]) * Exact(maximum[3])) / Exact(minimumWeight);
                }
            );
        }
        result[axis] = sum.high;
    }
    return result;
}
Result<bool> Equal(const Net& a, const Net& b, const SplineChartCallbacks& callbacks)
{
    if (a.rational != b.rational || a.nu != b.nu || a.nv != b.nv || a.pu != b.pu || a.pv != b.pv || a.poles.size() != b.poles.size() ||
        a.u.size() != b.u.size() || a.v.size() != b.v.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.poles.size(); ++i)
    {
        for (std::size_t k = 0; k < 4; ++k)
        {
            LINE_CHECK(Work(callbacks, 1));
            if (!Same(a.poles[i][k], b.poles[i][k]))
            {
                return false;
            }
        }
    }
    for (const auto pair : { std::pair{ &a.u, &b.u }, std::pair{ &a.v, &b.v } })
    {
        for (std::size_t i = 0; i < pair.first->size(); ++i)
        {
            LINE_CHECK(Work(callbacks, 1));
            if (!Same((*pair.first)[i], (*pair.second)[i]))
            {
                return false;
            }
        }
    }
    return a.domain == b.domain;
}
} // namespace
struct PcurveLineLiftData
{
    Net surface;
    std::array<std::array<double, 2>, 2> uvEnds{};
    double maximumMm = 0;
    std::optional<std::array<double, 2>> derivatives;
};
PcurveLineLiftWitness::PcurveLineLiftWitness(std::shared_ptr<const PcurveLineLiftData> data) : m_data(std::move(data))
{
}
double PcurveLineLiftWitness::MaximumMm() const
{
    return m_data->maximumMm;
}
std::optional<std::array<double, 2>> PcurveLineLiftWitness::DerivativeBoundsMmPerUv() const
{
    return m_data->derivatives;
}
Result<void> PcurveLineLiftWitness::Verify(const SmSurface& surface, const SmCurve& uv, const SplineChartCallbacks& callbacks) const
{
    LINE_VALUE(current, ReadSurface(surface, callbacks));
    LINE_VALUE(equal, Equal(m_data->surface, *current, callbacks));
    if (!*equal)
    {
        return Refuse("PcurveLineCarrierDefinitionChanged", Status::PostcheckFailure);
    }
    return ReadUv(uv, m_data->uvEnds, *current, callbacks);
}
Result<std::shared_ptr<const PcurveLineLiftWitness>> ProvePcurveLineLift(
    const SmSurface& surface,
    const SmCurve& uv,
    const std::array<std::array<double, 2>, 2>& ends,
    const std::array<double, 2>& maximumRoundingUv,
    double maximumMm,
    const SplineChartCallbacks& callbacks
)
{
    LINE_CHECK(Work(callbacks, 1));
    if (!(maximumMm >= 0) || !std::isfinite(maximumMm) ||
        !std::all_of(
            maximumRoundingUv.begin(),
            maximumRoundingUv.end(),
            [](double x)
            {
                return x >= 0 && std::isfinite(x);
            }
        ))
    {
        return Refuse("PcurveLineDefinitionBudgetUnestablished");
    }
    LINE_VALUE(net, ReadSurface(surface, callbacks));
    LINE_CHECK(ReadUv(uv, ends, *net, callbacks));
    auto data = std::make_shared<PcurveLineLiftData>();
    data->surface = std::move(*net);
    data->uvEnds = ends;
    if (maximumRoundingUv[0] != 0 || maximumRoundingUv[1] != 0)
    {
        try
        {
            LINE_VALUE(derivatives, DerivativeBounds(data->surface, callbacks));
            data->derivatives = *derivatives;
            Interval maximum = Exact(0);
            for (std::size_t k = 0; k < 2; ++k)
            {
                if (maximumRoundingUv[k] != 0)
                {
                    LINE_CHECK(Work(callbacks, 2));
                    maximum = Arithmetic(
                        [&]
                        {
                            return maximum + Exact((*derivatives)[k]) * Exact(maximumRoundingUv[k]);
                        }
                    );
                }
            }
            data->maximumMm = maximum.high;
        }
        catch (const NumericUnestablished&)
        {
            return Refuse("PcurveLineDerivativeRangeUnestablished");
        }
    }
    if (data->maximumMm > maximumMm)
    {
        return Refuse("PcurveLineDefinitionErrorUnestablished");
    }
    LINE_CHECK(Work(callbacks, 1));
    return std::shared_ptr<const PcurveLineLiftWitness>(new PcurveLineLiftWitness(std::move(data)));
}
#undef LINE_CHECK
#undef LINE_VALUE
} // namespace step_import::detail
