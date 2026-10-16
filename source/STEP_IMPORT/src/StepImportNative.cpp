// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportInterval.h"

#include <SmApiQueries.h>
#include <SmAssertArray.h>
#include <SmBrep.h>
#include <SmBrepData.h>
#include <SmContext.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <SmFaceuse.h>
#include <SmLine.h>
#include <SmLoopuse.h>
#include <SmPlane.h>
#include <SmPointClass.h>
#include <SmTol.h>
#include <SmVertex.h>
#include <SmVertexuse.h>
#include <cfenv>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#    include <windows.h>
#    undef GetObject
#endif

namespace step_import::detail
{
struct ModelData
{
    // Destruction is reverse declaration order: owned BRep before its context.
    std::unique_ptr<SmContext> context;
    std::unique_ptr<SmBrep> brep;
    std::shared_ptr<const step_semantics::Snapshot> source;
    SmFace* face = nullptr;
    SmPlane* plane = nullptr;
    std::vector<SmVertex*> vertices;
    std::vector<SmEdge*> edges;
    std::vector<SmLine*> curves;
    std::string faceId;
    Statistics info;
    mutable std::mutex mutex;
};
namespace
{
Point PointOf(const SmPoint3d& point)
{
    return { point.x, point.y, point.z };
}
SmPoint3d Native(const Point& point)
{
    return { point[0], point[1], point[2] };
}
Point Delta(Point a, Point b)
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        a[i] -= b[i];
    }
    return a;
}
double DotPoint(Point a, Point b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
std::string LoadedKernel()
{
#ifdef _WIN32
    std::array<wchar_t, 32768> path{};
    const auto size = GetModuleFileNameW(GetModuleHandleW(L"SMLib.dll"), path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size())
    {
        throw std::runtime_error("NativeLibraryIdentityUnavailable");
    }
    const auto utf8 = std::filesystem::path(path.data()).generic_u8string();
    return { utf8.begin(), utf8.end() };
#else
    return "Unmeasured";
#endif
}
void Need(bool value, const char* key, std::size_t record)
{
    if (!value)
    {
        throw Failure{ Status::PostcheckFailure, key, "Native candidate does not preserve the qualified source contract", record };
    }
}
void Good(SmStatus status)
{
    if (status != SM_SUCCESS)
    {
        throw std::runtime_error("NativeReturnFailure:" + std::to_string(status));
    }
}
void Certify(double upper, Fraction budget, const char* key, std::size_t record)
{
    if (!std::isfinite(upper) || upper > Enclose(budget).low)
    {
        throw Failure{ Status::Indeterminate, key, "Conservative native error bound cannot establish the requested quality", record };
    }
}
struct Affine
{
    IntervalPoint origin, delta;
};
Affine LineCoefficients(const SmLine& line, bool polygon)
{
    if (line.GetDegree() != 1 || line.GetInsideOut() || line.GetNaturalInterval().GetMin() != 0 || line.GetNaturalInterval().GetMax() != 1 ||
        line.GetSTEPInterval().GetMin() != 0 || line.GetSTEPInterval().GetMax() != 1)
    {
        throw std::domain_error("UnestablishedNativeLineParameterization");
    }
    if (polygon)
    {
        SmTArray<double> knots;
        Good(line.GetKnotsAll(knots));
        if (knots.GetSize() != 4 || knots[0] != 0 || knots[1] != 0 || knots[2] != 1 || knots[3] != 1)
        {
            throw std::domain_error("UnestablishedNativeAffineKnots");
        }
        SmTArray<SmPoint3d> points;
        SmTArray<double> weights;
        Good(line.GetControlPolygon(points, weights));
        if (points.GetSize() != 2 || (weights.GetSize() != 0 && (weights.GetSize() != 2 || weights[0] != 1 || weights[1] != 1)))
        {
            throw std::domain_error("UnestablishedNativeAffineCurve");
        }
        const auto a = Exact(PointOf(points[0])), b = Exact(PointOf(points[1]));
        return { a, Subtract(b, a) };
    }
    return { Exact(PointOf(line.GetLinePoint())), Times(Exact(PointOf(line.GetLineVector())), Exact(line.GetLineScale())) };
}
struct SurfaceCoefficients
{
    IntervalPoint origin, u, v, uv;
};
SurfaceCoefficients PlaneCoefficients(const SmPlane& plane, bool polygon)
{
    if (!polygon)
    {
        const auto& position = plane.GetPosition();
        const auto scale = plane.GetUVScale();
        return { Exact(PointOf(position.GetOriginRef())),
                 Times(Exact(PointOf(position.GetXAxisRef())), Exact(scale.x)),
                 Times(Exact(PointOf(position.GetYAxisRef())), Exact(scale.y)),
                 Exact(Point{}) };
    }
    const auto& surface = static_cast<const SmBSplineSurface&>(plane);
    if (surface.IsRational() || surface.GetDegree(SM_SP_U) != 1 || surface.GetDegree(SM_SP_V) != 1 || surface.GetNumberControlPoints(SM_SP_U) != 2 ||
        surface.GetNumberControlPoints(SM_SP_V) != 2)
    {
        throw std::domain_error("UnestablishedNativeBilinearPlane");
    }
    IntervalPoint cp[2][2];
    for (ULONG u = 0; u < 2; ++u)
    {
        for (ULONG v = 0; v < 2; ++v)
        {
            SmPoint3d point;
            double weight = 0;
            Good(surface.GetControlPoint(SM_CP_NON_RATIONAL, u, v, point, weight));
            cp[u][v] = Exact(PointOf(point));
        }
    }
    const auto domain = surface.GetNaturalUVDomain();
    for (const auto parameter : { SM_SP_U, SM_SP_V })
    {
        SmTArray<double> knots;
        SmTArray<ULONG> multiplicities;
        Good(surface.GetKnots(parameter, knots, &multiplicities));
        const auto minimum = parameter == SM_SP_U ? domain.GetMin().x : domain.GetMin().y;
        const auto maximum = parameter == SM_SP_U ? domain.GetMax().x : domain.GetMax().y;
        if (knots.GetSize() != 2 || multiplicities.GetSize() != 2 || multiplicities[0] != 2 || multiplicities[1] != 2 || knots[0] != minimum ||
            knots[1] != maximum)
        {
            throw std::domain_error("UnestablishedNativeBilinearKnots");
        }
    }
    const auto u0 = Exact(domain.GetMin().x), v0 = Exact(domain.GetMin().y);
    const auto du = Exact(domain.GetMax().x) - u0, dv = Exact(domain.GetMax().y) - v0;
    const auto mixed = Times(Add(Subtract(cp[1][1], cp[1][0]), Subtract(cp[0][0], cp[0][1])), Exact(1) / (du * dv));
    const auto u = Subtract(Times(Subtract(cp[1][0], cp[0][0]), Exact(1) / du), Times(mixed, v0));
    const auto v = Subtract(Times(Subtract(cp[0][1], cp[0][0]), Exact(1) / dv), Times(mixed, u0));
    const auto origin = Subtract(Subtract(Subtract(cp[0][0], Times(u, u0)), Times(v, v0)), Times(mixed, u0 * v0));
    return { origin, u, v, mixed };
}
IntervalPoint Evaluate(const SurfaceCoefficients& surface, Interval u, Interval v)
{
    return Add(Add(Add(surface.origin, Times(surface.u, u)), Times(surface.v, v)), Times(surface.uv, u * v));
}
double LiftBound(const SurfaceCoefficients& surface, const Affine& uv, const Affine& curve)
{
    // The native bilinear surface composed with a linear trim is quadratic. Bernstein coefficients
    // enclose every t in [0,1], not just sampled parameters or endpoints.
    const auto a0 = Subtract(Evaluate(surface, uv.origin[0], uv.origin[1]), curve.origin);
    const auto a1 = Subtract(
        Add(Add(Times(surface.u, uv.delta[0]), Times(surface.v, uv.delta[1])),
            Times(surface.uv, uv.origin[0] * uv.delta[1] + uv.origin[1] * uv.delta[0])),
        curve.delta
    );
    const auto a2 = Times(surface.uv, uv.delta[0] * uv.delta[1]);
    return std::max({ NormUpper(a0), NormUpper(Add(a0, Times(a1, Exact(.5)))), NormUpper(Add(Add(a0, a1), a2)) });
}
class Builder
{
public:

    Builder(const Plan& plan, const Options& options, Report& report) : p(plan), o(options), r(report), d(std::make_shared<ModelData>())
    {
        d->source = p.source;
        d->faceId = Id(p.face);
    }
    std::shared_ptr<ModelData> Run()
    {
        if (!std::numeric_limits<double>::is_iec559 || std::fegetround() != FE_TONEAREST)
        {
            throw Failure{ Status::Indeterminate,
                           "UnsupportedNativeNumericEnvironment",
                           "Outward binary64 evidence requires nearest rounding",
                           p.face };
        }
        try
        {
            r.kernelPath = LoadedKernel();
            Step(p.face);
            d->context = std::make_unique<SmContext>();
            SmBrepConstructor ctor;
            Step(p.face);
            d->brep.reset(ctor.StartBrep(*d->context));
            Need(bool(d->brep), "NativeBrepAllocationFailed", p.face);
            const auto tolerance = Value(o.quality.initialWorkToleranceMm);
            d->brep->SetTolerance(tolerance, FALSE);
            for (const auto& vertex : p.vertices)
            {
                Step(vertex.record);
                auto value = ctor.StartVertex(Native(Value(vertex.pointMm)));
                Need(value != nullptr, "NativeVertexConstructionFailed", vertex.record);
                d->vertices.push_back(value);
                value->SetTolerance(tolerance, FALSE);
                Step(vertex.record);
                Good(ctor.EndVertex());
            }
            SmShell* outer = nullptr;
            SmShell* inner = nullptr;
            Step(p.face);
            Good(ctor.StartShell(outer, inner));
            Need(outer && !inner, "NativeOpenShellRoleMismatch", p.face);
            Step(p.face);
            d->face = ctor.StartFace(p.faceSameSense ? SM_OT_SAME : SM_OT_OPPOSITE);
            Need(d->face != nullptr, "NativeFaceConstructionFailed", p.face);
            d->face->SetTolerance(tolerance, FALSE);
            const auto x = Unit(p.x, p.xNormSquared), y = Unit(p.y, p.yNormSquared), origin = Enclose(p.originMm);
            std::array<double, 4> window{ std::numeric_limits<double>::infinity(),
                                          std::numeric_limits<double>::infinity(),
                                          -std::numeric_limits<double>::infinity(),
                                          -std::numeric_limits<double>::infinity() };
            for (const auto& edge : p.edges)
            {
                for (const auto& point : { edge.minimumMm, edge.maximumMm })
                {
                    const auto delta = Subtract(Enclose(point), origin);
                    const Interval uv[2] = { detail::Dot(delta, x), detail::Dot(delta, y) };
                    for (std::size_t i = 0; i < 2; ++i)
                    {
                        window[i] = std::min(window[i], uv[i].low);
                        window[i + 2] = std::max(window[i + 2], uv[i].high);
                    }
                }
            }
            for (std::size_t i = 0; i < 2; ++i)
            {
                const double padding = 64 * std::numeric_limits<double>::epsilon() * (1 + std::max(std::abs(window[i]), std::abs(window[i + 2])));
                window[i] = Down(window[i] - padding);
                window[i + 2] = Up(window[i + 2] + padding);
            }
            const auto xValue = Value(p.x), yValue = Value(p.y);
            const double nx = std::sqrt(Value(p.xNormSquared)), ny = std::sqrt(Value(p.yNormSquared));
            const SmExtent2d domain(window[0], window[1], window[2], window[3]);
            Step(p.plane);
            std::unique_ptr<SmPlane> plane(new (*d->context) SmPlane(
                Native(Value(p.originMm)),
                SmVector3d(xValue[0] / nx, xValue[1] / nx, xValue[2] / nx),
                SmVector3d(yValue[0] / ny, yValue[1] / ny, yValue[2] / ny),
                SmVector2d(1, 1),
                domain,
                d->context.get()
            ));
            Step(p.face);
            const auto surfaceStatus = ctor.SetFaceSurface(plane.get(), domain);
            if (plane->GetOwner() == d->face)
            {
                d->plane = plane.release();
            }
            Good(surfaceStatus);
            Need(d->plane != nullptr, "NativeSurfaceOwnershipMismatch", p.face);
            Step(p.bound);
            Need(ctor.StartLoop(SM_OT_SAME) != nullptr, "NativeLoopConstructionFailed", p.bound);
            d->edges.resize(p.edges.size());
            d->curves.resize(p.edges.size());
            for (const auto& use : p.primaryUses)
            {
                const auto& edge = p.edges.at(use.edge);
                Step(edge.record);
                auto native = ctor.StartEdge(
                    use.forward ? SM_OT_SAME : SM_OT_OPPOSITE,
                    nullptr,
                    d->vertices.at(edge.minimumVertex),
                    d->vertices.at(edge.maximumVertex)
                );
                Need(native != nullptr, "NativeEdgeConstructionFailed", edge.record);
                native->SetTolerance(tolerance, FALSE);
                d->edges[use.edge] = native;
                Step(edge.record);
                SmLine* raw = nullptr;
                const auto created = SmLine::CreateLineSegment(*d->context, 3, Native(Value(edge.minimumMm)), Native(Value(edge.maximumMm)), raw);
                std::unique_ptr<SmLine> curve(raw);
                Good(created);
                Need(bool(curve), "NativeCurveAllocationFailed", edge.record);
                Step(edge.record);
                const auto adopted = ctor.SetEdgeCurve(curve.get(), FALSE);
                if (curve->GetOwner() == native)
                {
                    d->curves[use.edge] = curve.release();
                }
                Good(adopted);
                Need(d->curves[use.edge] != nullptr, "NativeCurveOwnershipMismatch", edge.record);
                Step(edge.record);
                Good(ctor.SetLimits(SmVertexNode(0.0), SmVertexNode(1.0)));
                Step(edge.record);
                Good(ctor.EndEdge());
            }
            Step(p.bound);
            Good(ctor.EndLoop());
            Step(p.face);
            Good(ctor.EndFace());
            Step(p.face);
            Good(ctor.EndShell(FALSE));
            Step(p.face);
            Good(ctor.EndBrep());
            Observe("Constructed");
            InstallUv();
            Observe("UvInstalled");
            SmAssertArray pointers;
            Step(p.face);
            const auto pointerStatus = d->brep->ValidatePointers(&pointers);
            r.pointerReports = pointers.GetSize();
            Need(pointerStatus == SM_SUCCESS && pointers.GetSize() == 0, "NativePointerValidationFailed", p.face);
            Observe("PointersChecked");
            SmAssertArray deep;
            Step(p.face);
            const bool valid = d->brep->AssertValid(&deep, SM_LEVEL_2, SM_WALK) != FALSE;
            r.deepReports = deep.GetSize();
            Need(valid && deep.GetSize() == 0, "NativeDeepValidationFailed", p.face);
            Observe("DeepChecked");
            Verify();
            Measure();
            Observe("Postchecked");
            for (const auto& edge : p.edges)
            {
                r.edges.push_back(edge.mapping);
            }
            return d;
        }
        catch (...)
        {
            const bool held = bool(d->brep);
            d.reset();
            r.candidateDiscarded = held;
            throw;
        }
    }

private:

    const Plan& p;
    const Options& o;
    Report& r;
    std::shared_ptr<ModelData> d;
    std::map<std::string, double> initialTolerances;
    std::string Id(std::size_t record) const
    {
        return p.source->Source().Records().at(record).id;
    }
    void Step(std::size_t record)
    {
        if (r.nativeCalls >= o.nativeLimits.nativeCalls)
        {
            throw Failure{ Status::ResourceLimit, "NativeCallLimit", "Bounded native call allowance exhausted", record };
        }
        ++r.nativeCalls;
    }
    void Observe(const char* stage)
    {
        const auto observe = [&](const char* kind, std::string id, const SmObject* object)
        {
            const double value = SmTol::GetZoneTol3d(object);
            r.tolerances.push_back({ stage, kind, id, value });
            Need(
                std::isfinite(value) && value > 0 && value <= Enclose(o.quality.maximumWorkToleranceMm).low,
                "NativeWorkingToleranceMismatch",
                p.face
            );
            const auto key = std::string(kind) + ":" + id;
            const auto [found, added] = initialTolerances.emplace(key, value);
            Need(!added || value == Value(o.quality.initialWorkToleranceMm), "NativeInitialToleranceMismatch", p.face);
            const auto growth = value == found->second ? 0.0 : (Exact(value) - Exact(found->second)).high;
            Need(growth <= Enclose(o.quality.toleranceGrowthMm).low, "NativeToleranceGrowth", p.face);
        };
        observe("Brep", Id(p.face), d->brep.get());
        observe("Face", Id(p.face), d->face);
        for (std::size_t i = 0; i < d->vertices.size(); ++i)
        {
            observe("Vertex", Id(p.vertices[i].record), d->vertices[i]);
        }
        for (std::size_t i = 0; i < d->edges.size(); ++i)
        {
            observe("Edge", Id(p.edges[i].record), d->edges[i]);
        }
    }
    Point Uv(const Point& point) const
    {
        const auto& position = d->plane->GetPosition();
        const auto x = PointOf(position.GetXAxisRef()), y = PointOf(position.GetYAxisRef());
        const auto delta = Delta(point, PointOf(position.GetOriginRef()));
        const double xx = DotPoint(x, x), yy = DotPoint(y, y), xy = DotPoint(x, y), a = DotPoint(delta, x), b = DotPoint(delta, y),
                     det = xx * yy - xy * xy;
        if (!(det > 0))
        {
            throw std::domain_error("UnestablishedNativePlaneInverse");
        }
        return { (a * yy - b * xy) / det, (b * xx - a * xy) / det, 0 };
    }
    void InstallUv()
    {
        for (std::size_t i = 0; i < d->edges.size(); ++i)
        {
            SmTArray<SmEdgeuse*> uses;
            Good(d->edges[i]->GetEdgeuses(uses));
            Need(uses.GetSize() == 2, "NativeOccurrenceMultiplicity", p.edges[i].record);
            auto same = uses[0]->GetOrientation() == SM_OT_SAME ? uses[0] : uses[1];
            SmTArray<SmPoint3d> points;
            SmTArray<double> weights;
            Good(d->curves[i]->GetControlPolygon(points, weights));
            Need(points.GetSize() == 2, "NativeCurveControlShape", p.edges[i].record);
            Step(p.edges[i].record);
            SmLine* raw = nullptr;
            const auto status = SmLine::CreateLineSegment(*d->context, 2, Native(Uv(PointOf(points[0]))), Native(Uv(PointOf(points[1]))), raw);
            std::unique_ptr<SmLine> uv(raw);
            Good(status);
            Need(bool(uv), "NativeUvAllocationFailed", p.edges[i].record);
            const auto plane = PlaneCoefficients(*d->plane, false);
            const double bound = std::max(
                LiftBound(plane, LineCoefficients(*uv, false), LineCoefficients(*d->curves[i], false)),
                LiftBound(plane, LineCoefficients(*uv, true), LineCoefficients(*d->curves[i], true))
            );
            Certify(bound, o.quality.derivedUvMm, "NativeUvErrorBudget", p.edges[i].record);
            Step(p.edges[i].record);
            same->SetUVTrimCurve(uv.get(), bound);
            if (uv->GetOwner() == same)
            {
                uv.release();
            }
            Need(
                same->GetUVTrimCurvePointer() != nullptr && same->GetMate()->GetUVTrimCurvePointer() == nullptr,
                "NativeUvStorageSideMismatch",
                p.edges[i].record
            );
        }
    }
    void VerifyTraversal(SmFaceuse* faceuse, const std::vector<UsePlan>& expected)
    {
        SmTArray<SmLoopuse*> loops;
        faceuse->GetLoopuses(loops);
        Need(loops.GetSize() == 1, "NativeLoopMultiplicity", p.face);
        SmTArray<SmEdgeuse*> uses;
        loops[0]->GetEdgeuses(uses);
        Need(uses.GetSize() == expected.size(), "NativeLoopUseCount", p.face);
        bool equal = false;
        for (std::size_t offset = 0; offset < expected.size(); ++offset)
        {
            bool matches = true;
            for (std::size_t i = 0; i < expected.size(); ++i)
            {
                const auto& wanted = expected[(offset + i) % expected.size()];
                const auto& edge = p.edges[wanted.edge];
                const auto use = uses[static_cast<ULONG>(i)];
                matches = matches && use->GetEdge() == d->edges[wanted.edge] &&
                          use->GetVertexuse()->GetVertex() == d->vertices[wanted.forward ? edge.minimumVertex : edge.maximumVertex] &&
                          use->GetMate()->GetVertexuse()->GetVertex() == d->vertices[wanted.forward ? edge.maximumVertex : edge.minimumVertex];
            }
            equal = equal || matches;
        }
        Need(equal, "NativeDirectedSourcePathMismatch", p.face);
    }
    void Verify()
    {
        SmTArray<SmFace*> faces;
        SmTArray<SmVertex*> vertices;
        SmTArray<SmEdge*> edges;
        d->brep->GetFaces(faces);
        d->brep->GetVertices(vertices);
        d->brep->GetEdges(edges);
        Need(
            faces.GetSize() == 1 && edges.GetSize() == p.edges.size() && vertices.GetSize() == p.vertices.size() && !d->brep->IsManifoldSolid(),
            "NativeShapeRoleOrIdentityMismatch",
            p.face
        );
        Need(
            faces[0] == d->face && d->face->GetSurface() == d->plane && d->plane->GetOwner() == d->face && d->face->GetContext() == d->context.get(),
            "NativeFaceIdentityMismatch",
            p.face
        );
        std::set<SmVertex*> actualVertices;
        std::set<SmEdge*> actualEdges;
        for (ULONG i = 0; i < vertices.GetSize(); ++i)
        {
            actualVertices.insert(vertices[i]);
        }
        for (ULONG i = 0; i < edges.GetSize(); ++i)
        {
            actualEdges.insert(edges[i]);
        }
        Need(
            actualVertices == std::set<SmVertex*>(d->vertices.begin(), d->vertices.end()) &&
                actualEdges == std::set<SmEdge*>(d->edges.begin(), d->edges.end()),
            "NativeTopologyIdentityMismatch",
            p.face
        );
        for (std::size_t i = 0; i < d->edges.size(); ++i)
        {
            const auto edge = d->edges[i];
            Need(
                edge->GetCurve() == d->curves[i] && d->curves[i]->GetOwner() == edge && edge->GetContext() == d->context.get() &&
                    d->curves[i]->GetContext() == d->context.get() && edge->GetInterval().GetMin() == 0 && edge->GetInterval().GetMax() == 1,
                "NativeEdgeIdentityOrIntervalMismatch",
                p.edges[i].record
            );
        }
        SmFaceuse* same = nullptr;
        SmFaceuse* opposite = nullptr;
        d->face->GetFaceuses(same, opposite);
        Need(
            same && opposite && same->GetOrientation() == SM_OT_SAME && opposite->GetOrientation() == SM_OT_OPPOSITE,
            "NativeFaceSideMismatch",
            p.face
        );
        VerifyTraversal(same, p.primaryUses);
        VerifyTraversal(p.faceSameSense ? same : opposite, p.selectedUses);
        double definition = 0, uvError = 0;
        const auto domain = d->plane->GetNaturalUVDomain();
        const auto faceDomain = d->face->GetUVDomain();
        Need(
            domain.GetMin().x == faceDomain.GetMin().x && domain.GetMin().y == faceDomain.GetMin().y && domain.GetMax().x == faceDomain.GetMax().x &&
                domain.GetMax().y == faceDomain.GetMax().y,
            "NativeFaceDomainMismatch",
            p.face
        );
        for (bool polygon : { false, true })
        {
            const auto nativePlane = PlaneCoefficients(*d->plane, polygon);
            const auto sourceOrigin = Enclose(p.originMm), sourceX = Unit(p.x, p.xNormSquared), sourceY = Unit(p.y, p.yNormSquared);
            for (double u : { domain.GetMin().x, domain.GetMax().x })
            {
                for (double v : { domain.GetMin().y, domain.GetMax().y })
                {
                    const auto exact = Add(Add(sourceOrigin, Times(sourceX, Exact(u))), Times(sourceY, Exact(v)));
                    definition = std::max(definition, NormUpper(Subtract(Evaluate(nativePlane, Exact(u), Exact(v)), exact)));
                }
            }
            for (std::size_t i = 0; i < d->edges.size(); ++i)
            {
                SmTArray<SmEdgeuse*> uses;
                Good(d->edges[i]->GetEdgeuses(uses));
                auto sameUse = uses[0]->GetOrientation() == SM_OT_SAME ? uses[0] : uses[1];
                SmEdgeuse* owner = nullptr;
                auto uv = sameUse->GetUVTrimCurve(&owner);
                Need(
                    owner == sameUse && uv && uv->GetOwner() == sameUse && uv->GetContext() == d->context.get(),
                    "NativeUvOwnerMismatch",
                    p.edges[i].record
                );
                const auto line = dynamic_cast<const SmLine*>(uv);
                Need(line != nullptr, "NativeUvNotAffine", p.edges[i].record);
                for (bool curvePolygon : { false, true })
                {
                    const auto curve = LineCoefficients(*d->curves[i], curvePolygon);
                    definition = std::max(
                        { definition,
                          NormUpper(Subtract(curve.origin, Enclose(p.edges[i].minimumMm))),
                          NormUpper(Subtract(Add(curve.origin, curve.delta), Enclose(p.edges[i].maximumMm))) }
                    );
                    for (bool uvPolygon : { false, true })
                    {
                        const auto trim = LineCoefficients(*line, uvPolygon);
                        uvError = std::max(uvError, LiftBound(nativePlane, trim, curve));
                        for (const auto& endpoint : { trim.origin, Add(trim.origin, trim.delta) })
                        {
                            Need(
                                endpoint[0].low >= domain.GetMin().x && endpoint[0].high <= domain.GetMax().x &&
                                    endpoint[1].low >= domain.GetMin().y && endpoint[1].high <= domain.GetMax().y,
                                "NativeTrimOutsideWindow",
                                p.face
                            );
                        }
                    }
                }
            }
        }
        for (std::size_t i = 0; i < d->vertices.size(); ++i)
        {
            definition = std::max(definition, NormUpper(Subtract(Exact(PointOf(d->vertices[i]->GetPoint())), Enclose(p.vertices[i].pointMm))));
        }
        r.definitionBoundMm = definition;
        r.uvBoundMm = uvError;
        Certify(definition, o.quality.definitionMm, "NativeDefinitionErrorBudget", p.face);
        Certify(uvError, o.quality.derivedUvMm, "NativeUvErrorBudget", p.face);
    }
    void Measure()
    {
        Statistics info;
        info.faces = 1;
        info.edges = d->edges.size();
        info.vertices = d->vertices.size();
        info.shapeRole = "OpenSurfaceModel";
        Step(p.face);
        SmPoint3d centroid;
        Good(SmApiFaceComputeCentroid(d->face, 1e-4, info.areaMm2, centroid));
        info.centroidMm = PointOf(centroid);
        for (std::size_t i = 0; i < d->edges.size(); ++i)
        {
            double length = 0;
            Step(p.edges[i].record);
            Good(SmApiEdgeComputeLength(d->edges[i], Value(o.quality.queryMm), length));
            info.boundaryMm += length;
        }
        SmPoint3d minimum, maximum;
        Step(p.face);
        Good(SmApiFaceBoundingBox(d->face, TRUE, minimum, maximum));
        info.boundsMm = { PointOf(minimum), PointOf(maximum) };
        info.normal = PointOf(d->plane->GetPosition().GetZAxis());
        if (!p.faceSameSense)
        {
            for (auto& value : info.normal)
            {
                value = -value;
            }
        }
        const auto domain = d->plane->GetNaturalUVDomain();
        info.windowMm = { domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y };
        const auto expectedArea = SqrtNonnegative(Enclose(p.sourceAreaSquared));
        const auto error = Exact(info.areaMm2) - expectedArea;
        Certify(std::max(std::abs(error.low), std::abs(error.high)), o.quality.areaMm2, "NativeAreaQueryMismatch", p.face);
        const auto checkVector = [&](const Point& actual, const IntervalPoint& expected, const char* key)
        {
            Certify(NormUpper(detail::Subtract(Exact(actual), expected)), o.quality.queryMm, key, p.face);
        };
        checkVector(info.centroidMm, Enclose(p.sourceCentroid), "NativeCentroidQueryMismatch");
        checkVector(info.boundsMm[0], Enclose(p.sourceMinimum), "NativeBoundsQueryMismatch");
        checkVector(info.boundsMm[1], Enclose(p.sourceMaximum), "NativeBoundsQueryMismatch");
        auto expectedNormal = Cross(Unit(p.x, p.xNormSquared), Unit(p.y, p.yNormSquared));
        if (!p.faceSameSense)
        {
            expectedNormal = Times(expectedNormal, Exact(-1));
        }
        checkVector(info.normal, expectedNormal, "NativeNormalMismatch");
        auto expectedLength = Exact(0);
        for (const auto& edge : p.edges)
        {
            expectedLength = expectedLength + SqrtNonnegative(Enclose(edge.sourceLengthSquared));
        }
        const auto lengthError = Exact(info.boundaryMm) - expectedLength;
        Certify(std::max(std::abs(lengthError.low), std::abs(lengthError.high)), o.quality.queryMm, "NativeLengthQueryMismatch", p.face);
        d->info = info;
        r.native = info;
    }
};
} // namespace
std::shared_ptr<ModelData> Build(const Plan& plan, const Options& options, Report& report)
{
    return Builder(plan, options, report).Run();
}
std::shared_ptr<const ModelSnapshot> Access::Snapshot(std::shared_ptr<ModelData> data)
{
    return std::shared_ptr<const ModelSnapshot>(new ModelSnapshot(std::move(data)));
}
} // namespace step_import::detail

