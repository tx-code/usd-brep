// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportNativeData.h"
#include "StepImportResult.h"
#include "StepImportWireframeLength.h"

#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmBrepData.h>
#include <SmNativeArchive.h>
#include <SmPlane.h>
#include <SmSurface.h>
#include <SmVertex.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace step_import
{
namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
void Good(SmStatus status)
{
    Require(status == SM_SUCCESS, "Linked geometry native operation failed");
}
Point Coordinates(const SmPoint3d& point)
{
    return { point.x, point.y, point.z };
}
void FinitePoint(const Point& point)
{
    if (!std::all_of(
            point.begin(),
            point.end(),
            [](double value)
            {
                return std::isfinite(value);
            }
        ))
    {
        throw std::invalid_argument("Finite linked geometry query required");
    }
}
double Distance(Point a, Point b)
{
    return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}
class LengthQuery
{
public:

    LengthQuery(const SmCurve& curve, std::array<double, 2> interval, double accuracy) : m_curve(curve), m_interval(interval), m_accuracy(accuracy)
    {
    }
    detail::Result<double> Run()
    {
        if (!std::isfinite(m_interval[0]) || !std::isfinite(m_interval[1]) || !(m_interval[1] > m_interval[0]))
        {
            return Refuse("LinkedCurveLengthInterval");
        }
        SmTArray<double> knots;
        if (m_curve.GetKnots(knots) != SM_SUCCESS)
        {
            return Refuse("LinkedCurveLengthNativeKnots");
        }
        if (knots.GetSize() > MaximumSpans + 1)
        {
            return Refuse("LinkedCurveLengthSpanBudget");
        }
        std::vector<double> cuts{ m_interval[0] };
        for (const auto knot : knots)
        {
            if (!std::isfinite(knot))
            {
                return Refuse("LinkedCurveLengthNativeKnots");
            }
            if (knot > m_interval[0] && knot < m_interval[1] && knot != cuts.back())
            {
                if (knot < cuts.back())
                {
                    return Refuse("LinkedCurveLengthNativeKnots");
                }
                cuts.push_back(knot);
            }
        }
        cuts.push_back(m_interval[1]);
        const auto spans = cuts.size() - 1;
        // Half the explicit absolute stopping allowance is reserved for
        // aggregation/roundoff. This is numerical quadrature evidence, not a
        // continuous integration/evaluator error certificate.
        const double seedAccuracy = m_accuracy / (2. * double(spans) * Seeds);
        if (!std::isfinite(seedAccuracy) || !(seedAccuracy > 0))
        {
            return Refuse("LinkedCurveLengthAccuracyUnestablished");
        }
        double total = 0, compensation = 0;
        for (std::size_t span = 0; span < spans; ++span)
        {
            m_low = cuts[span];
            m_high = cuts[span + 1];
            m_width = m_high - m_low;
            if (!std::isfinite(m_width) || !(m_width > 0))
            {
                return Refuse("LinkedCurveLengthParameterRange");
            }
            auto first = Speed(0);
            if (!first)
            {
                return tl::unexpected(std::move(first.error()));
            }
            for (unsigned seed = 0; seed < Seeds; ++seed)
            {
                const double a = double(seed) / Seeds, b = double(seed + 1) / Seeds, middle = std::midpoint(a, b);
                auto center = Speed(middle);
                if (!center)
                {
                    return tl::unexpected(std::move(center.error()));
                }
                auto last = Speed(b);
                if (!last)
                {
                    return tl::unexpected(std::move(last.error()));
                }
                const double estimate = Simpson(a, b, *first, *center, *last);
                auto length = Refine(a, b, *first, *center, *last, estimate, seedAccuracy, 0);
                if (!length)
                {
                    return tl::unexpected(std::move(length.error()));
                }
                const double increment = *length - compensation, sum = total + increment;
                compensation = (sum - total) - increment;
                total = sum;
                first = std::move(last);
            }
        }
        if (auto guard = Check(); !guard)
        {
            return tl::unexpected(std::move(guard.error()));
        }
        if (!std::isfinite(total) || !(total > 0) || m_accuracy <= 256 * std::numeric_limits<double>::epsilon() * total)
        {
            return Refuse("LinkedCurveLengthAccuracyUnestablished");
        }
        return total;
    }

private:

    static constexpr unsigned Seeds = 8, MaximumDepth = 20;
    static constexpr std::size_t MaximumEvaluations = 100000, MaximumSpans = 4096;
    const SmCurve& m_curve;
    std::array<double, 2> m_interval;
    double m_accuracy, m_low = 0, m_high = 0, m_width = 0;
    std::size_t m_evaluations = 0;
    std::chrono::steady_clock::time_point m_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    static tl::unexpected<detail::Failure> Refuse(const char* key)
    {
        return tl::unexpected(
            detail::Failure{ Status::Indeterminate, key, "Bounded native derivative length query is unestablished", {}, "WireframeQuery" }
        );
    }
    detail::Result<void> Check() const
    {
        if (std::chrono::steady_clock::now() >= m_deadline)
        {
            return Refuse("LinkedCurveLengthDeadline");
        }
        return {};
    }
    detail::Result<double> Speed(double u)
    {
        if (auto guard = Check(); !guard)
        {
            return tl::unexpected(std::move(guard.error()));
        }
        if (m_evaluations == MaximumEvaluations)
        {
            return Refuse("LinkedCurveLengthEvaluationBudget");
        }
        ++m_evaluations;
        const double t = std::lerp(m_low, m_high, u);
        if ((u > 0 && !(t > m_low)) || (u < 1 && !(t < m_high)))
        {
            return Refuse("LinkedCurveLengthParameterResolution");
        }
        SmVector3d derivatives[2];
        // SmCurve::Evaluate defaults to manufactured nonzero tangents. Length
        // needs the ACTUAL derivative magnitude, and one-sided derivatives at
        // each original knot. No physical shape is inferred from a tiny t span.
        const auto status = m_curve.Evaluate(t, 1, u == 1 ? FALSE : TRUE, derivatives, FALSE);
        if (auto guard = Check(); !guard)
        {
            return tl::unexpected(std::move(guard.error()));
        }
        if (status != SM_SUCCESS || !std::isfinite(derivatives[0].x) || !std::isfinite(derivatives[0].y) || !std::isfinite(derivatives[0].z) ||
            !std::isfinite(derivatives[1].x) || !std::isfinite(derivatives[1].y) || !std::isfinite(derivatives[1].z))
        {
            return Refuse("LinkedCurveLengthNativeDerivative");
        }
        const double speed = std::hypot(derivatives[1].x * m_width, derivatives[1].y * m_width, derivatives[1].z * m_width);
        if (!std::isfinite(speed))
        {
            return Refuse("LinkedCurveLengthDerivativeRange");
        }
        return speed;
    }
    static double Simpson(double a, double b, double first, double center, double last)
    {
        return (b - a) * (first / 6 + (2. / 3.) * center + last / 6);
    }
    detail::Result<double> Refine(double a, double b, double first, double center, double last, double estimate, double accuracy, unsigned depth)
    {
        const double middle = std::midpoint(a, b), leftMiddle = std::midpoint(a, middle), rightMiddle = std::midpoint(middle, b);
        if (!(leftMiddle > a && leftMiddle < middle && rightMiddle > middle && rightMiddle < b))
        {
            return Refuse("LinkedCurveLengthParameterResolution");
        }
        auto leftSpeed = Speed(leftMiddle);
        if (!leftSpeed)
        {
            return tl::unexpected(std::move(leftSpeed.error()));
        }
        auto rightSpeed = Speed(rightMiddle);
        if (!rightSpeed)
        {
            return tl::unexpected(std::move(rightSpeed.error()));
        }
        const double left = Simpson(a, middle, first, *leftSpeed, center), right = Simpson(middle, b, center, *rightSpeed, last);
        const double refined = left + right, delta = refined - estimate;
        const double roundoff = 64 * std::numeric_limits<double>::epsilon() * (std::abs(refined) + std::abs(estimate));
        if (!std::isfinite(refined) || !std::isfinite(delta) || !std::isfinite(roundoff) || !(accuracy > roundoff))
        {
            return Refuse("LinkedCurveLengthAccuracyUnestablished");
        }
        if (std::abs(delta) / 15 <= accuracy - roundoff)
        {
            const double corrected = refined + delta / 15;
            if (!std::isfinite(corrected) || corrected < 0)
            {
                return Refuse("LinkedCurveLengthIntegrationRange");
            }
            return corrected;
        }
        if (depth == MaximumDepth || !(accuracy / 2 > 0))
        {
            return Refuse("LinkedCurveLengthRefinementBudget");
        }
        auto leftLength = Refine(a, middle, first, *leftSpeed, center, left, accuracy / 2, depth + 1);
        if (!leftLength)
        {
            return tl::unexpected(std::move(leftLength.error()));
        }
        auto rightLength = Refine(middle, b, center, *rightSpeed, last, right, accuracy / 2, depth + 1);
        if (!rightLength)
        {
            return tl::unexpected(std::move(rightLength.error()));
        }
        const double length = *leftLength + *rightLength;
        if (!std::isfinite(length))
        {
            return Refuse("LinkedCurveLengthIntegrationRange");
        }
        return length;
    }
};
// The part reader returns independent owning arrays. Its failure can also leave
// partial output: clear every array before its allocating context in all paths.
// This owner is used only under NativeMutex, just like the snapshot writer.
struct ReadPart
{
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    SmObjsDelete<SmCurve*> deleteCurves{ &curves };
    SmObjsDelete<SmSurface*> deleteSurfaces{ &surfaces };
    SmObjsDelete<SmBrep*> deleteBreps{ &breps };
};
void CompareCurve(const SmCurve& source, const SmCurve& read, double toleranceMm)
{
    Require(source.GetType() == read.GetType() && source.GetDim() == read.GetDim(), "Linked archive curve type changed");
    const auto a = dynamic_cast<const SmBSplineCurve*>(&source), b = dynamic_cast<const SmBSplineCurve*>(&read);
    Require(a && b && a->GetDegree() == b->GetDegree(), "Linked archive curve degree changed");
    Require(
        a->GetNaturalInterval().GetMin() == b->GetNaturalInterval().GetMin() && a->GetNaturalInterval().GetMax() == b->GetNaturalInterval().GetMax(),
        "Linked archive curve interval changed"
    );
    SmTArray<SmPoint3d> pa, pb;
    SmTArray<double> wa, wb, ka, kb;
    Good(a->GetControlPolygon(pa, wa));
    Good(b->GetControlPolygon(pb, wb));
    Good(a->GetKnotsAll(ka));
    Good(b->GetKnotsAll(kb));
    Require(pa.GetSize() == pb.GetSize() && wa.GetSize() == wb.GetSize() && ka.GetSize() == kb.GetSize(), "Linked archive curve net changed");
    for (ULONG i = 0; i < pa.GetSize(); ++i)
    {
        Require(pa[i].DistanceBetween(pb[i]) <= toleranceMm, "Linked archive curve coefficient changed");
    }
    for (ULONG i = 0; i < wa.GetSize(); ++i)
    {
        Require(std::isfinite(wb[i]) && std::abs(wa[i] - wb[i]) <= 1e-12, "Linked archive curve weight changed");
    }
    for (ULONG i = 0; i < ka.GetSize(); ++i)
    {
        Require(ka[i] == kb[i], "Linked archive curve knot changed");
    }
}
void ComparePlane(const SmSurface& source, const SmSurface& read, double toleranceMm)
{
    const auto a = dynamic_cast<const SmPlane*>(&source), b = dynamic_cast<const SmPlane*>(&read);
    Require(a && b && source.GetType() == read.GetType(), "Supporting archive analytic plane type changed");
    const auto& pa = a->GetPosition();
    const auto& pb = b->GetPosition();
    Require(pa.GetOriginRef().DistanceBetween(pb.GetOriginRef()) <= toleranceMm, "Supporting archive origin changed");
    const auto exact = [](const auto& x, const auto& y)
    {
        return x.x == y.x && x.y == y.y && x.z == y.z;
    };
    Require(exact(pa.GetXAxisRef(), pb.GetXAxisRef()) && exact(pa.GetYAxisRef(), pb.GetYAxisRef()), "Supporting archive frame axes changed");
    const auto da = a->GetNaturalUVDomain(), db = b->GetNaturalUVDomain();
    const auto sa = a->GetUVScale(), sb = b->GetUVScale();
    Require(
        da.GetMin().x == db.GetMin().x && da.GetMin().y == db.GetMin().y && da.GetMax().x == db.GetMax().x && da.GetMax().y == db.GetMax().y &&
            sa.x == sb.x && sa.y == sb.y,
        "Supporting archive plane domain or scale changed"
    );
}
void Validate(SmBrep& brep, const char* role)
{
    SmAssertArray pointers, deep;
    Good(brep.ValidatePointers(&pointers));
    if (pointers.GetSize())
    {
        std::string message = std::string("Linked archive pointer validation failed: ") + role;
        for (ULONG i = 0; i < std::min<ULONG>(pointers.GetSize(), 32); ++i)
        {
            // Bounded predicate identities survive cleanup without publishing
            // native addresses or incidental geometry in an exception string.
            message += " [type=" + std::to_string(pointers[i]->GetOwnerType()) + ",test=" + std::to_string(pointers[i]->GetTestIndex()) + "]";
        }
        throw std::runtime_error(message);
    }
    Require(brep.AssertValid(&deep, SM_LEVEL_2, SM_WALK) && deep.GetSize() == 0, "Linked archive level2 validation failed");
}
} // namespace

