// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportRevolutionChart.h"

#include "StepImportProofEnvironment.h"

#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmBrep.h>
#include <SmCacheMgr.h>
#include <SmCrvOnSurf.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <SmLoopuse.h>
#include <SmSTEPSurface.h>
#include <SmSurfOfRevolution.h>
#include <SmVertexuse.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace step_import::detail
{
namespace
{
bool Same(double a, double b)
{
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
bool Same(const std::vector<double>& a, const std::vector<double>& b)
{
    return a.size() == b.size() && std::equal(
                                       a.begin(),
                                       a.end(),
                                       b.begin(),
                                       [](double x, double y)
                                       {
                                           return Same(x, y);
                                       }
                                   );
}
struct Net
{
    std::vector<double> controls, u, v;
    ULONG degreeU = 0, degreeV = 0;
    ULONG dimension = 0;
    SmBoolean rational = FALSE;
    bool operator==(const Net& b) const
    {
        return Same(controls, b.controls) && Same(u, b.u) && Same(v, b.v) && degreeU == b.degreeU && degreeV == b.degreeV &&
               dimension == b.dimension && rational == b.rational;
    }
};
Net Read(const SmBSplineSurface& s)
{
    ULONG nu = 0, nv = 0, ku = 0, kv = 0;
    double *p = nullptr, *u = nullptr, *v = nullptr;
    if (s.GetControlPointsPointer(nu, nv, p) != SM_SUCCESS || s.GetKnotsPointers(ku, kv, u, v) != SM_SUCCESS || !p || !u || !v)
    {
        return {};
    }
    return { { p, p + std::size_t(nu) * nv * 4 }, { u, u + ku }, { v, v + kv }, s.GetDegree(SM_SP_U), s.GetDegree(SM_SP_V), 3, s.IsRational() };
}
Net Read(const SmBSplineCurve& c)
{
    ULONG n = 0;
    double* p = nullptr;
    SmTArray<double> k;
    if (c.GetControlPointsPointer(n, p) != SM_SUCCESS || c.GetKnotsAll(k) != SM_SUCCESS || !p)
    {
        return {};
    }
    return { { p, p + std::size_t(n) * 4 }, { k.GetDataArray(), k.GetDataArray() + k.GetSize() }, {}, c.GetDegree(), 0, c.GetDim(), c.IsRational() };
}
bool Scaled(double a, double b, int exponent)
{
    // Pure power-of-two, no translation. Inverse equality excludes lost bits
    // through subnormal rounding; finite positive scale preserves orientation.
    return std::isfinite(a) && std::isfinite(b) && Same(b, std::ldexp(a, exponent)) && Same(a, std::ldexp(b, -exponent));
}
bool SurfaceIdentity(const Net& a, const Net& b, int exponent, std::size_t& values)
{
    if (a.controls.empty() || !Same(a.controls, b.controls) || !Same(a.v, b.v) || a.u.size() != b.u.size() || a.degreeU != b.degreeU ||
        a.degreeV != b.degreeV || a.rational != b.rational || a.dimension != b.dimension)
    {
        return false;
    }
    for (std::size_t i = 0; i < a.u.size(); ++i)
    {
        if (!Scaled(a.u[i], b.u[i], exponent))
        {
            return false;
        }
    }
    values += a.controls.size() + a.v.size() + a.u.size();
    return true;
}
bool CurveIdentity(const Net& a, const Net& b, int exponent, std::size_t& values)
{
    if (a.controls.empty() || a.controls.size() != b.controls.size() || !Same(a.u, b.u) || a.degreeU != b.degreeU || a.dimension != b.dimension ||
        a.rational != b.rational)
    {
        return false;
    }
    for (std::size_t i = 0; i < a.controls.size(); ++i)
    {
        if (i % 4 == 0)
        {
            if (!Scaled(a.controls[i], b.controls[i], exponent))
            {
                return false;
            }
        }
        else if (!Same(a.controls[i], b.controls[i]))
        {
            return false;
        }
    }
    values += a.controls.size() + a.u.size();
    return true;
}
std::array<double, 4> Domain(const SmExtent2d& d)
{
    return { d.GetMin().x, d.GetMin().y, d.GetMax().x, d.GetMax().y };
}
bool Metric(const SmSurface& s, double& ratio, double& condition)
{
    SmVector3d d[4];
    if (s.Evaluate(s.GetNaturalUVDomain().Evaluate(.5, .5), 1, 1, TRUE, TRUE, TRUE, d, FALSE) != SM_SUCCESS)
    {
        return false;
    }
    const double a = d[2].Dot(d[2]), b = d[2].Dot(d[1]), c = d[1].Dot(d[1]);
    const double area2 = (d[2] * d[1]).LengthSquared();
    const double largest = (a + c + std::hypot(a - c, 2 * b)) / 2;
    if (!(a > 0 && c > 0 && area2 > 0 && largest > 0))
    {
        return false;
    }
    ratio = std::sqrt(a / c);
    condition = largest / std::sqrt(area2);
    return std::isfinite(ratio) && std::isfinite(condition);
}
auto Fail(Status status, const char* key)
{
    return tl::unexpected(Failure{ status, key, "Private revolution chart normalization failed", {}, "TargetChartNormalization" });
}
// A valid owner backlink alone cannot exclude borrowed aliases inside a COS.
// Audit all defining BRep curves before deleting any derived trim or changing
// the target surface. Unknown dependency forms refuse this normalization.
// See smlib-brep-model/references/brep-format.md, UV trim curves: these data
// are derived, whereas an SmCrvOnSurf's internal UV and support DEFINE C(t).
bool IndependentCurve(const SmCurve*, const SmSurface*, const std::set<const SmBSplineCurve*>&, unsigned);
bool IndependentSurface(const SmSurface* s, const SmSurface* target, const std::set<const SmBSplineCurve*>& trims, unsigned depth)
{
    if (!s || s == target || depth > 16)
    {
        return false;
    }
    if (s->GetType() == SmSTEPSurface_TYPE)
    {
        return IndependentSurface(static_cast<const SmSTEPSurface*>(s)->GetBaseSurface(), target, trims, depth + 1);
    }
    if (s->GetType() == SmSurfOfRevolution_TYPE)
    {
        SmBSplineCurve* generator = nullptr;
        SmPoint3d p;
        SmVector3d a;
        return static_cast<const SmSurfOfRevolution*>(s)->GetCanonical(generator, p, a) == SM_SUCCESS &&
               IndependentCurve(generator, target, trims, depth + 1);
    }
    const auto t = s->GetType();
    return t == SmBSplineSurface_TYPE || t == SmPlane_TYPE || t == SmCylinder_TYPE || t == SmCone_TYPE || t == SmSphere_TYPE || t == SmTorus_TYPE;
}
bool IndependentCurve(const SmCurve* c, const SmSurface* target, const std::set<const SmBSplineCurve*>& trims, unsigned depth)
{
    if (!c || depth > 16)
    {
        return false;
    }
    if (c->GetType() == SmBSplineCurve_TYPE)
    {
        return !trims.contains(static_cast<const SmBSplineCurve*>(c));
    }
    if (c->GetType() == SmCrvOnSurf_TYPE)
    {
        const auto* composite = static_cast<const SmCrvOnSurf*>(c);
        return IndependentCurve(composite->GetUVCurve(), target, trims, depth + 1) &&
               IndependentSurface(composite->GetBaseSurface(), target, trims, depth + 1);
    }
    const auto t = c->GetType();
    return t == SmLine_TYPE || t == SmCircle_TYPE || t == SmEllipse_TYPE;
}
} // namespace

bool ExactRevolutionChartScale(double a, double b, int exponent)
{
    return exponent >= -32 && exponent <= 32 && SurfaceProofNumericEnvironment() && std::isfinite(a) && std::isfinite(b) && Scaled(a, b, exponent);
}

std::optional<std::vector<SmFace*>> RevolutionChartFaces(const SmAssertArray& findings)
{
    if (!findings.GetSize())
    {
        return {};
    }
    std::vector<SmFace*> faces;
    for (ULONG i = 0; i < findings.GetSize(); ++i)
    {
        const auto* finding = findings[i];
        // SmBSplineCurve::AssertValid test 13 is its UV-coordinate small-radius
        // predicate. Do not reinterpret any defining-curve or mixed finding.
        if (!finding || finding->m_bOK || finding->m_lReportingType != SmBSplineCurve_TYPE || finding->GetListIndex() != 0 ||
            finding->GetOwnerType() != SmBSplineCurve_TYPE || finding->GetTestIndex() != 13 || !finding->GetOwner() ||
            finding->m_pReporter != finding->GetOwner())
        {
            return {};
        }
        const auto* curve = static_cast<const SmBSplineCurve*>(finding->GetOwner());
        auto* owner = curve->GetOwner();
        if (curve->GetType() != SmBSplineCurve_TYPE || curve->GetDim() != 2 || !owner || owner->GetType() != SmEdgeuse_TYPE)
        {
            return {};
        }
        auto* use = static_cast<SmEdgeuse*>(owner);
        SmEdgeuse* storedOwner = nullptr;
        auto* face = use->GetFace();
        if (!face || use->GetUVTrimCurve(&storedOwner) != curve || storedOwner != use || !face->GetSurface() ||
            face->GetSurface()->GetType() != SmSurfOfRevolution_TYPE)
        {
            return {};
        }
        if (std::find(faces.begin(), faces.end(), face) == faces.end())
        {
            faces.push_back(face);
        }
    }
    return faces;
}

Result<std::optional<RevolutionChartChange>> ConditionRevolutionChart(SmFace& face, std::size_t maximumControls, const ExecutionCheckpoint& check)
{
    const auto absent = []
    {
        return std::optional<RevolutionChartChange>{};
    };
    if (!check || !maximumControls || !SurfaceProofNumericEnvironment())
    {
        return absent();
    }
    auto* surface = dynamic_cast<SmSurfOfRevolution*>(face.GetSurface());
    if (!surface || surface->GetType() != SmSurfOfRevolution_TYPE || surface->GetSwapUV() || !surface->HavePolarConversion())
    {
        return absent();
    }
    if (auto r = check(); !r)
    {
        return tl::unexpected(std::move(r.error()));
    }
    RevolutionChartChange result;
    double ratio = 0;
    if (!Metric(*surface, ratio, result.conditionBefore) || (ratio >= 1. / 16 && ratio <= 16))
    {
        return absent();
    }
    int e = 0;
    const double m = std::frexp(ratio, &e);
    result.exponent = m < std::sqrt(.5) ? e - 1 : e;
    if (result.exponent < -32 || result.exponent > 32 || result.exponent == 0)
    {
        return absent();
    }
    const auto oldDomain = surface->GetNaturalUVDomain();
    const auto newDomain = SmExtent2d(
        std::ldexp(oldDomain.GetMin().x, result.exponent),
        oldDomain.GetMin().y,
        std::ldexp(oldDomain.GetMax().x, result.exponent),
        oldDomain.GetMax().y
    );
    if (!Scaled(oldDomain.GetMin().x, newDomain.GetMin().x, result.exponent) || !Scaled(oldDomain.GetMax().x, newDomain.GetMax().x, result.exponent))
    {
        return absent();
    }
    result.before = Domain(oldDomain);
    result.after = Domain(newDomain);
    result.step = Domain(surface->GetSTEPUVDomain());
    ULONG nu = 0, nv = 0;
    double* coefficients = nullptr;
    if (surface->GetControlPointsPointer(nu, nv, coefficients) != SM_SUCCESS || !nu || !nv || !coefficients)
    {
        return absent();
    }
    if (std::size_t(nu) > maximumControls / nv)
    {
        return Fail(Status::ResourceLimit, "RevolutionChartControlLimit");
    }
    const auto before = Read(*surface);
    SmBSplineCurve* generator = nullptr;
    SmPoint3d origin;
    SmVector3d axis;
    if (surface->GetCanonical(generator, origin, axis) != SM_SUCCESS || !generator)
    {
        return absent();
    }
    ULONG ng = 0;
    if (generator->GetControlPointsPointer(ng, coefficients) != SM_SUCCESS || !coefficients || !ng)
    {
        return absent();
    }
    std::size_t controls = std::size_t(nu) * nv;
    if (ng > maximumControls - controls)
    {
        return Fail(Status::ResourceLimit, "RevolutionChartControlLimit");
    }
    controls += ng;
    const auto gen = Read(*generator);
    const auto genStep = generator->GetSTEPInterval();
    SmSurface* raw = nullptr;
    if (auto r = check(); !r)
    {
        return tl::unexpected(std::move(r.error()));
    }
    const auto copied = surface->Copy(*face.GetContext(), raw);
    std::unique_ptr<SmSurface> original(raw);
    if (copied != SM_SUCCESS || !original)
    {
        return Fail(Status::BuildFailure, "RevolutionChartCopyFailure");
    }
    raw = nullptr;
    if (auto r = check(); !r)
    {
        return tl::unexpected(std::move(r.error()));
    }
    const auto copied2 = surface->Copy(*face.GetContext(), raw);
    std::unique_ptr<SmSurface> trial(raw);
    if (copied2 != SM_SUCCESS || !trial)
    {
        return Fail(Status::BuildFailure, "RevolutionChartCopyFailure");
    }
    auto* candidate = dynamic_cast<SmSurfOfRevolution*>(trial.get());
    if (!candidate || candidate->GetSwapUV())
    {
        return absent();
    }
    if (auto r = check(); !r)
    {
        return tl::unexpected(std::move(r.error()));
    }
    if (candidate->Reparameterize(newDomain) != SM_SUCCESS || !candidate->HavePolarConversion())
    {
        return absent();
    }
    SmBSplineCurve* newGen = nullptr;
    SmPoint3d newOrigin;
    SmVector3d newAxis;
    if (candidate->GetCanonical(newGen, newOrigin, newAxis) != SM_SUCCESS || !newGen || Read(*newGen) != gen ||
        !(newGen->GetSTEPInterval() == genStep) || newOrigin != origin || newAxis != axis || Domain(candidate->GetSTEPUVDomain()) != result.step ||
        !SurfaceIdentity(before, Read(*candidate), result.exponent, result.exactValues))
    {
        return absent();
    }
    double newRatio = 0;
    if (auto r = check(); !r)
    {
        return tl::unexpected(std::move(r.error()));
    }
    if (!Metric(*candidate, newRatio, result.conditionAfter) || result.conditionAfter * 2 > result.conditionBefore)
    {
        return absent();
    }
    struct Trim
    {
        SmEdgeuse* owner;
        std::unique_ptr<SmBSplineCurve> curve;
        Net expected;
    };
    std::vector<Trim> trims;
    std::map<const SmBSplineCurve*, SmEdgeuse*> seen;
    SmTArray<SmLoopuse*> loops;
    face.GetUpwardLoopuses(loops);
    for (ULONG li = 0; li < loops.GetSize(); ++li)
    {
        if (!loops[li]->IsEdgeLoopuse())
        {
            return absent();
        }
        SmTArray<SmEdgeuse*> uses;
        loops[li]->GetEdgeuses(uses);
        for (ULONG ui = 0; ui < uses.GetSize(); ++ui)
        {
            SmEdgeuse* owner = nullptr;
            const auto* uv = uses[ui]->GetUVTrimCurve(&owner);
            if (!uv || !owner || owner->GetFace() != &face || uv->GetOwner() != owner)
            {
                return absent();
            }
            if (seen.contains(uv))
            {
                continue;
            }
            seen.emplace(uv, owner);
            ULONG nc = 0;
            if (uv->GetType() != SmBSplineCurve_TYPE || uv->GetDim() != 2 || uv->GetControlPointsPointer(nc, coefficients) != SM_SUCCESS ||
                !coefficients || !nc)
            {
                return absent();
            }
            if (nc > maximumControls - controls)
            {
                return Fail(Status::ResourceLimit, "RevolutionChartControlLimit");
            }
            controls += nc;
            const auto net = Read(*uv);
            const auto natural = uv->GetNaturalInterval(), step = uv->GetSTEPInterval();
            SmCurve* curveRaw = nullptr;
            if (auto r = check(); !r)
            {
                return tl::unexpected(std::move(r.error()));
            }
            const auto status = uv->Copy(*face.GetContext(), curveRaw);
            std::unique_ptr<SmCurve> copy(curveRaw);
            if (status != SM_SUCCESS || !copy)
            {
                return Fail(Status::BuildFailure, "RevolutionChartTrimCopyFailure");
            }
            auto* spline = dynamic_cast<SmBSplineCurve*>(copy.get());
            if (!spline || spline->GetDim() != 2)
            {
                return absent();
            }
            spline->SetOwner(nullptr);
            if (auto r = check(); !r)
            {
                return tl::unexpected(std::move(r.error()));
            }
            ULONG count = 0;
            double* data = nullptr;
            if (spline->GetControlPointsPointer(count, data) != SM_SUCCESS || !data || data == coefficients || count != nc)
            {
                return absent();
            }
            for (ULONG j = 0; j < count; ++j)
            {
                if (!Scaled(data[std::size_t(j) * 4], std::ldexp(data[std::size_t(j) * 4], result.exponent), result.exponent))
                {
                    return absent();
                }
            }
            // SmBSplineCurve::Transform goes through N_CrvTransform, which
            // promotes a 2D NLIB net to 3D. Instead edit ONLY raw homogeneous
            // X on this independent, detached copy, with standard edit notices.
            // Preserve NL_NOW polynomial markers, weights, Y/Z and curve t.
            spline->Notify(SM_NO_PRE_EDIT, spline, nullptr, nullptr);
            for (ULONG j = 0; j < count; ++j)
            {
                data[std::size_t(j) * 4] = std::ldexp(data[std::size_t(j) * 4], result.exponent);
            }
            spline->Notify(SM_NO_POST_EDIT, spline, nullptr, nullptr);
            if (!CurveIdentity(net, Read(*spline), result.exponent, result.exactValues) || !(spline->GetNaturalInterval() == natural) ||
                !(spline->GetSTEPInterval() == step))
            {
                return absent();
            }
            auto expected = Read(*spline);
            copy.release();
            trims.push_back({ owner, std::unique_ptr<SmBSplineCurve>(spline), std::move(expected) });
        }
    }
    if (trims.empty())
    {
        return absent();
    }
    std::set<const SmBSplineCurve*> derived;
    for (const auto& [curve, owner] : seen)
    {
        (void)owner;
        derived.insert(curve);
    }
    if (!face.GetBrep())
    {
        return absent();
    }
    SmTArray<SmEdge*> definingEdges;
    face.GetBrep()->GetEdges(definingEdges);
    for (ULONG i = 0; i < definingEdges.GetSize(); ++i)
    {
        if (!IndependentCurve(definingEdges[i]->GetCurve(), surface, derived, 0))
        {
            return absent();
        }
    }
    // All fallible preparation/proof work completed on independent candidates.
    // Failure after this point MUST discard the whole unpublished BRep; this
    // helper does not roll back a partly applied chart or permit fallback.
    const auto oldFaceDomain = face.GetUVDomain();
    const SmExtent2d newFaceDomain(
        std::ldexp(oldFaceDomain.GetMin().x, result.exponent),
        oldFaceDomain.GetMin().y,
        std::ldexp(oldFaceDomain.GetMax().x, result.exponent),
        oldFaceDomain.GetMax().y
    );
    if (!Scaled(oldFaceDomain.GetMin().x, newFaceDomain.GetMin().x, result.exponent) ||
        !Scaled(oldFaceDomain.GetMax().x, newFaceDomain.GetMax().x, result.exponent))
    {
        return absent();
    }
    if (auto r = check(); !r)
    {
        return tl::unexpected(std::move(r.error()));
    }
    // SmSurface::Notify(PRE_EDIT) clears only its trimmed-surface cache; the
    // ordinary surface tree can retain OLD UV intervals after reparameterizing.
    // This is a serialized private graph, with no outstanding checked-out query
    // caches at a request boundary. Clear both explicitly before the edit.
    SmCacheMgr::DeleteObjectsCache(SM_OC_SURFACE, surface);
    SmCacheMgr::DeleteObjectsCache(SM_OC_TRIMSRF, surface);
    if (surface->Reparameterize(newDomain) != SM_SUCCESS || !surface->HavePolarConversion() ||
        !SurfaceIdentity(before, Read(*surface), result.exponent, result.exactValues))
    {
        return Fail(Status::BuildFailure, "RevolutionChartApplicationFailure");
    }
    if (surface->GetCanonical(newGen, newOrigin, newAxis) != SM_SUCCESS || !newGen || Read(*newGen) != gen ||
        !(newGen->GetSTEPInterval() == genStep) || newOrigin != origin || newAxis != axis || Domain(surface->GetSTEPUVDomain()) != result.step)
    {
        return Fail(Status::BuildFailure, "RevolutionChartDefinitionChanged");
    }
    face.SetUVDomain(newFaceDomain);
    for (auto& trim : trims)
    {
        if (auto r = check(); !r)
        {
            return tl::unexpected(std::move(r.error()));
        }
        auto* adopted = trim.curve.get();
        trim.owner->SetUVTrimCurve(adopted, 0., TRUE);
        if (adopted->GetOwner() == trim.owner || adopted->GetOwner() == trim.owner->GetMate())
        {
            trim.curve.release();
        }
        if (trim.curve || trim.owner->GetUVTrimCurve() != adopted || Read(*adopted) != trim.expected)
        {
            return Fail(Status::BuildFailure, "RevolutionChartTrimOwnershipFailure");
        }
        // SetUVTrimCurve resets vertex/face gaps but not cached edge/face
        // closest UV coordinates. Reset both sides of the owner pair as well.
        for (auto* use : { trim.owner, trim.owner->GetMate() })
        {
            if (!use)
            {
                continue;
            }
            use->InitMaxEdgeFaceGap();
            use->GetVertexuse()->InitVertexFaceGap();
            use->GetVertexuse()->SetUVPoint(SmPoint2d(SM_BIG_DOUBLE, SM_BIG_DOUBLE));
        }
    }
    std::vector<std::pair<SmEdgeuse*, Net>> expectedTrims;
    for (auto& trim : trims)
    {
        expectedTrims.emplace_back(trim.owner, std::move(trim.expected));
    }
    result.verifyStoredIdentity = [surface,
                                   owner = &face,
                                   expectedSurface = Read(*surface),
                                   expectedDomain = Domain(newFaceDomain),
                                   expectedTrims = std::move(expectedTrims),
                                   gen,
                                   genStep,
                                   origin,
                                   axis,
                                   stepDomain = result.step]()
    {
        if (owner->GetSurface() != surface || Domain(owner->GetUVDomain()) != expectedDomain || Read(*surface) != expectedSurface ||
            Domain(surface->GetSTEPUVDomain()) != stepDomain)
        {
            return false;
        }
        SmBSplineCurve* generator = nullptr;
        SmPoint3d p;
        SmVector3d a;
        if (surface->GetCanonical(generator, p, a) != SM_SUCCESS || !generator || Read(*generator) != gen ||
            !(generator->GetSTEPInterval() == genStep) || p != origin || a != axis)
        {
            return false;
        }
        for (const auto& [use, expected] : expectedTrims)
        {
            SmEdgeuse* storedOwner = nullptr;
            const auto* trim = use->GetUVTrimCurve(&storedOwner);
            if (!trim || storedOwner != use || trim->GetOwner() != use || Read(*trim) != expected)
            {
                return false;
            }
        }
        return true;
    };
    result.trims = trims.size();
    result.original = std::move(original);
    return std::optional<RevolutionChartChange>(std::move(result));
}
} // namespace step_import::detail
