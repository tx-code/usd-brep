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
} // namespace
[[nodiscard]] Result<SupportDomainEvidence> RestrictUnusedSupportDomain(
    SmFace& face,
    bool uniqueExplicitOuter,
    double maximumMm,
    std::size_t maximumControls,
    std::size_t maximumProducts,
    const ExecutionCheckpoint& beforeNativeCall
)
{
    Expects(bool(beforeNativeCall));
    Expects(std::isfinite(maximumMm) && maximumMm > 0);
    SupportDomainEvidence result;
    if (!SurfaceProofNumericEnvironment())
    {
        result.status = SupportDomainStatus::Unestablished;
        return result;
    }
    const ExecutionCheckpoint checkedCall = [&]() -> Result<void>
    {
        if (auto operation = beforeNativeCall(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (!SurfaceProofNumericEnvironment())
        {
            throw std::runtime_error("SupportDomainNumericEnvironmentChanged");
        }
        return {};
    };
    auto surface = dynamic_cast<SmBSplineSurface*>(face.GetSurface());
    // Analytic types have separate defining data. Trimming only their NURBS
    // cache would not establish an equivalent analytic representation.
    if (!uniqueExplicitOuter || !surface || surface->GetType() != SmBSplineSurface_TYPE)
    {
        return result;
    }
    const auto natural = surface->GetNaturalUVDomain();
    result.original = Domain(natural);
    if (!std::all_of(result.original.begin(), result.original.end(), Finite))
    {
        return result;
    }
    if (auto operation = checkedCall(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    if (surface->IsClosed(natural, SM_SP_U) || surface->IsClosed(natural, SM_SP_V) || surface->IsPeriodic(natural, SM_SP_U) ||
        surface->IsPeriodic(natural, SM_SP_V))
    {
        return result;
    }
    SmTArray<SmEdgeuse*> uses;
    face.GetUpwardEdgeuses(uses);
    if (uses.GetSize() == 0)
    {
        return result;
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
    SmSurface* copy = nullptr;
    if (auto operation = checkedCall(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
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
    SmExtent2d requested(bounds[0], bounds[1], bounds[2], bounds[3]);
    if (auto operation = checkedCall(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
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
    auto nativeValue25 = ProveSurfaceRestriction(*surface, *restricted, maximumMm, maximumControls, maximumProducts, checkedCall);
    if (!nativeValue25)
    {
        return tl::unexpected(std::move(nativeValue25.error()));
    }
    result.numeric = std::move(*nativeValue25);
    if (!result.numeric.established)
    {
        if (result.numeric.workLimit)
        {
            result.status = SupportDomainStatus::WorkLimit;
        }
        return result;
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
        if (auto operation = checkedCall(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
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
    if (auto operation = checkedCall(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
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
        if (auto operation = checkedCall(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
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
    return result;
}
} // namespace step_import::detail
