// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportDerivedUv.h"

#include <SmBSplineSurface.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <SmSurface.h>
#include <array>
#include <cmath>
#include <cstring>
#include <gsl/assert>
#include <stdexcept>

namespace step_import::detail
{
namespace
{
class SurfaceOwnerScope
{
public:

    explicit SurfaceOwnerScope(SmSurface& surface) : m_surface(surface), m_owner(surface.GetOwner())
    {
        // Same unpublished-surface detachment as SmFace::CreateUVTrimCurves:
        // projection must not depend on the existing face's trim caches.
        m_surface.SetOwner(nullptr);
    }
    ~SurfaceOwnerScope()
    {
        m_surface.SetOwner(m_owner);
    }
    SurfaceOwnerScope(const SurfaceOwnerScope&) = delete;
    SurfaceOwnerScope& operator=(const SurfaceOwnerScope&) = delete;

private:

    SmSurface& m_surface;
    SmObject* m_owner;
};
bool FiniteUv(const SmPoint3d& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y);
}
bool SameBasis(const SmBSplineSurface& a, const SmBSplineSurface& b)
{
    if (a.IsRational() != b.IsRational() || a.GetDegree(SM_SP_U) != b.GetDegree(SM_SP_U) || a.GetDegree(SM_SP_V) != b.GetDegree(SM_SP_V))
    {
        return false;
    }
    ULONG au = 0, av = 0, bu = 0, bv = 0;
    double *ap = nullptr, *bp = nullptr;
    if (a.GetControlPointsPointer(au, av, ap) != SM_SUCCESS || b.GetControlPointsPointer(bu, bv, bp) != SM_SUCCESS || au != bu || av != bv || !ap ||
        !bp || !au || !av || au > 64 || av > 64 || std::memcmp(ap, bp, std::size_t(au) * av * 4 * sizeof(double)) != 0)
    {
        return false;
    }
    double *ak = nullptr, *al = nullptr, *bk = nullptr, *bl = nullptr;
    if (a.GetKnotsPointers(au, av, ak, al) != SM_SUCCESS || b.GetKnotsPointers(bu, bv, bk, bl) != SM_SUCCESS || au != bu || av != bv || !ak || !al ||
        !bk || !bl || au > 128 || av > 128 || std::memcmp(ak, bk, std::size_t(au) * sizeof(double)) != 0 ||
        std::memcmp(al, bl, std::size_t(av) * sizeof(double)) != 0)
    {
        return false;
    }
    const auto x = a.GetNaturalUVDomain(), y = b.GetNaturalUVDomain();
    return x.GetMin().x == y.GetMin().x && x.GetMax().x == y.GetMax().x && x.GetMin().y == y.GetMin().y && x.GetMax().y == y.GetMax().y;
}
} // namespace

