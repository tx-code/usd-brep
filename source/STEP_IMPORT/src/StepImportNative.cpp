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

    Builder(const Plan& plan, const Options& options, Report& report)
        : m_plan(plan), m_options(options), m_report(report), m_model(std::make_shared<ModelData>())
    {
        m_model->source = m_plan.source;
        m_model->faceId = Id(m_plan.face);
    }
    std::shared_ptr<ModelData> Run()
    {
        if (!std::numeric_limits<double>::is_iec559 || std::fegetround() != FE_TONEAREST)
        {
            throw Failure{ Status::Indeterminate,
                           "UnsupportedNativeNumericEnvironment",
                           "Outward binary64 evidence requires nearest rounding",
                           m_plan.face };
        }
        try
        {
            m_report.kernelPath = LoadedKernel();
            Step(m_plan.face);
            m_model->context = std::make_unique<SmContext>();
            SmBrepConstructor ctor;
            Step(m_plan.face);
            m_model->brep.reset(ctor.StartBrep(*m_model->context));
            Need(bool(m_model->brep), "NativeBrepAllocationFailed", m_plan.face);
            const auto tolerance = Value(m_options.quality.initialWorkToleranceMm);
            m_model->brep->SetTolerance(tolerance, FALSE);
            for (const auto& vertex : m_plan.vertices)
            {
                Step(vertex.record);
                auto value = ctor.StartVertex(Native(Value(vertex.pointMm)));
                Need(value != nullptr, "NativeVertexConstructionFailed", vertex.record);
                m_model->vertices.push_back(value);
                value->SetTolerance(tolerance, FALSE);
                Step(vertex.record);
                Good(ctor.EndVertex());
            }
            SmShell* outer = nullptr;
            SmShell* inner = nullptr;
            Step(m_plan.face);
            Good(ctor.StartShell(outer, inner));
            Need(outer && !inner, "NativeOpenShellRoleMismatch", m_plan.face);
            Step(m_plan.face);
            m_model->face = ctor.StartFace(m_plan.faceSameSense ? SM_OT_SAME : SM_OT_OPPOSITE);
            Need(m_model->face != nullptr, "NativeFaceConstructionFailed", m_plan.face);
            m_model->face->SetTolerance(tolerance, FALSE);
            const auto x = Unit(m_plan.x, m_plan.xNormSquared), y = Unit(m_plan.y, m_plan.yNormSquared), origin = Enclose(m_plan.originMm);
            std::array<double, 4> window{ std::numeric_limits<double>::infinity(),
                                          std::numeric_limits<double>::infinity(),
                                          -std::numeric_limits<double>::infinity(),
                                          -std::numeric_limits<double>::infinity() };
            for (const auto& edge : m_plan.edges)
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
            const auto xValue = Value(m_plan.x), yValue = Value(m_plan.y);
            const double nx = std::sqrt(Value(m_plan.xNormSquared)), ny = std::sqrt(Value(m_plan.yNormSquared));
            const SmExtent2d domain(window[0], window[1], window[2], window[3]);
            Step(m_plan.plane);
            std::unique_ptr<SmPlane> plane(new (*m_model->context) SmPlane(
                Native(Value(m_plan.originMm)),
                SmVector3d(xValue[0] / nx, xValue[1] / nx, xValue[2] / nx),
                SmVector3d(yValue[0] / ny, yValue[1] / ny, yValue[2] / ny),
                SmVector2d(1, 1),
                domain,
                m_model->context.get()
            ));
            Step(m_plan.face);
            const auto surfaceStatus = ctor.SetFaceSurface(plane.get(), domain);
            if (plane->GetOwner() == m_model->face)
            {
                m_model->plane = plane.release();
            }
            Good(surfaceStatus);
            Need(m_model->plane != nullptr, "NativeSurfaceOwnershipMismatch", m_plan.face);
            Step(m_plan.bound);
            Need(ctor.StartLoop(SM_OT_SAME) != nullptr, "NativeLoopConstructionFailed", m_plan.bound);
            m_model->edges.resize(m_plan.edges.size());
            m_model->curves.resize(m_plan.edges.size());
            for (const auto& use : m_plan.primaryUses)
            {
                const auto& edge = m_plan.edges.at(use.edge);
                Step(edge.record);
                auto native = ctor.StartEdge(
                    use.forward ? SM_OT_SAME : SM_OT_OPPOSITE,
                    nullptr,
                    m_model->vertices.at(edge.minimumVertex),
                    m_model->vertices.at(edge.maximumVertex)
                );
                Need(native != nullptr, "NativeEdgeConstructionFailed", edge.record);
                native->SetTolerance(tolerance, FALSE);
                m_model->edges[use.edge] = native;
                Step(edge.record);
                SmLine* raw = nullptr;
                const auto
                    created = SmLine::CreateLineSegment(*m_model->context, 3, Native(Value(edge.minimumMm)), Native(Value(edge.maximumMm)), raw);
                std::unique_ptr<SmLine> curve(raw);
                Good(created);
                Need(bool(curve), "NativeCurveAllocationFailed", edge.record);
                Step(edge.record);
                const auto adopted = ctor.SetEdgeCurve(curve.get(), FALSE);
                if (curve->GetOwner() == native)
                {
                    m_model->curves[use.edge] = curve.release();
                }
                Good(adopted);
                Need(m_model->curves[use.edge] != nullptr, "NativeCurveOwnershipMismatch", edge.record);
                Step(edge.record);
                Good(ctor.SetLimits(SmVertexNode(0.0), SmVertexNode(1.0)));
                Step(edge.record);
                Good(ctor.EndEdge());
            }
            Step(m_plan.bound);
            Good(ctor.EndLoop());
            Step(m_plan.face);
            Good(ctor.EndFace());
            Step(m_plan.face);
            Good(ctor.EndShell(FALSE));
            Step(m_plan.face);
            Good(ctor.EndBrep());
            Observe("Constructed");
            InstallUv();
            Observe("UvInstalled");
            SmAssertArray pointers;
            Step(m_plan.face);
            const auto pointerStatus = m_model->brep->ValidatePointers(&pointers);
            m_report.pointerReports = pointers.GetSize();
            Need(pointerStatus == SM_SUCCESS && pointers.GetSize() == 0, "NativePointerValidationFailed", m_plan.face);
            Observe("PointersChecked");
            SmAssertArray deep;
            Step(m_plan.face);
            const bool valid = m_model->brep->AssertValid(&deep, SM_LEVEL_2, SM_WALK) != FALSE;
            m_report.deepReports = deep.GetSize();
            Need(valid && deep.GetSize() == 0, "NativeDeepValidationFailed", m_plan.face);
            Observe("DeepChecked");
            Verify();
            Measure();
            Observe("Postchecked");
            for (const auto& edge : m_plan.edges)
            {
                m_report.edges.push_back(edge.mapping);
            }
            return m_model;
        }
        catch (...)
        {
            const bool held = bool(m_model->brep);
            m_model.reset();
            m_report.candidateDiscarded = held;
            throw;
        }
    }

