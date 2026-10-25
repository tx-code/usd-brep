// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportSplineChart.h"

#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmContext.h>
#include <SmCrvOnSurf.h>
#include <SmTol.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <nurbs.h>
#include <type_traits>
#include <utility>
#include <vector>

namespace step_import::detail
{
namespace
{
constexpr std::size_t MaximumSurfaceAxis = 64, MaximumCurveControls = 4096, MaximumKnots = 8192;
using Pole = std::array<double, 4>;
struct SurfaceNet
{
    ULONG degreeU = 0, degreeV = 0, countU = 0, countV = 0;
    std::vector<Pole> poles;
    std::vector<double> u, v;
    std::array<double, 4> domain{};
};
struct CurveNet
{
    ULONG degree = 0, dimension = 0;
    std::vector<Pole> poles;
    std::vector<double> knots;
    std::array<double, 2> domain{};
};
auto Refuse(const char* key)
{
    return tl::unexpected(Failure{ Status::Indeterminate, key, "Exact binary spline chart evidence is unestablished", {}, "SplineChart" });
}
Result<void> Callbacks(const SplineChartCallbacks& callbacks)
{
    if (!callbacks.beforeNativeCall || !callbacks.chargeWork)
    {
        return Refuse("SplineChartCallbacksRequired");
    }
    return {};
}
// The macros only propagate the existing Failure value, without changing its
// cancellation/resource identity or catching allocation/STL exceptions.
#define CHART_CHECK(expression)                                                                                                                      \
    do                                                                                                                                               \
    {                                                                                                                                                \
        auto checked = (expression);                                                                                                                 \
        if (!checked)                                                                                                                                \
        {                                                                                                                                            \
            return tl::unexpected(std::move(checked.error()));                                                                                       \
        }                                                                                                                                            \
    } while (false)
#define CHART_VALUE(name, expression)                                                                                                                \
    auto name = (expression);                                                                                                                        \
    if (!name)                                                                                                                                       \
    {                                                                                                                                                \
        return tl::unexpected(std::move(name.error()));                                                                                              \
    }

template <class Function>
Result<std::invoke_result_t<Function>> Native(const SplineChartCallbacks& callbacks, Function&& function)
{
    CHART_CHECK(callbacks.chargeWork(1));
    CHART_CHECK(callbacks.beforeNativeCall());
    return function();
}
template <class Function>
Result<void> NativeStatus(const SplineChartCallbacks& callbacks, Function&& function, const char* key)
{
    CHART_VALUE(status, Native(callbacks, std::forward<Function>(function)));
    if (*status != SM_SUCCESS)
    {
        return Refuse(key);
    }
    return {};
}
bool Bits(double a, double b)
{
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
Result<double> WorkingZone(const SmObject& object, const SplineChartCallbacks& callbacks)
{
    CHART_VALUE(
        zone,
        Native(
            callbacks,
            [&]
            {
                return double(SmTol::GetZoneTol3d(&object));
            }
        )
    );
    CHART_CHECK(callbacks.chargeWork(1));
    if (!std::isfinite(*zone) || !(*zone > 0))
    {
        return Refuse("SplineChartWorkingToleranceUnestablished");
    }
    return *zone;
}
Result<void> SameWorkingZone(const SmObject& object, double originalZone, const SplineChartCallbacks& callbacks)
{
    CHART_VALUE(zone, WorkingZone(object, callbacks));
    CHART_CHECK(callbacks.chargeWork(1));
    if (!Bits(*zone, originalZone))
    {
        return Refuse("SplineChartWorkingToleranceChanged");
    }
    return {};
}
template <class A, class B>
Result<bool> SameScalars(const A& a, const B& b, const SplineChartCallbacks& callbacks)
{
    CHART_CHECK(callbacks.chargeWork(1));
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        CHART_CHECK(callbacks.chargeWork(1));
        if (!Bits(a[i], b[i]))
        {
            return false;
        }
    }
    return true;
}
Result<bool> Same(const SurfaceNet& a, const SurfaceNet& b, const SplineChartCallbacks& callbacks)
{
    CHART_CHECK(callbacks.chargeWork(5));
    if (a.degreeU != b.degreeU || a.degreeV != b.degreeV || a.countU != b.countU || a.countV != b.countV || a.poles.size() != b.poles.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.poles.size(); ++i)
    {
        CHART_VALUE(equal, SameScalars(a.poles[i], b.poles[i], callbacks));
        if (!*equal)
        {
            return false;
        }
    }
    CHART_VALUE(u, SameScalars(a.u, b.u, callbacks));
    CHART_VALUE(v, SameScalars(a.v, b.v, callbacks));
    CHART_VALUE(domain, SameScalars(a.domain, b.domain, callbacks));
    return *u && *v && *domain;
}
Result<bool> Same(const CurveNet& a, const CurveNet& b, const SplineChartCallbacks& callbacks)
{
    CHART_CHECK(callbacks.chargeWork(3));
    if (a.degree != b.degree || a.dimension != b.dimension || a.poles.size() != b.poles.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.poles.size(); ++i)
    {
        CHART_VALUE(equal, SameScalars(a.poles[i], b.poles[i], callbacks));
        if (!*equal)
        {
            return false;
        }
    }
    CHART_VALUE(knots, SameScalars(a.knots, b.knots, callbacks));
    CHART_VALUE(domain, SameScalars(a.domain, b.domain, callbacks));
    return *knots && *domain;
}
Result<bool> Clamped(const std::vector<double>& knots, ULONG degree, std::size_t controls, const SplineChartCallbacks& callbacks)
{
    CHART_CHECK(callbacks.chargeWork(1));
    if (degree == 0 || degree > 32 || controls <= degree || knots.size() != controls + degree + 1 || !(knots.front() < knots.back()))
    {
        return false;
    }
    for (std::size_t i = 0; i < knots.size(); ++i)
    {
        CHART_CHECK(callbacks.chargeWork(1));
        if (!std::isfinite(knots[i]) || (i && knots[i] < knots[i - 1]))
        {
            return false;
        }
    }
    for (std::size_t i = 0; i <= degree; ++i)
    {
        CHART_CHECK(callbacks.chargeWork(2));
        if (!Bits(knots[i], knots.front()) || !Bits(knots[knots.size() - 1 - i], knots.back()))
        {
            return false;
        }
    }
    return true;
}
Result<std::optional<SurfaceNet>> ReadSurface(const SmSurface& source, const SplineChartCallbacks& callbacks)
{
    CHART_VALUE(
        type,
        Native(
            callbacks,
            [&]
            {
                return source.GetType();
            }
        )
    );
    if (*type != SmBSplineSurface_TYPE)
    {
        return std::nullopt;
    }
    const auto& surface = static_cast<const SmBSplineSurface&>(source);
    CHART_VALUE(
        borrowed,
        Native(
            callbacks,
            [&]
            {
                return surface.IsNurbBorrowed();
            }
        )
    );
    CHART_VALUE(
        native,
        Native(
            callbacks,
            [&]
            {
                return surface.GetGwNurbPointer();
            }
        )
    );
    if (*borrowed != FALSE || !*native)
    {
        return std::nullopt;
    }
    SurfaceNet net;
    double* points = nullptr;
    CHART_CHECK(NativeStatus(
        callbacks,
        [&]
        {
            return surface.GetControlPointsPointer(net.countU, net.countV, points);
        },
        "SplineChartSurfaceControls"
    ));
    ULONG ku = 0, kv = 0;
    double *u = nullptr, *v = nullptr;
    CHART_CHECK(NativeStatus(
        callbacks,
        [&]
        {
            return surface.GetKnotsPointers(ku, kv, u, v);
        },
        "SplineChartSurfaceKnots"
    ));
    if (!points || !u || !v || net.countU == 0 || net.countV == 0 || net.countU > MaximumSurfaceAxis || net.countV > MaximumSurfaceAxis || ku == 0 ||
        kv == 0 || ku > MaximumKnots || kv > MaximumKnots - ku)
    {
        return std::nullopt;
    }
    CHART_VALUE(
        du,
        Native(
            callbacks,
            [&]
            {
                return surface.GetDegree(SM_SP_U);
            }
        )
    );
    CHART_VALUE(
        dv,
        Native(
            callbacks,
            [&]
            {
                return surface.GetDegree(SM_SP_V);
            }
        )
    );
    net.degreeU = *du;
    net.degreeV = *dv;
    CHART_CHECK(callbacks.chargeWork(std::size_t(net.countU) * net.countV * 4 + ku + kv));
    net.u.assign(u, u + ku);
    net.v.assign(v, v + kv);
    for (std::size_t i = 0; i < std::size_t(net.countU) * net.countV; ++i)
    {
        Pole pole{ points[4 * i], points[4 * i + 1], points[4 * i + 2], points[4 * i + 3] };
        CHART_CHECK(callbacks.chargeWork(4));
        if (!std::all_of(
                pole.begin(),
                pole.end(),
                [](double x)
                {
                    return std::isfinite(x);
                }
            ))
        {
            return std::nullopt;
        }
        net.poles.push_back(pole);
    }
    CHART_VALUE(cu, Clamped(net.u, net.degreeU, net.countU, callbacks));
    CHART_VALUE(cv, Clamped(net.v, net.degreeV, net.countV, callbacks));
    if (!*cu || !*cv)
    {
        return std::nullopt;
    }
    CHART_VALUE(
        domain,
        Native(
            callbacks,
            [&]
            {
                return surface.GetNaturalUVDomain();
            }
        )
    );
    net.domain = { domain->GetMin().x, domain->GetMin().y, domain->GetMax().x, domain->GetMax().y };
    const std::array<double, 4> expected{ net.u.front(), net.v.front(), net.u.back(), net.v.back() };
    CHART_VALUE(equal, SameScalars(net.domain, expected, callbacks));
    if (!*equal)
    {
        return std::nullopt;
    }
    return std::optional<SurfaceNet>(std::move(net));
}
Result<std::optional<CurveNet>> ReadCurve(const SmCurve& source, const SplineChartCallbacks& callbacks)
{
    CHART_VALUE(
        type,
        Native(
            callbacks,
            [&]
            {
                return source.GetType();
            }
        )
    );
    if (*type != SmBSplineCurve_TYPE)
    {
        return std::nullopt;
    }
    const auto& curve = static_cast<const SmBSplineCurve&>(source);
    CHART_VALUE(
        borrowed,
        Native(
            callbacks,
            [&]
            {
                return curve.IsNurbBorrowed();
            }
        )
    );
    CHART_VALUE(
        native,
        Native(
            callbacks,
            [&]
            {
                return curve.GetGwNurbPointer();
            }
        )
    );
    CHART_VALUE(
        rational,
        Native(
            callbacks,
            [&]
            {
                return curve.IsRational();
            }
        )
    );
    CHART_VALUE(
        dimension,
        Native(
            callbacks,
            [&]
            {
                return curve.GetDim();
            }
        )
    );
    if (*borrowed != FALSE || !*native || *rational || *dimension != 2 || !(*native)->knt || (*native)->knt->m < 0 ||
        std::size_t((*native)->knt->m) >= MaximumKnots)
    {
        return std::nullopt;
    }
    CurveNet net;
    net.dimension = *dimension;
    ULONG count = 0;
    double* points = nullptr;
    CHART_CHECK(NativeStatus(
        callbacks,
        [&]
        {
            return curve.GetControlPointsPointer(count, points);
        },
        "SplineChartCurveControls"
    ));
    if (!points || count == 0 || count > MaximumCurveControls)
    {
        return std::nullopt;
    }
    CHART_VALUE(
        degree,
        Native(
            callbacks,
            [&]
            {
                return curve.GetDegree();
            }
        )
    );
    net.degree = *degree;
    SmTArray<double> knots;
    CHART_CHECK(NativeStatus(
        callbacks,
        [&]
        {
            return curve.GetKnotsAll(knots);
        },
        "SplineChartCurveKnots"
    ));
    if (knots.GetSize() == 0 || knots.GetSize() > MaximumKnots)
    {
        return std::nullopt;
    }
    CHART_CHECK(callbacks.chargeWork(std::size_t(count) * 4 + knots.GetSize()));
    net.knots.assign(knots.GetDataArray(), knots.GetDataArray() + knots.GetSize());
    for (std::size_t i = 0; i < count; ++i)
    {
        Pole pole{ points[4 * i], points[4 * i + 1], points[4 * i + 2], points[4 * i + 3] };
        CHART_CHECK(callbacks.chargeWork(4));
        if (!std::all_of(
                pole.begin(),
                pole.end(),
                [](double x)
                {
                    return std::isfinite(x);
                }
            ) ||
            (pole[2] != 0 && pole[2] != NL_NOZ) || pole[3] != NL_NOW)
        {
            return std::nullopt;
        }
        net.poles.push_back(pole);
    }
    CHART_VALUE(clamped, Clamped(net.knots, net.degree, count, callbacks));
    if (!*clamped)
    {
        return std::nullopt;
    }
    CHART_VALUE(
        domain,
        Native(
            callbacks,
            [&]
            {
                return curve.GetNaturalInterval();
            }
        )
    );
    net.domain = { domain->GetMin(), domain->GetMax() };
    const std::array<double, 2> expected{ net.knots.front(), net.knots.back() };
    CHART_VALUE(equal, SameScalars(net.domain, expected, callbacks));
    if (!*equal)
    {
        return std::nullopt;
    }
    return std::optional<CurveNet>(std::move(net));
}
Result<bool> Scale(double& value, int exponent, const SplineChartCallbacks& callbacks)
{
    CHART_CHECK(callbacks.chargeWork(2));
    const double scaled = std::ldexp(value, exponent);
    if (!std::isfinite(scaled) || !Bits(std::ldexp(scaled, -exponent), value))
    {
        return false;
    }
    value = scaled;
    return true;
}
Result<bool> RadiusOnly(const SmCurve& uv, const SplineChartCallbacks& callbacks, bool selected)
{
    SmAssertArray reports;
    SmTArray<ULONG> tests;
    tests.Add(13);
    CHART_VALUE(
        valid,
        Native(
            callbacks,
            [&]
            {
                return uv.AssertValid(&reports, selected ? SM_LEVEL_GIVEN : SM_LEVEL_2, SM_NO_WALK, selected ? &tests : nullptr);
            }
        )
    );
    if (*valid || reports.GetSize() == 0)
    {
        return false;
    }
    for (const auto* report : reports)
    {
        CHART_CHECK(callbacks.chargeWork(2));
        if (report->GetOwner() != &uv || report->GetTestIndex() != 13)
        {
            return false;
        }
    }
    return true;
}
Result<void> Valid(const SmObject& object, const SplineChartCallbacks& callbacks)
{
    SmAssertArray reports;
    CHART_VALUE(
        valid,
        Native(
            callbacks,
            [&]
            {
                return object.AssertValid(&reports, SM_LEVEL_2, SM_WALK);
            }
        )
    );
    if (!*valid || reports.GetSize())
    {
        return Refuse("SplineChartNativeValidation");
    }
    return {};
}
template <class T>
struct PendingCopy
{
    const T* source;
    T* value = nullptr;
    ~PendingCopy()
    {
        // A native Copy may populate its output before returning/throwing.
        // Never delete a returned alias to the immutable original.
        if (value != source)
        {
            delete value;
        }
    }
    std::unique_ptr<T> Take()
    {
        return std::unique_ptr<T>(std::exchange(value, nullptr));
    }
};
Result<bool> Unowned(const SmSurface& surface, const SmCurve* uv, SmContext& context, const SplineChartCallbacks& callbacks)
{
    CHART_VALUE(
        owner,
        Native(
            callbacks,
            [&]
            {
                return surface.GetOwner();
            }
        )
    );
    CHART_VALUE(
        actualContext,
        Native(
            callbacks,
            [&]
            {
                return surface.GetContext();
            }
        )
    );
    if (*owner || *actualContext != &context)
    {
        return false;
    }
    if (uv)
    {
        CHART_VALUE(
            curveOwner,
            Native(
                callbacks,
                [&]
                {
                    return uv->GetOwner();
                }
            )
        );
        CHART_VALUE(
            curveContext,
            Native(
                callbacks,
                [&]
                {
                    return uv->GetContext();
                }
            )
        );
        return !*curveOwner && *curveContext == &context;
    }
    return true;
}
} // namespace

struct SplineChartData
{
    SurfaceNet original, target;
    int uExponent = 0, vExponent = 0;
    std::array<double, 2> metricUv{}, derivativeLengths{};
    double ratio = 0, cosine = 0, surfaceWorkingZone = 0;
};
struct SplineChartWitnessData
{
    std::shared_ptr<const SplineChartData> chart;
    CurveNet original, target;
    double curveWorkingZone = 0;
};
struct SplineChartAccess
{
    static const SplineChartData& Data(const BinarySplineChart& chart)
    {
        return *chart.m_data;
    }
    static std::shared_ptr<const SplineChartData> Share(const BinarySplineChart& chart)
    {
        return chart.m_data;
    }
    static BinarySplineChart Plan(std::shared_ptr<const SplineChartData> data)
    {
        return BinarySplineChart(std::move(data));
    }
    static std::shared_ptr<const SplineChartWitness> Witness(std::shared_ptr<const SplineChartWitnessData> data)
    {
        return std::shared_ptr<const SplineChartWitness>(new SplineChartWitness(std::move(data)));
    }
};
BinarySplineChart::BinarySplineChart(std::shared_ptr<const SplineChartData> data) : m_data(std::move(data))
{
}
int BinarySplineChart::UExponent() const
{
    return m_data->uExponent;
}
int BinarySplineChart::VExponent() const
{
    return m_data->vExponent;
}
std::array<double, 4> BinarySplineChart::OriginalDomain() const
{
    return m_data->original.domain;
}
std::array<double, 4> BinarySplineChart::TargetDomain() const
{
    return m_data->target.domain;
}
std::array<double, 2> BinarySplineChart::MetricUv() const
{
    return m_data->metricUv;
}
std::array<double, 2> BinarySplineChart::MetricDerivativeLengths() const
{
    return m_data->derivativeLengths;
}
double BinarySplineChart::MetricRatio() const
{
    return m_data->ratio;
}
double BinarySplineChart::MetricCosine() const
{
    return m_data->cosine;
}
std::size_t BinarySplineChart::SurfaceControlCount() const
{
    return m_data->original.poles.size();
}
SplineChartWitness::SplineChartWitness(std::shared_ptr<const SplineChartWitnessData> data) : m_data(std::move(data))
{
}
std::size_t SplineChartWitness::CurveControlCount() const
{
    return m_data->original.poles.size();
}
Result<void> SplineChartWitness::Verify(const SmSurface& surface, const SmCurve* uv, const SplineChartCallbacks& callbacks) const
{
    CHART_CHECK(Callbacks(callbacks));
    CHART_VALUE(actual, ReadSurface(surface, callbacks));
    if (!*actual)
    {
        return Refuse("SplineChartSurfaceWitnessMismatch");
    }
    CHART_VALUE(same, Same(**actual, m_data->chart->target, callbacks));
    if (!*same)
    {
        return Refuse("SplineChartSurfaceWitnessMismatch");
    }
    if (uv)
    {
        CHART_VALUE(curve, ReadCurve(*uv, callbacks));
        if (!*curve)
        {
            return Refuse("SplineChartCurveWitnessMismatch");
        }
        CHART_VALUE(equal, Same(**curve, m_data->target, callbacks));
        if (!*equal)
        {
            return Refuse("SplineChartCurveWitnessMismatch");
        }
    }
    // Catch cancellation after the last native getter and before handoff.
    return callbacks.beforeNativeCall();
}
Result<std::unique_ptr<SmSurface>> ApplySplineSurfaceChart(
    const SmSurface& original,
    const BinarySplineChart& chart,
    SmContext& context,
    const SplineChartCallbacks& callbacks
)
{
    CHART_CHECK(Callbacks(callbacks));
    CHART_VALUE(unowned, Unowned(original, nullptr, context, callbacks));
    CHART_VALUE(source, ReadSurface(original, callbacks));
    if (!*unowned || !*source)
    {
        return Refuse("SplineChartSurfaceScope");
    }
    const auto& data = SplineChartAccess::Data(chart);
    CHART_CHECK(SameWorkingZone(original, data.surfaceWorkingZone, callbacks));
    CHART_VALUE(matches, Same(**source, data.original, callbacks));
    if (!*matches)
    {
        return Refuse("SplineChartSourceSurfaceMismatch");
    }
    PendingCopy<SmSurface> pending{ &original };
    CHART_CHECK(NativeStatus(
        callbacks,
        [&]
        {
            return original.Copy(context, pending.value);
        },
        "SplineChartSurfaceCopy"
    ));
    if (!pending.value || pending.value == &original)
    {
        return Refuse("SplineChartSurfaceCopyAlias");
    }
    auto copy = pending.Take();
    CHART_CHECK(SameWorkingZone(*copy, data.surfaceWorkingZone, callbacks));
    CHART_VALUE(copyUnowned, Unowned(*copy, nullptr, context, callbacks));
    CHART_VALUE(copied, ReadSurface(*copy, callbacks));
    if (!*copyUnowned || !*copied)
    {
        return Refuse("SplineChartSurfaceCopyScope");
    }
    CHART_VALUE(copyMatches, Same(**copied, data.original, callbacks));
    if (!*copyMatches)
    {
        return Refuse("SplineChartSurfaceCopyMismatch");
    }
    const auto& d = data.target.domain;
    CHART_CHECK(NativeStatus(
        callbacks,
        [&]
        {
            return copy->Reparameterize(SmExtent2d(d[0], d[1], d[2], d[3]));
        },
        "SplineChartSurfaceReparameterize"
    ));
    CHART_VALUE(target, ReadSurface(*copy, callbacks));
    if (!*target)
    {
        return Refuse("SplineChartSurfaceTargetMismatch");
    }
    CHART_VALUE(exact, Same(**target, data.target, callbacks));
    if (!*exact)
    {
        return Refuse("SplineChartSurfaceTargetMismatch");
    }
    CHART_CHECK(Valid(*copy, callbacks));
    // Copy/reparameterization/native validation must preserve the effective
    // ownerless physical zone, independently of the exact coordinate proof.
    // No tolerance/precision setter is used by this module.
    CHART_CHECK(SameWorkingZone(*copy, data.surfaceWorkingZone, callbacks));
    CHART_CHECK(SameWorkingZone(original, data.surfaceWorkingZone, callbacks));
    CHART_VALUE(unchanged, ReadSurface(original, callbacks));
    if (!*unchanged)
    {
        return Refuse("SplineChartOriginalChanged");
    }
    CHART_VALUE(originalMatches, Same(**unchanged, data.original, callbacks));
    if (!*originalMatches)
    {
        return Refuse("SplineChartOriginalChanged");
    }
    CHART_CHECK(callbacks.beforeNativeCall());
    return copy;
}
Result<std::optional<SplineChartCandidate>> TryConditionSplinePcurve(
    const SmSurface& surface,
    const SmCurve& uv,
    SmContext& context,
    const std::optional<BinarySplineChart>& preset,
    const SplineChartCallbacks& callbacks
)
{
    CHART_CHECK(Callbacks(callbacks));
    CHART_VALUE(unowned, Unowned(surface, &uv, context, callbacks));
    if (!*unowned)
    {
        return std::nullopt;
    }
    CHART_VALUE(curve, ReadCurve(uv, callbacks));
    if (!*curve)
    {
        return std::nullopt;
    }
    CHART_VALUE(curveZone, WorkingZone(uv, callbacks));
    // The bounded, finite, ownerless UV definition is sufficient for this
    // precheck. It cannot access the independent surface. Normal valid pcurves
    // therefore avoid copying/scanning a potentially 64x64 surface net.
    CHART_VALUE(selected, RadiusOnly(uv, callbacks, true));
    if (!*selected)
    {
        return std::nullopt;
    }
    CHART_VALUE(only, RadiusOnly(uv, callbacks, false));
    if (!*only)
    {
        return std::nullopt;
    }
    CHART_VALUE(source, ReadSurface(surface, callbacks));
    if (!*source)
    {
        return std::nullopt;
    }
    CHART_VALUE(surfaceZone, WorkingZone(surface, callbacks));
    CHART_CHECK(Valid(surface, callbacks));
    std::shared_ptr<const SplineChartData> plan;
    if (preset)
    {
        plan = SplineChartAccess::Share(*preset);
        CHART_CHECK(SameWorkingZone(surface, plan->surfaceWorkingZone, callbacks));
        CHART_VALUE(matches, Same(**source, plan->original, callbacks));
        if (!*matches)
        {
            return std::nullopt;
        }
    }
    else
    {
        auto data = std::make_shared<SplineChartData>();
        data->surfaceWorkingZone = *surfaceZone;
        CHART_CHECK(callbacks.chargeWork((**source).poles.size() * 8 + ((**source).u.size() + (**source).v.size()) * 2 + 8));
        data->original = **source;
        data->target = **source;
        const auto& interval = (**curve).domain;
        SmPoint3d point;
        CHART_CHECK(NativeStatus(
            callbacks,
            [&]
            {
                return uv.EvaluatePoint(std::midpoint(interval[0], interval[1]), point);
            },
            "SplineChartMetricCurve"
        ));
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        {
            return std::nullopt;
        }
        SmVector3d derivatives[4];
        CHART_CHECK(NativeStatus(
            callbacks,
            [&]
            {
                return surface.Evaluate(SmPoint2d(point.x, point.y), 1, 1, TRUE, TRUE, TRUE, derivatives, FALSE);
            },
            "SplineChartMetricSurface"
        ));
        // SmSurface's matrix layout has Sv at index 1 and Su at index 2.
        // One physical metric sample selects ONE plan; validation never
        // searches alternate scalings until a failed curve starts to pass.
        const auto su = derivatives[2], sv = derivatives[1];
        const double a = std::hypot(su.x, su.y, su.z), b = std::hypot(sv.x, sv.y, sv.z);
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(a) || !std::isfinite(b) || !(a > 0) || !(b > 0))
        {
            return std::nullopt;
        }
        const double ratio = a / b, cosine = (su.x / a) * (sv.x / b) + (su.y / a) * (sv.y / b) + (su.z / a) * (sv.z / b);
        if (!std::isfinite(ratio) || !(ratio > 0) || !std::isfinite(cosine) || std::abs(cosine) >= 1 - 64 * std::numeric_limits<double>::epsilon())
        {
            return std::nullopt;
        }
        const double balance = std::round(std::log2(ratio));
        if (!std::isfinite(balance) || std::abs(balance) > 32 || balance == 0)
        {
            return std::nullopt;
        }
        data->uExponent = balance > 0 ? int(balance) : 0;
        data->vExponent = balance < 0 ? -int(balance) : 0;
        data->metricUv = { point.x, point.y };
        data->derivativeLengths = { a, b };
        data->ratio = ratio;
        data->cosine = cosine;
        for (auto& knot : data->target.u)
        {
            CHART_VALUE(exact, Scale(knot, data->uExponent, callbacks));
            if (!*exact)
            {
                return std::nullopt;
            }
        }
        for (auto& knot : data->target.v)
        {
            CHART_VALUE(exact, Scale(knot, data->vExponent, callbacks));
            if (!*exact)
            {
                return std::nullopt;
            }
        }
        data->target.domain = { data->target.u.front(), data->target.v.front(), data->target.u.back(), data->target.v.back() };
        plan = std::move(data);
    }
    auto evidence = std::make_shared<SplineChartWitnessData>();
    evidence->chart = plan;
    evidence->curveWorkingZone = *curveZone;
    CHART_CHECK(callbacks.chargeWork((**curve).poles.size() * 8 + (**curve).knots.size() * 2 + 4));
    evidence->original = **curve;
    evidence->target = **curve;
    for (auto& pole : evidence->target.poles)
    {
        CHART_VALUE(u, Scale(pole[0], plan->uExponent, callbacks));
        CHART_VALUE(v, Scale(pole[1], plan->vExponent, callbacks));
        if (!*u || !*v)
        {
            return std::nullopt;
        }
    }
    auto chart = SplineChartAccess::Plan(plan);
    CHART_VALUE(targetSurface, ApplySplineSurfaceChart(surface, chart, context, callbacks));
    PendingCopy<SmCurve> pending{ &uv };
    CHART_CHECK(NativeStatus(
        callbacks,
        [&]
        {
            return uv.Copy(context, pending.value);
        },
        "SplineChartCurveCopy"
    ));
    if (!pending.value || pending.value == &uv)
    {
        return Refuse("SplineChartCurveCopyAlias");
    }
    auto targetUv = pending.Take();
    CHART_CHECK(SameWorkingZone(*targetUv, *curveZone, callbacks));
    CHART_VALUE(copied, ReadCurve(*targetUv, callbacks));
    if (!*copied)
    {
        return Refuse("SplineChartCurveCopyScope");
    }
    CHART_VALUE(exactCopy, Same(**copied, evidence->original, callbacks));
    CHART_VALUE(copyUnowned, Unowned(**targetSurface, targetUv.get(), context, callbacks));
    if (!*exactCopy || !*copyUnowned)
    {
        return Refuse("SplineChartCurveCopyMismatch");
    }
    auto& spline = static_cast<SmBSplineCurve&>(*targetUv);
    for (std::size_t i = 0; i < evidence->target.poles.size(); ++i)
    {
        const auto& p = evidence->target.poles[i];
        CHART_CHECK(NativeStatus(
            callbacks,
            [&]
            {
                return spline.SetControlPoint(SM_CP_NON_RATIONAL, ULONG(i), SmPoint3d(p[0], p[1], p[2]), 1);
            },
            "SplineChartCurveControlUpdate"
        ));
    }
    CHART_CHECK(Valid(*targetUv, callbacks));
    // Borrowed temporary composition checks the actual candidate pair, including
    // UV containment. Owner flag 0 neither adopts nor rewrites either object.
    const auto& d = plan->target.domain;
    const SmExtent2d domain(d[0], d[1], d[2], d[3]);
    CHART_CHECK(callbacks.chargeWork(1));
    CHART_CHECK(callbacks.beforeNativeCall());
    SmCrvOnSurf composed(*targetUv, **targetSurface, &domain, 0, &context);
    CHART_CHECK(Valid(composed, callbacks));
    CHART_VALUE(originalSurface, ReadSurface(surface, callbacks));
    CHART_VALUE(originalCurve, ReadCurve(uv, callbacks));
    if (!*originalSurface || !*originalCurve)
    {
        return Refuse("SplineChartOriginalChanged");
    }
    CHART_VALUE(sameSurface, Same(**originalSurface, plan->original, callbacks));
    CHART_VALUE(sameCurve, Same(**originalCurve, evidence->original, callbacks));
    if (!*sameSurface || !*sameCurve)
    {
        return Refuse("SplineChartOriginalChanged");
    }
    auto witness = SplineChartAccess::Witness(std::move(evidence));
    CHART_CHECK(witness->Verify(**targetSurface, targetUv.get(), callbacks));
    // These objects are still ownerless in the same actual context. Final
    // owning face/edge tolerances are checked by the import publication layer;
    // a geometric witness alone does not certify those later working budgets.
    CHART_CHECK(SameWorkingZone(**targetSurface, *surfaceZone, callbacks));
    CHART_CHECK(SameWorkingZone(*targetUv, *curveZone, callbacks));
    CHART_CHECK(SameWorkingZone(surface, *surfaceZone, callbacks));
    CHART_CHECK(SameWorkingZone(uv, *curveZone, callbacks));
    return std::optional<SplineChartCandidate>(
        SplineChartCandidate{ std::move(*targetSurface), std::move(targetUv), std::move(chart), std::move(witness) }
    );
}
#undef CHART_VALUE
#undef CHART_CHECK
} // namespace step_import::detail