DerivedUvCandidate MatchProjectedUvEndpoints(
    std::unique_ptr<SmBSplineCurve> projected,
    const SmCurve& edge,
    const SmCurve& installed,
    const SmSurface& surface,
    const SmExtent1d& interval,
    double maximumMm,
    const std::function<void()>& beforeNativeCall
)
{
    Expects(bool(beforeNativeCall));
    Expects(std::isfinite(maximumMm) && maximumMm > 0);
    Expects(!projected || projected->GetOwner() == nullptr);
    DerivedUvCandidate result;
    if (!projected || projected->GetDim() != 2 || installed.GetDim() != 2 || edge.GetDim() != 3)
    {
        return result;
    }
    try
    {
        CheckFinalTrimParameters(edge, *projected, interval.GetMin(), interval.GetMax());
        CheckFinalTrimParameters(edge, installed, interval.GetMin(), interval.GetMax());
    }
    catch (const std::runtime_error&)
    {
        return result;
    }
    beforeNativeCall();
    try
    {
        result.rawPair = MeasureFinalTrim(edge, *projected, surface, interval.GetMin(), interval.GetMax());
    }
    catch (const std::runtime_error&)
    {
        return result;
    }
    if (result.rawPair->maximumMm > maximumMm)
    {
        result.status = DerivedUvStatus::RawGapExceeded;
        return result;
    }
    // SmFace::sm_FixUVTrimCurves trims/restores the old parameter interval
    // before closing endpoints, which can warp paired t. Keep its closed-loop
    // endpoint positions, but retain the fresh UV's knots and parameterization.
    // Only derived UV is edited; defining 3D geometry and source data are borrowed.
    const auto fixedDomain = installed.GetNaturalInterval();
    std::array<SmPoint3d, 2> targets;
    for (std::size_t side = 0; side < targets.size(); ++side)
    {
        beforeNativeCall();
        if (installed.EvaluatePoint(side == 0 ? fixedDomain.GetMin() : fixedDomain.GetMax(), targets[side]) != SM_SUCCESS || !FiniteUv(targets[side]))
        {
            result.status = DerivedUvStatus::EndpointEvaluationFailed;
            return result;
        }
    }
    SmPoint3d first, last;
    std::array<double, 2> weights{ 1, 1 };
    projected->GetEnds(first, last, &weights[0], &weights[1]);
    for (std::size_t side = 0; side < targets.size(); ++side)
    {
        SmPoint3d point(targets[side].x, targets[side].y, 0);
        const double* weight = nullptr;
        if (projected->IsRational())
        {
            // Endpoint weights belong to this fresh projection, not the
            // old trimmed one. EditEndPoint takes homogeneous XY when weighted.
            if (!std::isfinite(weights[side]) || !(weights[side] > 0))
            {
                result.status = DerivedUvStatus::EndpointEditFailed;
                return result;
            }
            point = point * weights[side];
            if (!FiniteUv(point))
            {
                result.status = DerivedUvStatus::EndpointEditFailed;
                return result;
            }
            weight = &weights[side];
        }
        beforeNativeCall();
        if (projected->EditEndPoint(point, side == 0, weight, SM_EE_PROJECT_UVTRIMCURVE_VERTEX_TO_SURFACE, FALSE) != SM_SUCCESS)
        {
            result.status = DerivedUvStatus::EndpointEditFailed;
            return result;
        }
    }
    beforeNativeCall();
    try
    {
        result.matchedPair = MeasureFinalTrim(edge, *projected, surface, interval.GetMin(), interval.GetMax());
    }
    catch (const std::runtime_error&)
    {
        return result;
    }
    if (result.matchedPair->maximumMm > maximumMm)
    {
        result.status = DerivedUvStatus::MatchedGapExceeded;
        return result;
    }
    // Paired knot-span sampling is not a continuous error certificate.
    // Installation, seam/loop validity and final publication remain the caller's.
    result.status = DerivedUvStatus::Ready;
    result.curve = std::move(projected);
    return result;
}
DerivedUvCandidate RebuildDerivedUv(const SmEdgeuse& use, double maximumMm, const std::function<void()>& beforeNativeCall)
{
    Expects(bool(beforeNativeCall));
    Expects(std::isfinite(maximumMm) && maximumMm > 0);
    DerivedUvCandidate result;
    const auto edge = use.GetEdge();
    const auto face = use.GetFace();
    const auto surface = face ? face->GetSurface() : nullptr;
    const auto installed = use.GetUVTrimCurve();
    if (!edge || !edge->GetCurve() || !surface || !installed)
    {
        return result;
    }
    SmBSplineCurve* raw = nullptr;
    double gap = -1;
    SmStatus status;
    {
        SurfaceOwnerScope owner(*surface);
        beforeNativeCall();
        status = use.CreateUVTrimCurve(gap, raw);
    }
    // This API returns an unattached allocation with an association owner.
    // Reject unexpected borrowed aliases before adopting; clear the association
    // so endpoint edits cannot notify/mutate installed topology or its caches.
    if (raw == installed || raw == edge->GetCurve())
    {
        return result;
    }
    std::unique_ptr<SmBSplineCurve> projected(raw);
    if (projected)
    {
        projected->SetOwner(nullptr);
    }
    if (status != SM_SUCCESS || !projected)
    {
        result.status = DerivedUvStatus::ProjectionFailed;
        return result;
    }
    if (std::isfinite(gap) && gap >= 0)
    {
        result.projectionGapMm = gap;
    }
    if (!result.projectionGapMm || gap > maximumMm)
    {
        result.status = DerivedUvStatus::ProjectionGapExceeded;
        return result;
    }
    auto matched =
        MatchProjectedUvEndpoints(std::move(projected), *edge->GetCurve(), *installed, *surface, edge->GetInterval(), maximumMm, beforeNativeCall);
    matched.projectionGapMm = gap;
    return matched;
}
DerivedUvCandidate ProjectDerivedUv(
    const SmEdgeuse& use,
    double maximumMm,
    const std::function<void()>& beforeNativeCall,
    bool projectOnOriginalNurbsBasis
)
{
    Expects(bool(beforeNativeCall));
    Expects(std::isfinite(maximumMm) && maximumMm > 0);
    DerivedUvCandidate result;
    const auto edge = use.GetEdge();
    const auto face = use.GetFace();
    const auto surface = face ? face->GetSurface() : nullptr;
    if (!edge || !edge->GetCurve() || !surface || use.GetUVTrimCurve())
    {
        return result;
    }
    std::unique_ptr<SmBSplineSurface> ordinary;
    SmSurface* projectionSurface = surface;
    if (projectOnOriginalNurbsBasis)
    {
        const auto spline = dynamic_cast<const SmBSplineSurface*>(surface);
        if (!spline)
        {
            return result;
        }
        if (spline->GetType() != SmBSplineSurface_TYPE)
        {
            beforeNativeCall();
            ordinary.reset(new (*face->GetContext()) SmBSplineSurface(*spline));
            ordinary->SetOwner(nullptr);
            ordinary->SetBSplineSurfaceForm(SM_SF_UNSPECIFIED);
            // The bounded analytic torus net is small. Reject any copy-time
            // knot correction instead of claiming an unchanged UV map. Measure
            // every resulting trim against the actual original surface below.
            if (!SameBasis(*spline, *ordinary))
            {
                return result;
            }
            projectionSurface = ordinary.get();
        }
    }
    SmTArray<SmBSplineCurve*> curves;
    SmObjsDelete<SmBSplineCurve*> cleanup(&curves);
    double gap = -1, approximation = -1;
    SmStatus status;
    {
        SurfaceOwnerScope owner(*projectionSurface);
        beforeNativeCall();
        // Separate numerical approximation from normal source/surface distance.
        // KeepAll permits the latter to exceed the tighter solve target, but
        // both returned projection distance and paired lift must still satisfy
        // maximumMm below. No topology tolerance is adjusted to obtain a curve.
        status = projectionSurface->DropCurve(
            *face->GetContext(),
            face->GetUVDomain(),
            *edge->GetCurve(),
            edge->GetInterval(),
            SmApproxTol3d(maximumMm / 8),
            gap,
            approximation,
            curves,
            TRUE
        );
    }
    if (status != SM_SUCCESS || curves.GetSize() != 1 || !curves[0] || !std::isfinite(gap) || gap < 0 || gap > maximumMm)
    {
        return result;
    }
    result.projectionGapMm = gap;
    beforeNativeCall();
    try
    {
        const auto interval = edge->GetInterval();
        result.rawPair = MeasureFinalTrim(*edge->GetCurve(), *curves[0], *surface, interval.GetMin(), interval.GetMax());
    }
    catch (const std::runtime_error&)
    {
        return result;
    }
    if (result.rawPair->maximumMm > maximumMm)
    {
        return result;
    }
    result.curve.reset(curves[0]);
    cleanup.Clear();
    result.status = DerivedUvStatus::Ready;
    return result;
}
} // namespace step_import::detail