private:

    const Plan& m_plan;
    const Options& m_options;
    Report& m_report;
    std::shared_ptr<ModelData> m_model;
    std::map<std::string, double> initialTolerances;
    std::string Id(std::size_t record) const
    {
        return m_plan.source->Source().Records().at(record).id;
    }
    void Step(std::size_t record)
    {
        if (m_report.nativeCalls >= m_options.nativeLimits.nativeCalls)
        {
            throw Failure{ Status::ResourceLimit, "NativeCallLimit", "Bounded native call allowance exhausted", record };
        }
        ++m_report.nativeCalls;
    }
    void Observe(const char* stage)
    {
        const auto observe = [&](const char* kind, std::string id, const SmObject* object)
        {
            const double value = SmTol::GetZoneTol3d(object);
            m_report.tolerances.push_back({ stage, kind, id, value });
            Need(
                std::isfinite(value) && value > 0 && value <= Enclose(m_options.quality.maximumWorkToleranceMm).low,
                "NativeWorkingToleranceMismatch",
                m_plan.face
            );
            const auto key = std::string(kind) + ":" + id;
            const auto [found, added] = initialTolerances.emplace(key, value);
            Need(!added || value == Value(m_options.quality.initialWorkToleranceMm), "NativeInitialToleranceMismatch", m_plan.face);
            const auto growth = value == found->second ? 0.0 : (Exact(value) - Exact(found->second)).high;
            Need(growth <= Enclose(m_options.quality.toleranceGrowthMm).low, "NativeToleranceGrowth", m_plan.face);
        };
        observe("Brep", Id(m_plan.face), m_model->brep.get());
        observe("Face", Id(m_plan.face), m_model->face);
        for (std::size_t i = 0; i < m_model->vertices.size(); ++i)
        {
            observe("Vertex", Id(m_plan.vertices[i].record), m_model->vertices[i]);
        }
        for (std::size_t i = 0; i < m_model->edges.size(); ++i)
        {
            observe("Edge", Id(m_plan.edges[i].record), m_model->edges[i]);
        }
    }
    Point Uv(const Point& point) const
    {
        const auto& position = m_model->plane->GetPosition();
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
        for (std::size_t i = 0; i < m_model->edges.size(); ++i)
        {
            SmTArray<SmEdgeuse*> uses;
            Good(m_model->edges[i]->GetEdgeuses(uses));
            Need(uses.GetSize() == 2, "NativeOccurrenceMultiplicity", m_plan.edges[i].record);
            auto same = uses[0]->GetOrientation() == SM_OT_SAME ? uses[0] : uses[1];
            SmTArray<SmPoint3d> points;
            SmTArray<double> weights;
            Good(m_model->curves[i]->GetControlPolygon(points, weights));
            Need(points.GetSize() == 2, "NativeCurveControlShape", m_plan.edges[i].record);
            Step(m_plan.edges[i].record);
            SmLine* raw = nullptr;
            const auto status = SmLine::CreateLineSegment(*m_model->context, 2, Native(Uv(PointOf(points[0]))), Native(Uv(PointOf(points[1]))), raw);
            std::unique_ptr<SmLine> uv(raw);
            Good(status);
            Need(bool(uv), "NativeUvAllocationFailed", m_plan.edges[i].record);
            const auto plane = PlaneCoefficients(*m_model->plane, false);
            const double bound = std::max(
                LiftBound(plane, LineCoefficients(*uv, false), LineCoefficients(*m_model->curves[i], false)),
                LiftBound(plane, LineCoefficients(*uv, true), LineCoefficients(*m_model->curves[i], true))
            );
            Certify(bound, m_options.quality.derivedUvMm, "NativeUvErrorBudget", m_plan.edges[i].record);
            Step(m_plan.edges[i].record);
            same->SetUVTrimCurve(uv.get(), bound);
            if (uv->GetOwner() == same)
            {
                uv.release();
            }
            Need(
                same->GetUVTrimCurvePointer() != nullptr && same->GetMate()->GetUVTrimCurvePointer() == nullptr,
                "NativeUvStorageSideMismatch",
                m_plan.edges[i].record
            );
        }
    }
    void VerifyTraversal(SmFaceuse* faceuse, const std::vector<UsePlan>& expected)
    {
        SmTArray<SmLoopuse*> loops;
        faceuse->GetLoopuses(loops);
        Need(loops.GetSize() == 1, "NativeLoopMultiplicity", m_plan.face);
        SmTArray<SmEdgeuse*> uses;
        loops[0]->GetEdgeuses(uses);
        Need(uses.GetSize() == expected.size(), "NativeLoopUseCount", m_plan.face);
        bool equal = false;
        for (std::size_t offset = 0; offset < expected.size(); ++offset)
        {
            bool matches = true;
            for (std::size_t i = 0; i < expected.size(); ++i)
            {
                const auto& wanted = expected[(offset + i) % expected.size()];
                const auto& edge = m_plan.edges[wanted.edge];
                const auto use = uses[static_cast<ULONG>(i)];
                matches = matches && use->GetEdge() == m_model->edges[wanted.edge] &&
                          use->GetVertexuse()->GetVertex() == m_model->vertices[wanted.forward ? edge.minimumVertex : edge.maximumVertex] &&
                          use->GetMate()->GetVertexuse()->GetVertex() == m_model->vertices[wanted.forward ? edge.maximumVertex : edge.minimumVertex];
            }
            equal = equal || matches;
        }
        Need(equal, "NativeDirectedSourcePathMismatch", m_plan.face);
    }
    void Verify()
    {
        SmTArray<SmFace*> faces;
        SmTArray<SmVertex*> vertices;
        SmTArray<SmEdge*> edges;
        m_model->brep->GetFaces(faces);
        m_model->brep->GetVertices(vertices);
        m_model->brep->GetEdges(edges);
        Need(
            faces.GetSize() == 1 && edges.GetSize() == m_plan.edges.size() && vertices.GetSize() == m_plan.vertices.size() &&
                !m_model->brep->IsManifoldSolid(),
            "NativeShapeRoleOrIdentityMismatch",
            m_plan.face
        );
        Need(
            faces[0] == m_model->face && m_model->face->GetSurface() == m_model->plane && m_model->plane->GetOwner() == m_model->face &&
                m_model->face->GetContext() == m_model->context.get(),
            "NativeFaceIdentityMismatch",
            m_plan.face
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
            actualVertices == std::set<SmVertex*>(m_model->vertices.begin(), m_model->vertices.end()) &&
                actualEdges == std::set<SmEdge*>(m_model->edges.begin(), m_model->edges.end()),
            "NativeTopologyIdentityMismatch",
            m_plan.face
        );
        for (std::size_t i = 0; i < m_model->edges.size(); ++i)
        {
            const auto edge = m_model->edges[i];
            Need(
                edge->GetCurve() == m_model->curves[i] && m_model->curves[i]->GetOwner() == edge && edge->GetContext() == m_model->context.get() &&
                    m_model->curves[i]->GetContext() == m_model->context.get() && edge->GetInterval().GetMin() == 0 &&
                    edge->GetInterval().GetMax() == 1,
                "NativeEdgeIdentityOrIntervalMismatch",
                m_plan.edges[i].record
            );
        }
        SmFaceuse* same = nullptr;
        SmFaceuse* opposite = nullptr;
        m_model->face->GetFaceuses(same, opposite);
        Need(
            same && opposite && same->GetOrientation() == SM_OT_SAME && opposite->GetOrientation() == SM_OT_OPPOSITE,
            "NativeFaceSideMismatch",
            m_plan.face
        );
        VerifyTraversal(same, m_plan.primaryUses);
        VerifyTraversal(m_plan.faceSameSense ? same : opposite, m_plan.selectedUses);
        double definition = 0, consistency = 0, uvError = 0;
        const auto domain = m_model->plane->GetNaturalUVDomain();
        const auto faceDomain = m_model->face->GetUVDomain();
        Need(
            domain.GetMin().x == faceDomain.GetMin().x && domain.GetMin().y == faceDomain.GetMin().y && domain.GetMax().x == faceDomain.GetMax().x &&
                domain.GetMax().y == faceDomain.GetMax().y,
            "NativeFaceDomainMismatch",
            m_plan.face
        );
        for (bool polygon : { false, true })
        {
            const auto nativePlane = PlaneCoefficients(*m_model->plane, polygon);
            for (const auto vertex : m_model->vertices)
            {
                const auto point = PointOf(vertex->GetPoint());
                const Affine constantUv{ Exact(Uv(point)), Exact(Point{}) };
                const Affine constantPoint{ Exact(point), Exact(Point{}) };
                consistency = std::max(consistency, LiftBound(nativePlane, constantUv, constantPoint));
            }
            const auto sourceOrigin = Enclose(m_plan.originMm), sourceX = Unit(m_plan.x, m_plan.xNormSquared),
                       sourceY = Unit(m_plan.y, m_plan.yNormSquared);
            for (double u : { domain.GetMin().x, domain.GetMax().x })
            {
                for (double v : { domain.GetMin().y, domain.GetMax().y })
                {
                    const auto exact = Add(Add(sourceOrigin, Times(sourceX, Exact(u))), Times(sourceY, Exact(v)));
                    definition = std::max(definition, NormUpper(Subtract(Evaluate(nativePlane, Exact(u), Exact(v)), exact)));
                }
            }
            for (std::size_t i = 0; i < m_model->edges.size(); ++i)
            {
                SmTArray<SmEdgeuse*> uses;
                Good(m_model->edges[i]->GetEdgeuses(uses));
                auto sameUse = uses[0]->GetOrientation() == SM_OT_SAME ? uses[0] : uses[1];
                SmEdgeuse* owner = nullptr;
                auto uv = sameUse->GetUVTrimCurve(&owner);
                Need(
                    owner == sameUse && uv && uv->GetOwner() == sameUse && uv->GetContext() == m_model->context.get(),
                    "NativeUvOwnerMismatch",
                    m_plan.edges[i].record
                );
                const auto line = dynamic_cast<const SmLine*>(uv);
                Need(line != nullptr, "NativeUvNotAffine", m_plan.edges[i].record);
                for (bool curvePolygon : { false, true })
                {
                    const auto curve = LineCoefficients(*m_model->curves[i], curvePolygon);
                    // Source consistency and definition conversion budgets do not compose into a native consistency proof.
                    // Compare the actual native endpoint/vertex pairs independently, for both stored curve representations.
                    consistency = std::max(
                        { consistency,
                          NormUpper(Subtract(curve.origin, Exact(PointOf(m_model->vertices[m_plan.edges[i].minimumVertex]->GetPoint())))),
                          NormUpper(
                              Subtract(Add(curve.origin, curve.delta), Exact(PointOf(m_model->vertices[m_plan.edges[i].maximumVertex]->GetPoint())))
                          ) }
                    );
                    definition = std::max(
                        { definition,
                          NormUpper(Subtract(curve.origin, Enclose(m_plan.edges[i].minimumMm))),
                          NormUpper(Subtract(Add(curve.origin, curve.delta), Enclose(m_plan.edges[i].maximumMm))) }
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
                                m_plan.face
                            );
                        }
                    }
                }
            }
        }
        for (std::size_t i = 0; i < m_model->vertices.size(); ++i)
        {
            definition = std::max(
                definition,
                NormUpper(Subtract(Exact(PointOf(m_model->vertices[i]->GetPoint())), Enclose(m_plan.vertices[i].pointMm)))
            );
        }
        m_report.definitionBoundMm = definition;
        m_report.consistencyBoundMm = std::max(consistency, uvError); // Whole-edge lift also bounds edge/plane consistency.
        m_report.uvBoundMm = uvError;
        Certify(definition, m_options.quality.definitionMm, "NativeDefinitionErrorBudget", m_plan.face);
        Certify(*m_report.consistencyBoundMm, m_options.quality.consistencyMm, "NativeConsistencyErrorBudget", m_plan.face);
        Certify(uvError, m_options.quality.derivedUvMm, "NativeUvErrorBudget", m_plan.face);
    }
    void Measure()
    {
        Statistics info;
        info.faces = 1;
        info.edges = m_model->edges.size();
        info.vertices = m_model->vertices.size();
        info.shapeRole = "OpenSurfaceModel";
        Step(m_plan.face);
        SmPoint3d centroid;
        Good(SmApiFaceComputeCentroid(m_model->face, 1e-4, info.areaMm2, centroid));
        info.centroidMm = PointOf(centroid);
        for (std::size_t i = 0; i < m_model->edges.size(); ++i)
        {
            double length = 0;
            Step(m_plan.edges[i].record);
            Good(SmApiEdgeComputeLength(m_model->edges[i], Value(m_options.quality.queryMm), length));
            info.boundaryMm += length;
        }
        SmPoint3d minimum, maximum;
        Step(m_plan.face);
        Good(SmApiFaceBoundingBox(m_model->face, TRUE, minimum, maximum));
        info.boundsMm = { PointOf(minimum), PointOf(maximum) };
        info.normal = PointOf(m_model->plane->GetPosition().GetZAxis());
        if (!m_plan.faceSameSense)
        {
            for (auto& value : info.normal)
            {
                value = -value;
            }
        }
        const auto domain = m_model->plane->GetNaturalUVDomain();
        info.windowMm = { domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y };
        const auto expectedArea = SqrtNonnegative(Enclose(m_plan.sourceAreaSquared));
        const auto error = Exact(info.areaMm2) - expectedArea;
        Certify(std::max(std::abs(error.low), std::abs(error.high)), m_options.quality.areaMm2, "NativeAreaQueryMismatch", m_plan.face);
        const auto checkVector = [&](const Point& actual, const IntervalPoint& expected, const char* key)
        {
            Certify(NormUpper(detail::Subtract(Exact(actual), expected)), m_options.quality.queryMm, key, m_plan.face);
        };
        checkVector(info.centroidMm, Enclose(m_plan.sourceCentroid), "NativeCentroidQueryMismatch");
        checkVector(info.boundsMm[0], Enclose(m_plan.sourceMinimum), "NativeBoundsQueryMismatch");
        checkVector(info.boundsMm[1], Enclose(m_plan.sourceMaximum), "NativeBoundsQueryMismatch");
        auto expectedNormal = Cross(Unit(m_plan.x, m_plan.xNormSquared), Unit(m_plan.y, m_plan.yNormSquared));
        if (!m_plan.faceSameSense)
        {
            expectedNormal = Times(expectedNormal, Exact(-1));
        }
        checkVector(info.normal, expectedNormal, "NativeNormalMismatch");
        auto expectedLength = Exact(0);
        for (const auto& edge : m_plan.edges)
        {
            expectedLength = expectedLength + SqrtNonnegative(Enclose(edge.sourceLengthSquared));
        }
        const auto lengthError = Exact(info.boundaryMm) - expectedLength;
        Certify(std::max(std::abs(lengthError.low), std::abs(lengthError.high)), m_options.quality.queryMm, "NativeLengthQueryMismatch", m_plan.face);
        m_model->info = info;
        m_report.native = info;
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
