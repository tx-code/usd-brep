// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportExecution.h"

#include <SmAxis2Placement.h>
#include <SmBSplineCurve.h>
#include <SmCircle.h>
#include <SmEllipse.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>

namespace step_import::detail::construction
{
using Index = std::size_t;
using V = Point;
inline V Add(V a, V b)
{
    for (Index i = 0; i < 3; ++i)
    {
        a[i] += b[i];
    }
    return a;
}
inline V Sub(V a, V b)
{
    for (Index i = 0; i < 3; ++i)
    {
        a[i] -= b[i];
    }
    return a;
}
inline V Mul(V a, double b)
{
    for (auto& x : a)
    {
        x *= b;
    }
    return a;
}
inline double Dot(V a, V b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline V Cross(V a, V b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
inline double Norm(V a)
{
    return std::hypot(a[0], a[1], a[2]);
}
inline V Unit(V a)
{
    const double n = Norm(a);
    if (!(n > 0) || !std::isfinite(n))
    {
        throw std::domain_error("GeneralDegenerateFrame");
    }
    return Mul(a, 1 / n);
}
inline SmPoint3d P(V a)
{
    return { a[0], a[1], a[2] };
}
inline SmVector3d D(V a)
{
    return { a[0], a[1], a[2] };
}
inline V P(const SmPoint3d& a)
{
    return { a.x, a.y, a.z };
}
inline double Q(Fraction a)
{
    return double(a.Numerator()) / double(a.Denominator());
}
inline std::array<double, 2> FullPeriodDegrees(double startRadians)
{
    // Kernel analytic angles are degrees. Converting both radian endpoints
    // independently can produce a span one ulp above 360, outside the native
    // full-revolution constructor's range. Convert the phase once and add the
    // native period; this only chooses the computational chart. Source angles,
    // coefficients, topology and the later mm error budgets remain unchanged.
    const double startDegrees = startRadians * 180 / std::numbers::pi;
    return { startDegrees, startDegrees + 360.0 };
}
inline void Good(SmStatus a)
{
    if (a != SM_SUCCESS)
    {
        throw std::runtime_error("GeneralNativeReturn:" + std::to_string(a));
    }
}
struct Frame
{
    V o, x, y, z;
    SmAxis2Placement Native() const
    {
        return { P(o), D(x), D(y) };
    }
};

// Concrete, in-process construction dependencies shared by the material builder
// and independent definitions. The typed source snapshot remains the sole field
// interpretation. These are moved existing algorithms, not a second STEP parser
// or an injectable backend. A copy borrows the same native context/execution.
class GeometryConstruction
{
protected:

    GeometryConstruction(std::shared_ptr<const step_semantics::Snapshot> source, const Options& options, Report& report, Execution& execution)
        : m_source(std::move(source)), m_options(options), m_report(report), m_execution(execution)
    {
    }
    GeometryConstruction(const GeometryConstruction&) = default;
    std::shared_ptr<const step_semantics::Snapshot> m_source;
    const Options& m_options;
    Report& m_report;
    Execution& m_execution;
    SmContext* m_context = nullptr;
    double scale = 0;
    bool Is(Index a, std::string_view type) const
    {
        const auto registered = m_source->RegisteredTypes();
        return std::find(registered.begin(), registered.end(), type) != registered.end() && m_source->IsA(a, type);
    }
    std::string Id(Index a) const
    {
        return m_source->Source().Records().at(a).id;
    }
    [[noreturn]] void Fail(const char* key, Index a, Status status = Status::PostcheckFailure) const
    {
        throw Failure{ status, key, "General source-to-native contract is unestablished", a, "GeneralBrep" };
    }
    void Need(bool b, const char* key, Index a) const
    {
        if (!b)
        {
            Fail(key, a);
        }
    }
    void Step(Index a)
    {
        m_execution.Check(Phase::BeforeNativeCall);
        if (m_report.nativeCalls >= m_options.nativeLimits.nativeCalls)
        {
            Fail("NativeCallLimit", a, Status::ResourceLimit);
        }
        ++m_report.nativeCalls;
    }
    const step_source::Value& F(Index a, std::string_view name) const
    {
        return m_source->Field(a, name).Value();
    }
    Index Ref(Index a, std::string_view name) const
    {
        return m_source->Field(a, name).ReferenceTarget();
    }
    std::vector<Index> Refs(Index a, std::string_view name) const
    {
        return m_source->Field(a, name).ReferenceTargets();
    }
    bool B(Index a, std::string_view name) const
    {
        return F(a, name).text == "T";
    }
    double Number(const step_source::Value& v) const
    {
        std::string_view t = v.text;
        if (t.starts_with('+'))
        {
            t.remove_prefix(1);
        }
        double n = 0;
        auto parsed = std::from_chars(t.data(), t.data() + t.size(), n, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != t.data() + t.size() || !std::isfinite(n))
        {
            throw std::domain_error("GeneralNumericRangeUnestablished");
        }
        return n; // Source lexemes remain owned by the immutable snapshot.
    }
    double N(Index a, std::string_view name) const
    {
        return Number(F(a, name));
    }
    V Triple(const step_source::Value& v) const
    {
        if (v.children.size() != 3)
        {
            throw std::domain_error("GeneralRequires3DGeometry");
        }
        return { Number(v.children[0]), Number(v.children[1]), Number(v.children[2]) };
    }
    V Position(Index a) const
    {
        const auto point = Mul(Triple(F(a, "coordinates")), scale);
        for (const auto value : point)
        {
            if (!std::isfinite(value))
            {
                throw std::domain_error("GeneralLengthRangeUnestablished");
            }
        }
        return point;
    }
    V Direction(Index a) const
    {
        return Unit(Triple(F(a, "direction_ratios")));
    }
    Frame Placement(Index a) const
    {
        Frame f;
        f.o = Position(Ref(a, "location"));
        f.z = F(a, "axis").kind == step_source::ValueKind::Unset ? V{ 0, 0, 1 } : Direction(Ref(a, "axis"));
        V x;
        if (F(a, "ref_direction").kind != step_source::ValueKind::Unset)
        {
            x = Direction(Ref(a, "ref_direction"));
        }
        else
        {
            x = f.z[1] != 0 || f.z[2] != 0 ? V{ 1, 0, 0 } : V{ 0, 1, 0 };
        }
        // STEP build_axes projects the reference direction onto the normal
        // plane. Ratios are dimensionless; arbitrary source magnitudes are not scales.
        // AP214E3_2010.exp FIRST_PROJ_AXIS defaults to X unless exactly
        // parallel to it. The double cross avoids cancellation near parallel.
        f.x = Unit(Cross(f.z, Cross(x, f.z)));
        f.y = Unit(Cross(f.z, f.x));
        return f;
    }
    struct ConicTrim
    {
        double first, last, low, high;
        bool full;
        std::optional<double> angleScale;
    };
    ConicTrim TrimmedConic(Index wrapper, const Frame& frame, double rx, double ry, double& measuredConsistency)
    {
        constexpr double tau = 2 * std::numbers::pi;
        const auto evaluate = [&](double t)
        {
            return Add(frame.o, Add(Mul(frame.x, rx * std::cos(t)), Mul(frame.y, ry * std::sin(t))));
        };
        const auto angle = [&](V p)
        {
            const auto d = Sub(p, frame.o);
            return std::atan2(Dot(d, frame.y) / ry, Dot(d, frame.x) / rx);
        };
        std::optional<double> angleScale;
        const auto preferred = F(wrapper, "master_representation").text;
        const auto cut = [&](std::string_view field)
        {
            std::optional<double> parameter, pointAngle;
            std::optional<V> point;
            for (const auto& value : F(wrapper, field).children)
            {
                if (value.kind == step_source::ValueKind::Reference)
                {
                    point = Position(m_source->ReferenceTarget(value));
                    pointAngle = angle(*point);
                    const auto gap = Norm(Sub(evaluate(*pointAngle), *point));
                    measuredConsistency = std::max(measuredConsistency, gap);
                    Need(std::isfinite(gap) && gap <= Q(m_options.quality.consistencyMm), "GeneralTrimmedPointOffBasis", wrapper);
                }
                else
                {
                    if (!angleScale)
                    {
                        for (const auto& unit : m_source->UnitContext().siDefinitions)
                        {
                            if (unit.recordIndex == m_source->UnitContext().planeAngleUnit && unit.siName == "RADIAN")
                            {
                                angleScale = std::pow(10., unit.canonicalPower10);
                            }
                        }
                        if (!angleScale || !std::isfinite(*angleScale) || !(*angleScale > 0))
                        {
                            Fail("GeneralTrimmedAngleUnitUnestablished", wrapper, Status::Unsupported);
                        }
                    }
                    parameter = Number(value.children.at(0)) * *angleScale;
                    // Limit argument reduction to a small, explicit native
                    // development range. This is not an EXPRESS restriction.
                    if (!std::isfinite(*parameter) || std::abs(*parameter) > 2 * tau)
                    {
                        Fail("GeneralTrimmedAngleRangeUnestablished", wrapper, Status::Unsupported);
                    }
                }
            }
            if (parameter && point)
            {
                // Master preference chooses the defining cut but does not
                // excuse a conflicting second representation (4.4.40 IP1/IP2).
                const auto gap = Norm(Sub(evaluate(*parameter), *point));
                measuredConsistency = std::max(measuredConsistency, gap);
                Need(std::isfinite(gap) && gap <= Q(m_options.quality.consistencyMm), "GeneralTrimmedRepresentationsConflict", wrapper);
            }
            // With UNSPECIFIED and two consistent forms, deterministically
            // retain the supplied parameter. With one form, use that form.
            return preferred == "CARTESIAN" && pointAngle ? *pointAngle : parameter ? *parameter : pointAngle.value();
        };
        double first = cut("trim_1"), last = cut("trim_2");
        const bool same = B(wrapper, "sense_agreement");
        double extent = same ? last - first : first - last;
        if (extent < 0 && extent > -tau)
        {
            if (same)
            {
                last += tau;
            }
            else
            {
                first += tau;
            }
            extent += tau;
        }
        // Only binary64 conversion roundoff can identify one period here;
        // consistencyMm is never used to turn a short arc into a closed circle.
        const double roundoff = 32 * std::numeric_limits<double>::epsilon() * tau;
        if (!(extent > 0) || extent > tau + roundoff)
        {
            Fail("GeneralTrimmedConicExtentUnestablished", wrapper, Status::Unsupported);
        }
        return { first, last, std::min(first, last), std::max(first, last), std::abs(extent - tau) <= roundoff, angleScale };
    }
    std::unique_ptr<SmCurve> MakeConic(Index geometry, const Frame& frame, double rx, double ry, const SmExtent1d& domain, bool checked)
    {
        const bool circle = Is(geometry, "CIRCLE");
        std::unique_ptr<SmCurve> result;
        Step(geometry);
        if (checked)
        {
            SmStatus status;
            if (circle)
            {
                SmCircle* curve = nullptr;
                status = SmCircle::CreateCanonical(*m_context, frame.Native(), rx, curve, &domain);
                result.reset(curve);
            }
            else
            {
                SmEllipse* curve = nullptr;
                status = SmEllipse::CreateCanonical(*m_context, frame.Native(), rx, ry, curve, &domain);
                result.reset(curve);
            }
            Good(status); // Own even a nonnull failure result before propagating.
        }
        else if (circle)
        {
            result.reset(new (*m_context) SmCircle(P(frame.o), D(frame.x), D(frame.y), domain, rx, 3, m_context));
        }
        else
        {
            result.reset(new (*m_context) SmEllipse(P(frame.o), D(frame.x), D(frame.y), domain, rx, ry, 3, m_context));
        }
        return result;
    }
    std::unique_ptr<SmCurve> MakeSplineCurve(Index geometry, Index use)
    {
        SmTArray<SmPoint3d> cp;
        for (const auto control : Refs(geometry, "control_points_list"))
        {
            cp.Add(P(Position(control)));
        }
        auto mult = Multiplicities(geometry, "knot_multiplicities");
        auto knots = Numbers(geometry, "knots");
        auto weights = SplineWeights(geometry, false, cp);
        SmBSplineCurve* raw = nullptr;
        Step(geometry);
        const auto status = SmBSplineCurve::CreateCanonical(
            *m_context,
            3,
            Degree(geometry, "degree"),
            cp,
            SM_CF_UNSPECIFIED,
            mult,
            knots,
            SM_KT_UNSPECIFIED,
            weights.GetSize() ? &weights : nullptr,
            nullptr,
            raw
        );
        std::unique_ptr<SmCurve> result(raw);
        Good(status);
        if (weights.GetSize())
        {
            Need(bool(raw), "GeneralCurveAllocation", geometry);
            ULONG count = 0;
            double* net = nullptr;
            Good(raw->GetControlPointsPointer(count, net));
            VerifyRationalNet(geometry, use, cp, weights, net, count, raw->IsRational());
        }
        return result;
    }
    ULONG Degree(Index a, std::string_view name) const
    {
        const double n = N(a, name);
        Need(n >= 1 && n <= 32 && std::floor(n) == n, "GeneralSplineDegree", a);
        return static_cast<ULONG>(n);
    }
    SmTArray<ULONG> Multiplicities(Index a, std::string_view name) const
    {
        SmTArray<ULONG> result;
        for (const auto& value : F(a, name).children)
        {
            const auto n = Number(value);
            Need(n >= 1 && n <= 33 && std::floor(n) == n, "GeneralKnotMultiplicity", a);
            result.Add(static_cast<ULONG>(n));
        }
        return result;
    }
    SmTArray<double> Numbers(Index a, std::string_view name) const
    {
        SmTArray<double> result;
        for (const auto& value : F(a, name).children)
        {
            result.Add(Number(value));
        }
        return result;
    }
    SmTArray<double> SplineWeights(Index geometry, bool surface, const SmTArray<SmPoint3d>& control) const
    {
        SmTArray<double> result;
        if (!Is(geometry, surface ? "RATIONAL_B_SPLINE_SURFACE" : "RATIONAL_B_SPLINE_CURVE"))
        {
            return result;
        }
        const auto& values = F(geometry, "weights_data");
        const auto append = [&](const step_source::Value& value)
        {
            const double weight = Number(value);
            Need(weight > 0, "GeneralWeightRangeUnestablished", geometry);
            result.Add(weight);
        };
        // STEP Cartesian controls are Euclidean, weights dimensionless. Preserve
        // source U rows / V entries and pass weights separately: CreateCanonical
        // performs the one conversion to homogeneous coordinates itself.
        for (const auto& value : values.children)
        {
            if (surface)
            {
                for (const auto& weight : value.children)
                {
                    append(weight);
                }
            }
            else
            {
                append(value);
            }
        }
        Need(result.GetSize() == control.GetSize(), "GeneralWeightNetShape", geometry);
        for (ULONG i = 0; i < result.GetSize(); ++i)
        {
            for (const auto coordinate : P(control[i]))
            {
                const double homogeneous = coordinate * result[i];
                Need(std::isfinite(homogeneous) && (coordinate == 0 || homogeneous != 0), "GeneralHomogeneousRangeUnestablished", geometry);
            }
        }
        return result;
    }
    void VerifyRationalNet(
        Index geometry,
        Index sourceUse,
        const SmTArray<SmPoint3d>& control,
        const SmTArray<double>& weights,
        const double* net,
        ULONG count,
        bool rational
    ) const
    {
        Need(net && count == control.GetSize(), "GeneralRationalNetShapeChanged", geometry);
        // Only an identical positive source weight at EVERY control can be
        // cancelled to one without changing geometry or parameterization.
        // Require literal equality as a sufficient exact source proof; binary64
        // equality alone could hide distinct source decimals rounding together.
        const auto& sourceWeights = F(geometry, "weights_data");
        const bool surface = Is(geometry, "RATIONAL_B_SPLINE_SURFACE");
        const auto& first = surface ? sourceWeights.children.front().children.front() : sourceWeights.children.front();
        bool uniformSource = true;
        for (const auto& value : sourceWeights.children)
        {
            if (surface)
            {
                for (const auto& child : value.children)
                {
                    uniformSource = uniformSource && child.text == first.text;
                }
            }
            else
            {
                uniformSource = uniformSource && value.text == first.text;
            }
        }
        bool cancelled = false;
        // SMLib may also discard near-unity NONuniform surface weights. Reject
        // that unqualified normalization; raw source weights remain unchanged.
        // Stored nonrational W is a sentinel, so interpret it as exactly one.
        for (ULONG i = 0; i < count; ++i)
        {
            const double actualWeight = rational ? net[4 * i + 3] : 1.;
            if (actualWeight != weights[i])
            {
                Need(uniformSource && !rational && actualWeight == 1., "GeneralSourceWeightsChanged", geometry);
                cancelled = true;
            }
            V actual{};
            for (Index axis = 0; axis < 3; ++axis)
            {
                actual[axis] = net[4 * i + axis] / actualWeight;
                Need(std::isfinite(actual[axis]), "GeneralHomogeneousRangeUnestablished", geometry);
            }
            Need(Norm(Sub(actual, P(control[i]))) <= Q(m_options.quality.definitionMm), "GeneralControlRoundtripUnestablished", geometry);
        }
        if (cancelled)
        {
            ParameterMapping trace;
            trace.kind = "ExactUniformWeightCancellation";
            trace.sourceGeometryId = Id(geometry);
            trace.sourceUseId = Id(sourceUse);
            trace.equation = "identical positive source weights cancel from numerator/denominator; source parameters and Euclidean controls unchanged";
            trace.parameters = { { "source_uniform_weight", { weights[0] } }, { "native_uniform_weight", { 1. } } };
            m_report.parameterMappings.push_back(std::move(trace));
        }
    }
};
} // namespace step_import::detail::construction