namespace step_import
{
ModelSnapshot::ModelSnapshot(std::shared_ptr<detail::ModelData> data) : m_data(std::move(data))
{
}
Statistics ModelSnapshot::Info() const
{
    return m_data->info;
}
FaceView ModelSnapshot::Face() const
{
    return FaceView(m_data);
}
std::shared_ptr<const step_semantics::Snapshot> ModelSnapshot::Source() const
{
    return m_data->source;
}
std::optional<double> ModelSnapshot::MaterialVolumeMm3() const
{
    return {};
}
FaceView::FaceView(std::shared_ptr<detail::ModelData> data) : m_data(std::move(data))
{
}
std::string FaceView::SourceId() const
{
    return m_data->faceId;
}
Point FaceView::EvaluateSurface(double u, double v) const
{
    std::lock_guard lock(m_data->mutex);
    const auto domain = m_data->plane->GetNaturalUVDomain();
    if (!std::isfinite(u) || !std::isfinite(v) || u < domain.GetMin().x || u > domain.GetMax().x || v < domain.GetMin().y || v > domain.GetMax().y)
    {
        throw std::out_of_range("Surface query lies outside the verified window");
    }
    SmPoint3d point;
    detail::Good(m_data->plane->EvaluatePoint(SmPoint2d(u, v), point));
    return detail::PointOf(point);
}
ClosestPoint FaceView::Closest(const Point& point) const
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
        throw std::invalid_argument("Finite query required");
    }
    std::lock_guard lock(m_data->mutex);
    SmPoint3d closest;
    double distance = 0;
    detail::Good(SmApiFaceClosestPoint(m_data->face, detail::Native(point), closest, distance));
    return { detail::PointOf(closest), distance };
}
PointClass FaceView::ClassifyUV(double u, double v) const
{
    if (!std::isfinite(u) || !std::isfinite(v))
    {
        throw std::invalid_argument("Finite query required");
    }
    std::lock_guard lock(m_data->mutex);
    int classification = 0;
    detail::Good(SmApiFaceClassifyUV(m_data->face, SmPoint2d(u, v), classification));
    switch (classification)
    {
        case SM_PC_FACE:
            return PointClass::Interior;
        case SM_PC_EDGE:
            return PointClass::Edge;
        case SM_PC_VERTEX:
            return PointClass::Vertex;
        default:
            return PointClass::Outside;
    }
}
} // namespace step_import