namespace detail
{
Result<double> QueryWireframeCurveLength(const SmCurve& curve, std::array<double, 2> interval, double absoluteAccuracyMm)
{
    if (!std::isfinite(absoluteAccuracyMm) || !(absoluteAccuracyMm > 0))
    {
        return tl::unexpected(
            Failure{ Status::Indeterminate,
                     "LinkedCurveLengthAccuracyUnestablished",
                     "Positive finite linked curve length accuracy required",
                     {},
                     "WireframeQuery" }
        );
    }
    const auto natural = curve.GetNaturalInterval();
    if (!std::isfinite(natural.GetMin()) || !std::isfinite(natural.GetMax()) || !(natural.GetMax() > natural.GetMin()) ||
        interval[0] < natural.GetMin() || interval[1] > natural.GetMax())
    {
        return tl::unexpected(
            Failure{ Status::Indeterminate,
                     "LinkedCurveLengthInterval",
                     "Length query must remain in the actual native curve domain",
                     {},
                     "WireframeQuery" }
        );
    }
    return LengthQuery(curve, interval, absoluteAccuracyMm).Run();
}
} // namespace detail

WireframeView::WireframeView(std::shared_ptr<detail::ModelData> data, std::size_t set) : m_data(std::move(data)), m_set(set)
{
}
WireframeInfo WireframeView::Info() const
{
    return m_data->wireframes.at(m_set);
}
step_semantics::SchemaInterpretation WireframeView::AppliedSourceInterpretation() const
{
    return m_data->source->AppliedInterpretation();
}
step_semantics::UnitInterpretation WireframeView::AppliedUnitInterpretation() const
{
    return m_data->source->AppliedUnitInterpretation();
}
bool WireframeView::HasAsciiInvariantDataAssumption() const
{
    return m_data->source->Source().HasAsciiInvariantDataAssumption();
}
const step_source::CarrierAssumption* WireframeView::AppliedCarrierAssumption() const
{
    return m_data->source->Source().AppliedCarrierAssumption();
}
std::vector<WireframeView> ModelSnapshot::Wireframes() const
{
    std::vector<WireframeView> result;
    result.reserve(m_data->wireframes.size());
    for (std::size_t i = 0; i < m_data->wireframes.size(); ++i)
    {
        result.push_back(WireframeView(m_data, i));
    }
    return result;
}
Point WireframeView::EvaluateCurve(std::size_t curve, double parameter) const
{
    const auto& info = m_data->wireframes.at(m_set).curves.at(curve);
    if (!std::isfinite(parameter) || parameter < info.parameterDomain[0] || parameter > info.parameterDomain[1])
    {
        throw std::out_of_range("Linked curve query lies outside its native interval");
    }
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard lock(m_data->mutex);
    SmPoint3d point;
    Good(m_data->curvePointDefinitions.CurveAt(detail::CurveIndex{ info.nativeIndex }).EvaluatePoint(parameter, point));
    const auto result = Coordinates(point);
    FinitePoint(result);
    return result;
}
double WireframeView::CurveLength(std::size_t curve, double absoluteAccuracyMm) const
{
    if (!std::isfinite(absoluteAccuracyMm) || absoluteAccuracyMm <= 0)
    {
        throw std::invalid_argument("Positive finite linked curve length accuracy required");
    }
    const auto& info = m_data->wireframes.at(m_set).curves.at(curve);
    // Native integral caches are shared mutable implementation state even
    // though the defining curve is immutable. Match evaluator lock ordering.
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard lock(m_data->mutex);
    // The legacy SmCurve::Length can return an endpoint chord after a failed
    // tessellation, and its integrator can hide an ODE failure as zero. Use
    // bounded derivative quadrature with checked status, preserving actual t.
    auto length = detail::QueryWireframeCurveLength(
        m_data->curvePointDefinitions.CurveAt(detail::CurveIndex{ info.nativeIndex }),
        info.parameterDomain,
        absoluteAccuracyMm
    );
    if (!length)
    {
        throw std::runtime_error(length.error().key);
    }
    return *length;
}
ClosestPoint WireframeView::Closest(const Point& point) const
{
    FinitePoint(point);
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard lock(m_data->mutex);
    ClosestPoint result{ {}, std::numeric_limits<double>::infinity() };
    const auto take = [&](const Point& candidate)
    {
        FinitePoint(candidate);
        const auto distance = Distance(point, candidate);
        Require(std::isfinite(distance), "Linked closest distance is not finite");
        if (distance < result.distanceMm)
        {
            result = { candidate, distance };
        }
    };
    const auto& set = m_data->wireframes.at(m_set);
    for (const auto& info : set.points)
    {
        take(info.pointMm);
        if (info.pointMm == point)
        {
            // Exact coordinate coincidence proves the set's global minimum 0;
            // other members cannot improve it (or require an ambiguous solve).
            return result;
        }
    }
    for (const auto& info : set.curves)
    {
        const auto& curve = m_data->curvePointDefinitions.CurveAt(detail::CurveIndex{ info.nativeIndex });
        SmBoolean found = FALSE;
        double parameter = 0, distance = 0;
        // No guess selects the native global search, and no snapping vector is
        // supplied. A failed member prevents a purported whole-set answer.
        Good(curve.DropPoint(
            SmExtent1d(info.parameterDomain[0], info.parameterDomain[1]),
            SmPoint3d(point[0], point[1], point[2]),
            nullptr,
            m_data->queryTolerance,
            nullptr,
            found,
            parameter,
            distance,
            SM_SO_MINIMIZE
        ));
        Require(
            found && std::isfinite(parameter) && std::isfinite(distance) && distance >= 0 && parameter >= info.parameterDomain[0] &&
                parameter <= info.parameterDomain[1],
            "Linked closest solution unestablished"
        );
        SmPoint3d candidate;
        Good(curve.EvaluatePoint(parameter, candidate));
        take(Coordinates(candidate));
        if (Coordinates(candidate) == point)
        {
            return result;
        }
    }
    Require(std::isfinite(result.distanceMm), "Linked source set has no queryable geometry");
    return result;
}

