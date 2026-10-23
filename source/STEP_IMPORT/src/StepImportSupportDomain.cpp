// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportSupportDomain.h"

#include "StepImportProofEnvironment.h"
#include "StepImportTrimMeasurement.h"

#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <SmFaceuse.h>
#include <SmTorus.h>
#include <algorithm>
#include <cmath>
#include <gsl/assert>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace step_import::detail
{
namespace
{
struct SavedTrim
{
    SmEdgeuse* owner;
    std::unique_ptr<SmBSplineCurve> curve;
};
std::array<double, 4> Domain(const SmExtent2d& d)
{
    return { d.GetMin().x, d.GetMin().y, d.GetMax().x, d.GetMax().y };
}
bool Finite(double value)
{
    return std::isfinite(value) && !SM_IS_INFINITE(value);
}
bool IncludeCoordinate(double numerator, double weight, double& lower, double& upper)
{
    if (!std::isfinite(numerator) || !std::isfinite(weight) || !(weight > 0))
    {
        return false;
    }
    const double value = numerator / weight;
    if (!Finite(value))
    {
        return false;
    }
    // Polynomial controls and a zero numerator divide exactly. Otherwise the
    // two adjacent values conservatively enclose a nearest-rounded quotient.
    const double lo = weight == 1 || numerator == 0 ? value : std::nextafter(value, -INFINITY);
    const double hi = weight == 1 || numerator == 0 ? value : std::nextafter(value, INFINITY);
    if (!Finite(lo) || !Finite(hi))
    {
        return false;
    }
    lower = std::min(lower, lo);
    upper = std::max(upper, hi);
    return true;
}
struct RadiusUnestablished
{
};
struct Interval
{
    double low, high;
};
Interval Enclose(double low, double high)
{
    if (!Finite(low) || !Finite(high))
    {
        throw RadiusUnestablished{};
    }
    const auto lower = std::nextafter(low, -INFINITY), upper = std::nextafter(high, INFINITY);
    if (!Finite(lower) || !Finite(upper))
    {
        throw RadiusUnestablished{};
    }
    return { lower, upper };
}
Interval Add(Interval a, Interval b)
{
    return Enclose(a.low + b.low, a.high + b.high);
}
Interval Subtract(Interval a, Interval b)
{
    return Enclose(a.low - b.high, a.high - b.low);
}
Interval Multiply(Interval a, Interval b)
{
    const std::array values{ a.low * b.low, a.low * b.high, a.high * b.low, a.high * b.high };
    if (!std::all_of(values.begin(), values.end(), Finite))
    {
        throw RadiusUnestablished{};
    }
    return Enclose(*std::min_element(values.begin(), values.end()), *std::max_element(values.begin(), values.end()));
}
Interval Divide(Interval a, Interval b)
{
    if (b.low <= 0 && b.high >= 0)
    {
        throw RadiusUnestablished{};
    }
    const std::array values{ a.low / b.low, a.low / b.high, a.high / b.low, a.high / b.high };
    if (!std::all_of(values.begin(), values.end(), Finite))
    {
        throw RadiusUnestablished{};
    }
    return Enclose(*std::min_element(values.begin(), values.end()), *std::max_element(values.begin(), values.end()));
}
using Vector = std::array<Interval, 3>;
Vector VectorOf(const SmVector3d& v)
{
    return { Interval{ v.x, v.x }, Interval{ v.y, v.y }, Interval{ v.z, v.z } };
}
Vector Cross(const Vector& a, const Vector& b)
{
    return { Subtract(Multiply(a[1], b[2]), Multiply(a[2], b[1])),
             Subtract(Multiply(a[2], b[0]), Multiply(a[0], b[2])),
             Subtract(Multiply(a[0], b[1]), Multiply(a[1], b[0])) };
}
Interval Dot(const Vector& a, const Vector& b)
{
    return Add(Add(Multiply(a[0], b[0]), Multiply(a[1], b[1])), Multiply(a[2], b[2]));
}
bool ClosedPlanarChart(SmFace& face, const SmTArray<SmEdgeuse*>& uses, const std::function<void()>& checkpoint)
{
    // Periodic equality alone is insufficient for a bounded planar control
    // hull: a jump by one period can select the other surface region. Require
    // the existing native parameter-resolution closure without taking modulo.
    // This is numerical chart evidence, not a new physical gap allowance.
    for (ULONG i = 0; i < uses.GetSize(); ++i)
    {
        const auto current = uses[i];
        const auto next = current->GetCCWEdgeuse();
        if (!next || next->GetFace() != &face || next->GetLoopuse() != current->GetLoopuse())
        {
            return false;
        }
        const auto a = current->GetUVTrimCurve(), b = next->GetUVTrimCurve();
        SmPoint3d end, start;
        checkpoint();
        if (!a || !b || a->EvaluatePoint(current->GetEdgeParam(1), end) != SM_SUCCESS ||
            b->EvaluatePoint(next->GetEdgeParam(0), start) != SM_SUCCESS || !Finite(end.x) || !Finite(end.y) || !Finite(start.x) ||
            !Finite(start.y) || std::abs(end.x - start.x) > SM_EFF_ZERO_PARAM || std::abs(end.y - start.y) > SM_EFF_ZERO_PARAM)
        {
            return false;
        }
    }
    return true;
}
bool GeneratorRadius(
    const SmTorus& torus,
    const SmExtent2d& actualDomain,
    SupportDomainEvidence& result,
    double maximumMm,
    std::size_t maximumControls,
    std::size_t maximumProducts,
    const std::function<void()>& checkpoint
)
{
    const auto generator = torus.GetGenCurve();
    checkpoint();
    if (!generator || torus.GetSwapUV() || torus.GetInsideOut() || !torus.HasSameParameterization(SM_SP_V, SM_SP_V, generator))
    {
        return false;
    }
    // Extract the original generator's NURBS basis, rather than invoking the
    // analytic circle Trim override, which may rebuild/reparameterize it.
    checkpoint();
    auto restricted = std::unique_ptr<SmBSplineCurve>(new (*torus.GetContext()) SmBSplineCurve(*generator));
    restricted->SetOwner(nullptr);
    auto interval = actualDomain.GetVInterval();
    const auto sourceInterval = restricted->GetNaturalInterval();
    if (interval.GetMin() < sourceInterval.GetMin() || interval.GetMax() > sourceInterval.GetMax())
    {
        return false;
    }
    checkpoint();
    if (restricted->Trim(interval) != SM_SUCCESS)
    {
        return false;
    }
    const auto actual = restricted->GetNaturalInterval();
    if (actual.GetMin() > actualDomain.GetMin().y || actual.GetMax() < actualDomain.GetMax().y)
    {
        return false;
    }
    ULONG count = 0;
    double* net = nullptr;
    if (restricted->GetControlPointsPointer(count, net) != SM_SUCCESS || !net || !count || restricted->GetDim() != 3)
    {
        return false;
    }
    if (count > maximumControls || count > maximumProducts / 64)
    {
        result.status = SupportDomainStatus::WorkLimit;
        return false;
    }
    try
    {
        const auto& frame = torus.GetPosition();
        const auto x = VectorOf(frame.GetXAxisRef());
        const auto cofactor = Cross(VectorOf(frame.GetYAxisRef()), VectorOf(frame.GetZAxis()));
        const auto determinant = Dot(x, cofactor);
        const auto origin = frame.GetOriginRef();
        std::array<double, 3> offset{ origin.x, origin.y, origin.z };
        double lower = INFINITY, upper = -INFINITY;
        for (ULONG i = 0; i < count; ++i)
        {
            checkpoint();
            const double w = restricted->IsRational() ? net[4 * i + 3] : 1.;
            if (!Finite(w) || !(w > 0))
            {
                return false;
            }
            Vector delta;
            for (std::size_t k = 0; k < 3; ++k)
            {
                if (!Finite(net[4 * i + k]))
                {
                    return false;
                }
                delta[k] = Subtract({ net[4 * i + k], net[4 * i + k] }, Multiply({ w, w }, { offset[k], offset[k] }));
            }
            // Use the inverse frame's cofactor, with outward arithmetic, rather
            // than treating rounded native axes as exactly orthonormal. Positive
            // rational weights bound the whole retained generator by its poles.
            const auto radius = Divide(Dot(delta, cofactor), Multiply({ w, w }, determinant));
            lower = std::min(lower, radius.low);
            upper = std::max(upper, radius.high);
        }
        result.generatorRadiusBoundsMm = { lower, upper };
        result.minimumAbsoluteGeneratorRadiusMm = lower > 0 ? lower : upper < 0 ? -upper : 0;
        return Finite(result.minimumAbsoluteGeneratorRadiusMm) && result.minimumAbsoluteGeneratorRadiusMm > maximumMm;
    }
    catch (const RadiusUnestablished&)
    {
        return false;
    }
}
} // namespace
SupportDomainEvidence RestrictUnusedSupportDomain(
    SmFace& face,
    bool uniqueExplicitOuter,
    double maximumMm,
    std::size_t maximumControls,
    std::size_t maximumProducts,
    const std::function<void()>& beforeNativeCall,
    const SmTorus* originalTorus
)
{
    Expects(bool(beforeNativeCall));
    Expects(std::isfinite(maximumMm) && maximumMm > 0);
    SupportDomainEvidence result;
    if (!SurfaceProofNumericEnvironment())
    {
        result.status = SupportDomainStatus::Unestablished;
        result.reason = "NumericEnvironmentUnestablished";
        return result;
    }
    const std::function<void()> checkedCall = [&]
    {
        beforeNativeCall();
        if (!SurfaceProofNumericEnvironment())
        {
            throw std::runtime_error("SupportDomainNumericEnvironmentChanged");
        }
    };
    auto surface = dynamic_cast<SmBSplineSurface*>(face.GetSurface());
    const bool spindle = originalTorus && originalTorus->GetMajorRadius() <= originalTorus->GetMinorRadius();
    // Analytic types have separate defining data. Trimming only their NURBS
    // cache would not establish an equivalent analytic representation.
    if (!uniqueExplicitOuter || !surface || surface->GetType() != SmBSplineSurface_TYPE ||
        (originalTorus && (!spindle || originalTorus->GetContext() != face.GetContext())))
    {
        return result;
    }
    const auto natural = surface->GetNaturalUVDomain();
    result.original = Domain(natural);
    if (spindle && result.original != Domain(originalTorus->GetNaturalUVDomain()))
    {
        return result;
    }
    if (!std::all_of(result.original.begin(), result.original.end(), Finite))
    {
        return result;
    }
    checkedCall();
    if (!spindle && (surface->IsClosed(natural, SM_SP_U) || surface->IsClosed(natural, SM_SP_V) || surface->IsPeriodic(natural, SM_SP_U) ||
                     surface->IsPeriodic(natural, SM_SP_V)))
    {
        return result;
    }
    SmTArray<SmEdgeuse*> uses;
    face.GetUpwardEdgeuses(uses);
    if (uses.GetSize() == 0)
    {
        return result;
    }
    // Every control-hull restriction needs a bounded region in one UV chart,
    // including ordinary B-spline supports. A spatially acceptable endpoint
    // gap or a collapsed surface boundary cannot supply a missing UV join.
    // Refuse unestablished coverage before copying or replacing any geometry.
    if (!ClosedPlanarChart(face, uses, checkedCall))
    {
        result.status = SupportDomainStatus::Unestablished;
        result.reason = "PlanarChartClosureUnestablished";
        return result;
    }
    if (spindle)
    {
        result.status = SupportDomainStatus::Unestablished;
        result.reason = "TrimControlHullUnestablished";
    }
    std::array<double, 4> bounds{ INFINITY, INFINITY, -INFINITY, -INFINITY };
    std::size_t totalControls = 0;
    for (ULONG i = 0; i < uses.GetSize(); ++i)
    {
        const auto edge = uses[i]->GetEdge();
        const auto uv = uses[i]->GetUVTrimCurve();
        if (!edge || !edge->GetCurve() || !uv || uv->GetDim() != 2)
        {
            return result;
        }
        const auto interval = edge->GetInterval();
        try
        {
            CheckFinalTrimParameters(*edge->GetCurve(), *uv, interval.GetMin(), interval.GetMax());
        }
        catch (const std::runtime_error&)
        {
            return result;
        }
        const auto uvDomain = uv->GetNaturalInterval();
        // The native validator tolerates endpoint roundoff, but a control hull
        // certifies only the curve's natural interval, not extrapolated values.
        // Refuse uncertain region coverage instead of clamping edge parameters.
        if (interval.GetMin() < uvDomain.GetMin() || interval.GetMax() > uvDomain.GetMax())
        {
            return result;
        }
        ULONG count = 0;
        double* net = nullptr;
        if (uv->GetControlPointsPointer(count, net) != SM_SUCCESS || !net || count == 0)
        {
            return result;
        }
        if (count > maximumControls - std::min(totalControls, maximumControls))
        {
            result.status = SupportDomainStatus::WorkLimit;
            return result;
        }
        totalControls += count;
        for (ULONG j = 0; j < count; ++j)
        {
            const double w = uv->IsRational() ? net[4 * j + 3] : 1.;
            if (!IncludeCoordinate(net[4 * j], w, bounds[0], bounds[2]) || !IncludeCoordinate(net[4 * j + 1], w, bounds[1], bounds[3]))
            {
                return result;
            }
        }
    }
    if (!(bounds[0] < bounds[2]) || !(bounds[1] < bounds[3]) || bounds[0] < result.original[0] || bounds[1] < result.original[1] ||
        bounds[2] > result.original[2] || bounds[3] > result.original[3] || bounds == result.original)
    {
        return result;
    }
    result.requested = bounds;
    result.status = SupportDomainStatus::Unestablished;
    result.reason = "OriginalBasisExtractionUnestablished";
    SmSurface* copy = nullptr;
    checkedCall();
    const auto copied = surface->Copy(*face.GetContext(), copy);
    if (copy == surface)
    {
        return result; // Never adopt or detach a borrowed alias.
    }
    std::unique_ptr<SmSurface> candidate(copy);
    if (candidate)
    {
        candidate->SetOwner(nullptr);
    }
    auto restricted = dynamic_cast<SmBSplineSurface*>(candidate.get());
    if (copied != SM_SUCCESS || !restricted)
    {
        return result;
    }
    if (spindle)
    {
        // The resulting object is an ordinary rational B-spline support, not
        // an analytic object with an independently rebuilt generator/cache.
        restricted->SetBSplineSurfaceForm(SM_SF_UNSPECIFIED);
    }
    SmExtent2d requested(bounds[0], bounds[1], bounds[2], bounds[3]);
    checkedCall();
    if (restricted->TrimWithDomain(requested) != SM_SUCCESS)
    {
        return result;
    }
    result.actual = Domain(restricted->GetNaturalUVDomain());
    // The kernel may snap extraction bounds outward to avoid tiny knot spans.
    // The actual domain, not the request, must retain the complete control box.
    if (result.actual[0] > bounds[0] || result.actual[1] > bounds[1] || result.actual[2] < bounds[2] || result.actual[3] < bounds[3])
    {
        return result;
    }
    result.reason = "GeneratorRegularityUnestablished";
    if (spindle &&
        !GeneratorRadius(*originalTorus, restricted->GetNaturalUVDomain(), result, maximumMm, maximumControls, maximumProducts, checkedCall))
    {
        return result;
    }
    result.reason = "ContinuousRestrictionUnestablished";
    const auto& originalDefinition = spindle ? static_cast<const SmBSplineSurface&>(*originalTorus) : *surface;
    result.numeric = ProveSurfaceRestriction(originalDefinition, *restricted, maximumMm, maximumControls, maximumProducts, checkedCall);
    if (!result.numeric.established)
    {
        if (result.numeric.workLimit)
        {
            result.status = SupportDomainStatus::WorkLimit;
        }
        return result;
    }
    if (spindle)
    {
        result.analyticDomainDegrees = Domain(originalTorus->GetSTEPUVDomain());
    }
    // SetSurface invalidates/deletes derived UV caches. Save independent exact
    // copies first, keyed by the actual SAME owner of each mated pair.
    std::set<SmEdgeuse*> owners;
    std::vector<SavedTrim> saved;
    for (ULONG i = 0; i < uses.GetSize(); ++i)
    {
        SmEdgeuse* owner = nullptr;
        const auto old = uses[i]->GetUVTrimCurve(&owner);
        if (!old || !owner)
        {
            return result;
        }
        if (!owners.insert(owner).second)
        {
            continue;
        }
        SmCurve* raw = nullptr;
        checkedCall();
        const auto copiedUv = old->Copy(*face.GetContext(), raw);
        if (raw == old)
        {
            return result;
        }
        std::unique_ptr<SmCurve> owned(raw);
        if (owned)
        {
            owned->SetOwner(nullptr);
        }
        const auto spline = dynamic_cast<SmBSplineCurve*>(owned.get());
        if (copiedUv != SM_SUCCESS || !spline)
        {
            return result;
        }
        owned.release();
        saved.push_back({ owner, std::unique_ptr<SmBSplineCurve>(spline) });
    }
    // From here on, an exception aborts the entire unpublished model. Never
    // return an unestablished result after committing a partial native change.
    checkedCall();
    face.SetSurface(candidate.get(), TRUE);
    const bool adopted = candidate->GetOwner() == &face;
    if (adopted)
    {
        candidate.release();
    }
    if (!adopted || face.GetSurface() != restricted)
    {
        throw std::runtime_error("SupportRestrictionSurfaceOwnership");
    }
    for (auto& trim : saved)
    {
        checkedCall();
        const auto raw = trim.curve.get();
        trim.owner->SetUVTrimCurve(raw, 0., TRUE); // No native tolerance growth.
        const bool uvOwned = raw->GetOwner() == trim.owner || raw->GetOwner() == trim.owner->GetMate();
        if (uvOwned)
        {
            trim.curve.release();
        }
        if (!uvOwned || trim.owner->GetUVTrimCurve() != raw)
        {
            throw std::runtime_error("SupportRestrictionUvOwnership");
        }
    }
    result.status = SupportDomainStatus::Normalized;
    result.reason.clear();
    return result;
}
} // namespace step_import::detail