SupportingGeometryView::SupportingGeometryView(std::shared_ptr<detail::ModelData> data, std::size_t set) : m_data(std::move(data)), m_set(set)
{
}
SupportingGeometryInfo SupportingGeometryView::Info() const
{
    return m_data->supportingGeometry.at(m_set);
}
step_semantics::SchemaInterpretation SupportingGeometryView::AppliedSourceInterpretation() const
{
    return m_data->source->AppliedInterpretation();
}
step_semantics::UnitInterpretation SupportingGeometryView::AppliedUnitInterpretation() const
{
    return m_data->source->AppliedUnitInterpretation();
}
bool SupportingGeometryView::HasAsciiInvariantDataAssumption() const
{
    return m_data->source->Source().HasAsciiInvariantDataAssumption();
}
const step_source::CarrierAssumption* SupportingGeometryView::AppliedCarrierAssumption() const
{
    return m_data->source->Source().AppliedCarrierAssumption();
}
std::vector<SupportingGeometryView> ModelSnapshot::SupportingGeometry() const
{
    std::vector<SupportingGeometryView> result;
    result.reserve(m_data->supportingGeometry.size());
    for (std::size_t i = 0; i < m_data->supportingGeometry.size(); ++i)
    {
        result.push_back(SupportingGeometryView(m_data, i));
    }
    return result;
}
Point SupportingGeometryView::EvaluateCurve(std::size_t curve, double parameter) const
{
    const auto& info = m_data->supportingGeometry.at(m_set).curves.at(curve);
    if (!std::isfinite(parameter) || parameter < info.parameterDomain[0] || parameter > info.parameterDomain[1])
    {
        throw std::out_of_range("Supporting curve query outside native interval");
    }
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard lock(m_data->mutex);
    SmPoint3d point;
    Good(m_data->curvePointDefinitions.CurveAt(detail::CurveIndex{ info.nativeIndex }).EvaluatePoint(parameter, point));
    const auto result = Coordinates(point);
    FinitePoint(result);
    return result;
}
Point SupportingGeometryView::EvaluatePlane(std::size_t plane, double u, double v) const
{
    const auto& info = m_data->supportingGeometry.at(m_set).planes.at(plane);
    if (!std::isfinite(u) || !std::isfinite(v) || u < info.parameterDomain[0] || v < info.parameterDomain[1] || u > info.parameterDomain[2] ||
        v > info.parameterDomain[3])
    {
        throw std::out_of_range("Supporting plane query outside native representable chart");
    }
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard lock(m_data->mutex);
    SmPoint3d point;
    Good(m_data->supportingSurfaces.at(info.nativeSurfaceIndex)->EvaluatePoint(SmPoint2d(u, v), point));
    const auto result = Coordinates(point);
    FinitePoint(result);
    return result;
}
ClosestPoint SupportingGeometryView::ClosestPlane(std::size_t plane, const Point& point) const
{
    FinitePoint(point);
    const auto& info = m_data->supportingGeometry.at(m_set).planes.at(plane);
    double dx = 0, dy = 0, xx = 0, xy = 0, yy = 0;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        const auto delta = point[axis] - info.frame.originMm[axis];
        dx += delta * info.frame.x[axis];
        dy += delta * info.frame.y[axis];
        xx += info.frame.x[axis] * info.frame.x[axis];
        xy += info.frame.x[axis] * info.frame.y[axis];
        yy += info.frame.y[axis] * info.frame.y[axis];
    }
    // Binary64 build_axes is only approximately orthonormal. Solve the small
    // Gram system for the actual retained native axes, rather than assuming
    // unit norms and zero cross term when projecting onto their exact span.
    const auto determinant = xx * yy - xy * xy;
    Require(std::isfinite(determinant) && determinant > 0, "Supporting plane projection frame unestablished");
    const auto u = (dx * yy - dy * xy) / determinant;
    const auto v = (dy * xx - dx * xy) / determinant;
    // The source plane has no trim. Its native chart uses finite sentinels:
    // inability to represent a projection is an explicit query failure, never
    // an answer clamped to an artificial boundary. This is not material scope.
    const auto projected = EvaluatePlane(plane, u, v);
    const auto distance = Distance(projected, point);
    Require(std::isfinite(distance), "Supporting plane distance unestablished");
    return { projected, distance };
}

namespace detail
{
void WriteWireframeArchive(const ModelData& model, const std::filesystem::path& path)
{
    // SRR does not imply containment or union. Store an existing BRep first,
    // independent curves in their own part array, and one point adapter BRep
    // per source point. Even equal coordinates keep distinct source identities.
    // These vertex-only BReps are an archive format adapter, not STEP vertices.
    auto context = std::make_unique<SmContext>();
    std::vector<std::unique_ptr<SmCurve>> curveOwners;
    std::vector<std::unique_ptr<SmSurface>> surfaceOwners;
    std::vector<std::unique_ptr<SmBrep>> brepOwners;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    for (std::size_t i = 0; i < model.curvePointDefinitions.CurveCount(); ++i)
    {
        const auto& source = model.curvePointDefinitions.CurveAt(CurveIndex{ i });
        SmCurve* raw = nullptr;
        const auto status = source.Copy(*context, raw);
        std::unique_ptr<SmCurve> copy(raw);
        Good(status);
        Require(copy && copy->GetContext() == context.get(), "Linked archive curve copy context mismatch");
        curves.Add(copy.get());
        curveOwners.push_back(std::move(copy));
    }
    for (const auto& source : model.supportingSurfaces)
    {
        SmSurface* raw = nullptr;
        const auto status = source->Copy(*context, raw);
        std::unique_ptr<SmSurface> copy(raw);
        Good(status);
        Require(copy && copy->GetContext() == context.get(), "Supporting archive surface copy context mismatch");
        surfaces.Add(copy.get());
        surfaceOwners.push_back(std::move(copy));
    }
    const ULONG pointOffset = model.brep ? 1 : 0;
    if (model.brep)
    {
        std::unique_ptr<SmBrep> body(new (*context) SmBrep(*model.brep, nullptr, nullptr, TRUE, FALSE));
        brepOwners.push_back(std::move(body)); // Own before any vector growth can fail.
        Require(brepOwners.back()->GetContext() == context.get(), "Linked archive BRep copy context mismatch");
        breps.Add(brepOwners.back().get());
    }
    for (const auto& point : model.curvePointDefinitions.Points())
    {
        std::unique_ptr<SmBrep> ownedAdapter(new (*context) SmBrep());
        auto& adapter = *ownedAdapter;
        adapter.SetTolerance(model.queryTolerance, FALSE);
        SmShell* shell = nullptr;
        SmVertex* vertex = nullptr;
        Good(adapter.MakeShellVertex(nullptr, SmPoint3d(point.pointMm[0], point.pointMm[1], point.pointMm[2]), shell, vertex));
        Require(shell && vertex, "Linked archive point adapter missing");
        vertex->SetTolerance(model.queryTolerance, FALSE);
        Validate(adapter, "point adapter before write");
        breps.Add(&adapter);
        brepOwners.push_back(std::move(ownedAdapter));
    }
    Good(SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, breps, SM_BINARY, TRUE, FALSE));
    // The legacy part writer does not propagate every child-writer status.
    // Verify inventory, finite intervals and every new defining curve net/point
    // by reading the actual file. This stricter archive check is not repair or
    // an import-wide continuous geometry bound; source correspondence is in the
    // owning snapshot/report and is not encoded by the native part container.
    ReadPart read;
    Good(SmNativeArchive::ReadPartFromFile(read.context, path.c_str(), read.curves, read.surfaces, read.trees, read.breps, SM_BINARY));
    Require(
        read.curves.GetSize() == curves.GetSize() && read.breps.GetSize() == breps.GetSize() && read.surfaces.GetSize() == surfaces.GetSize() &&
            read.trees.GetSize() == 0,
        "Linked archive inventory incomplete"
    );
    const double archiveToleranceMm = std::min(model.definitionToleranceMm, 1e-9);
    for (ULONG i = 0; i < curves.GetSize(); ++i)
    {
        Require(read.curves[i], "Linked archive curve missing");
        CompareCurve(model.curvePointDefinitions.CurveAt(CurveIndex{ i }), *read.curves[i], archiveToleranceMm);
        if (!model.brep)
        {
            SmAssertArray deep;
            Require(read.curves[i]->AssertValid(&deep, SM_LEVEL_2, SM_WALK) && deep.GetSize() == 0, "Standalone wire archive curve validity changed");
        }
    }
    for (ULONG i = 0; i < surfaces.GetSize(); ++i)
    {
        Require(read.surfaces[i], "Supporting archive surface missing");
        ComparePlane(*model.supportingSurfaces[i], *read.surfaces[i], archiveToleranceMm);
    }
    for (ULONG i = 0; i < read.breps.GetSize(); ++i)
    {
        Require(read.breps[i], "Linked archive BRep missing");
        Validate(*read.breps[i], i >= pointOffset ? "point adapter after read" : "material BRep after read");
        if (i >= pointOffset)
        {
            SmTArray<SmVertex*> vertices;
            SmTArray<SmEdge*> edges;
            SmTArray<SmFace*> faces;
            read.breps[i]->GetVertices(vertices);
            read.breps[i]->GetEdges(edges);
            read.breps[i]->GetFaces(faces);
            Require(vertices.GetSize() == 1 && edges.GetSize() == 0 && faces.GetSize() == 0, "Linked point adapter topology changed");
            Require(
                Distance(Coordinates(vertices[0]->GetPoint()), model.curvePointDefinitions.Points()[i - pointOffset].pointMm) <= archiveToleranceMm,
                "Linked archive point changed"
            );
        }
    }
    if (model.brep)
    {
        SmBrepCompareResult comparison;
        comparison.dVertexTolerance = archiveToleranceMm;
        Good(model.brep->CompareBreps(read.breps[0], comparison));
        Require(
            comparison.lFaceCountA == comparison.lFaceCountB && comparison.lEdgeCountA == comparison.lEdgeCountB &&
                comparison.lVertexCountA == comparison.lVertexCountB && comparison.lCurveTypeMismatches == 0 &&
                comparison.dMaxVertexGap <= archiveToleranceMm,
            "Linked archive material boundary changed"
        );
    }
}
} // namespace detail
} // namespace step_import
