// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportExecution.h"
#include "StepImportInterval.h"
#include "StepImportNativeData.h"

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
#include <SmLoop.h>
#include <SmLoopuse.h>
#include <SmPlane.h>
#include <SmPointClass.h>
#include <SmRegion.h>
#include <SmShell.h>
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
std::recursive_mutex& NativeMutex()
{
    static std::recursive_mutex mutex;
    return mutex;
}
ModelData::~ModelData()
{
    std::lock_guard lock(NativeMutex());
    brep.reset();
    context.reset();
}
struct EditableData
{
    std::unique_ptr<SmContext> context;
    std::unique_ptr<SmBrep> brep;
    ~EditableData()
    {
        std::lock_guard lock(NativeMutex());
        brep.reset();
        context.reset();
    }
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

    Builder(const Plan& plan, const Options& options, Report& report, Execution& execution)
        : m_plan(plan), m_options(options), m_report(report), m_model(std::make_shared<ModelData>()), m_execution(execution)
    {
        m_model->source = m_plan.source;
        m_model->queryTolerance = Value(m_options.quality.queryMm);
        m_model->queryBudget = m_options.quality.queryMm;
    }
    std::shared_ptr<ModelData> Run()
    {
        if (!std::numeric_limits<double>::is_iec559 || std::fegetround() != FE_TONEAREST)
        {
            throw Failure{ Status::Indeterminate,
                           "UnsupportedNativeNumericEnvironment",
                           "Outward binary64 evidence requires nearest rounding",
                           m_plan.record };
        }
        try
        {
            m_report.sourceShape = m_plan.sourceShape;
            m_report.units = m_plan.units;
            m_report.kernelPath = LoadedKernel();
            Step(m_plan.record);
            m_model->context = std::make_unique<SmContext>();
            SmBrepConstructor ctor;
            Step(m_plan.record);
            m_model->brep.reset(ctor.StartBrep(*m_model->context));
            Need(bool(m_model->brep), "NativeBrepAllocationFailed", m_plan.record);
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
            if (m_plan.closedBoundary)
            {
                Step(m_plan.record);
                m_model->bounded = ctor.StartRegion(m_plan.materialSolid ? TRUE : FALSE, nullptr);
                Need(
                    m_model->bounded != nullptr,
                    m_plan.materialSolid ? "NativeMaterialRegionAllocationFailed" : "NativeBoundedRegionAllocationFailed",
                    m_plan.record
                );
                m_model->bounded->SetIsVoid(m_plan.materialSolid ? FALSE : TRUE);
                m_model->material = m_plan.materialSolid ? m_model->bounded : nullptr;
            }
            SmShell* outer = nullptr;
            SmShell* inner = nullptr;
            Step(m_plan.record);
            Good(ctor.StartShell(outer, inner));
            Need(outer && bool(inner) == m_plan.closedBoundary, "NativeShellRoleMismatch", m_plan.record);
            m_model->outside = outer;
            m_model->inside = inner;
            m_model->edges.resize(m_plan.edges.size());
            m_model->curves.resize(m_plan.edges.size());
            m_model->faces.resize(m_plan.faces.size());
            for (std::size_t i = 0; i < m_plan.faces.size(); ++i)
            {
                BuildFace(ctor, m_plan.faces[i], m_model->faces[i]);
            }
            Step(m_plan.record);
            Good(ctor.EndShell(FALSE));
            if (m_plan.closedBoundary)
            {
                Step(m_plan.record);
                Good(ctor.EndRegion());
            }
            Step(m_plan.record);
            Good(ctor.EndBrep());
            Observe("Constructed");
            for (std::size_t i = 0; i < m_plan.faces.size(); ++i)
            {
                InstallUv(m_plan.faces[i], m_model->faces[i]);
            }
            Observe("UvInstalled");
            SmAssertArray pointers;
            Step(m_plan.record);
            const auto pointerStatus = m_model->brep->ValidatePointers(&pointers);
            m_report.pointerReports = pointers.GetSize();
            Need(pointerStatus == SM_SUCCESS && pointers.GetSize() == 0, "NativePointerValidationFailed", m_plan.record);
            Observe("PointersChecked");
            SmAssertArray deep;
            Step(m_plan.record);
            const bool valid = m_model->brep->AssertValid(&deep, SM_LEVEL_2, SM_WALK) != FALSE;
            m_report.deepReports = deep.GetSize();
            Need(valid && deep.GetSize() == 0, "NativeDeepValidationFailed", m_plan.record);
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
    Execution& m_execution;
    std::map<std::string, double> initialTolerances;
    std::string Id(std::size_t record) const
    {
        return m_plan.source->Source().Records().at(record).id;
    }
    void BuildFace(SmBrepConstructor& ctor, const FacePlan& facePlan, NativeFaceData& native)
    {
        const auto tolerance = Value(m_options.quality.initialWorkToleranceMm);
        native.faceId = Id(facePlan.face);
        Step(facePlan.face);
        native.face = ctor.StartFace(facePlan.faceSameSense ? SM_OT_SAME : SM_OT_OPPOSITE);
        Need(native.face != nullptr, "NativeFaceConstructionFailed", facePlan.face);
        native.face->SetTolerance(tolerance, FALSE);
        const auto x = Unit(facePlan.x, facePlan.xNormSquared), y = Unit(facePlan.y, facePlan.yNormSquared), origin = Enclose(facePlan.originMm);
        std::array<double, 4> window{ std::numeric_limits<double>::infinity(),
                                      std::numeric_limits<double>::infinity(),
                                      -std::numeric_limits<double>::infinity(),
                                      -std::numeric_limits<double>::infinity() };
        for (const auto index : facePlan.edges)
        {
            const auto& edge = m_plan.edges[index];
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
        const auto xValue = Value(facePlan.x), yValue = Value(facePlan.y);
        const double nx = std::sqrt(Value(facePlan.xNormSquared)), ny = std::sqrt(Value(facePlan.yNormSquared));
        const SmExtent2d domain(window[0], window[1], window[2], window[3]);
        Step(facePlan.plane);
        std::unique_ptr<SmPlane> plane(new (*m_model->context) SmPlane(
            Native(Value(facePlan.originMm)),
            SmVector3d(xValue[0] / nx, xValue[1] / nx, xValue[2] / nx),
            SmVector3d(yValue[0] / ny, yValue[1] / ny, yValue[2] / ny),
            SmVector2d(1, 1),
            domain,
            m_model->context.get()
        ));
        Step(facePlan.face);
        const auto surfaceStatus = ctor.SetFaceSurface(plane.get(), domain);
        if (plane->GetOwner() == native.face)
        {
            native.plane = plane.release();
        }
        Good(surfaceStatus);
        Need(native.plane != nullptr, "NativeSurfaceOwnershipMismatch", facePlan.face);
        for (const auto& loop : facePlan.loops)
        {
            Step(loop.bound);
            auto targetLoop = ctor.StartLoop(SM_OT_SAME);
            Need(targetLoop != nullptr, "NativeLoopConstructionFailed", loop.bound);
            native.loops.push_back(targetLoop);
            for (const auto& use : loop.primaryUses)
            {
                const auto& edge = m_plan.edges.at(use.edge);
                Step(edge.record);
                const auto existing = m_model->edges[use.edge];
                auto targetEdge = ctor.StartEdge(
                    use.forward ? SM_OT_SAME : SM_OT_OPPOSITE,
                    existing,
                    m_model->vertices.at(edge.minimumVertex),
                    m_model->vertices.at(edge.maximumVertex)
                );
                Need(targetEdge != nullptr, "NativeEdgeConstructionFailed", edge.record);
                if (!existing)
                {
                    targetEdge->SetTolerance(tolerance, FALSE);
                    m_model->edges[use.edge] = targetEdge;
                    Step(edge.record);
                    SmLine* raw = nullptr;
                    const auto
                        created = SmLine::CreateLineSegment(*m_model->context, 3, Native(Value(edge.minimumMm)), Native(Value(edge.maximumMm)), raw);
                    std::unique_ptr<SmLine> curve(raw);
                    Good(created);
                    Need(bool(curve), "NativeCurveAllocationFailed", edge.record);
                    Step(edge.record);
                    const auto adopted = ctor.SetEdgeCurve(curve.get(), FALSE);
                    if (curve->GetOwner() == targetEdge)
                    {
                        m_model->curves[use.edge] = curve.release();
                    }
                    Good(adopted);
                    Need(m_model->curves[use.edge] != nullptr, "NativeCurveOwnershipMismatch", edge.record);
                    Step(edge.record);
                    Good(ctor.SetLimits(SmVertexNode(0.0), SmVertexNode(1.0)));
                }
                else
                {
                    Need(targetEdge == existing, "NativeSharedEdgeNotReused", edge.record);
                }
                Step(edge.record);
                Good(ctor.EndEdge());
            }
            Step(loop.bound);
            Good(ctor.EndLoop());
        }
        Step(facePlan.face);
        Good(ctor.EndFace());
    }
    void Step(std::size_t record)
    {
        m_execution.Check(Phase::BeforeNativeCall);
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
                m_plan.record
            );
            const auto key = std::string(kind) + ":" + id;
            const auto [found, added] = initialTolerances.emplace(key, value);
            Need(!added || value == Value(m_options.quality.initialWorkToleranceMm), "NativeInitialToleranceMismatch", m_plan.record);
            const auto growth = value == found->second ? 0.0 : (Exact(value) - Exact(found->second)).high;
            Need(growth <= Enclose(m_options.quality.toleranceGrowthMm).low, "NativeToleranceGrowth", m_plan.record);
        };
        observe("Brep", Id(m_plan.record), m_model->brep.get());
        for (std::size_t i = 0; i < m_plan.faces.size(); ++i)
        {
            observe("Face", Id(m_plan.faces[i].face), m_model->faces[i].face);
        }
        for (std::size_t i = 0; i < m_model->vertices.size(); ++i)
        {
            observe("Vertex", Id(m_plan.vertices[i].record), m_model->vertices[i]);
        }
        for (std::size_t i = 0; i < m_model->edges.size(); ++i)
        {
            observe("Edge", Id(m_plan.edges[i].record), m_model->edges[i]);
        }
    }
    Point Uv(const NativeFaceData& native, const Point& point) const
    {
        const auto& position = native.plane->GetPosition();
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
    SmEdgeuse* SameUse(std::size_t edge, const NativeFaceData& native) const
    {
        SmTArray<SmEdgeuse*> uses;
        Good(m_model->edges[edge]->GetEdgeuses(uses));
        SmEdgeuse* result = nullptr;
        for (ULONG i = 0; i < uses.GetSize(); ++i)
        {
            const auto use = uses[i];
            if (use->GetLoopuse()->GetFaceuse()->GetFace() == native.face && use->GetOrientation() == SM_OT_SAME)
            {
                Need(result == nullptr, "NativeRepeatedSameFaceOccurrence", m_plan.edges[edge].record);
                result = use;
            }
        }
        Need(result != nullptr, "NativeFaceOccurrenceMissing", m_plan.edges[edge].record);
        return result;
    }
    void InstallUv(const FacePlan& facePlan, NativeFaceData& native)
    {
        for (const auto i : facePlan.edges)
        {
            const auto same = SameUse(i, native);
            SmTArray<SmPoint3d> points;
            SmTArray<double> weights;
            Good(m_model->curves[i]->GetControlPolygon(points, weights));
            Need(points.GetSize() == 2, "NativeCurveControlShape", m_plan.edges[i].record);
            Step(m_plan.edges[i].record);
            SmLine* raw = nullptr;
            const auto status = SmLine::CreateLineSegment(
                *m_model->context,
                2,
                Native(Uv(native, PointOf(points[0]))),
                Native(Uv(native, PointOf(points[1]))),
                raw
            );
            std::unique_ptr<SmLine> uv(raw);
            Good(status);
            Need(bool(uv), "NativeUvAllocationFailed", m_plan.edges[i].record);
            const auto plane = PlaneCoefficients(*native.plane, false);
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
    std::size_t VerifyTraversal(SmLoopuse* loopuse, const std::vector<UsePlan>& expected, const LoopPlan& loop)
    {
        Need(loopuse->GetOrientation() == (loop.outer ? SM_OT_SAME : SM_OT_OPPOSITE), "NativeLoopRoleMismatch", loop.bound);
        SmTArray<SmEdgeuse*> uses;
        loopuse->GetEdgeuses(uses);
        Need(uses.GetSize() == expected.size(), "NativeLoopUseCount", m_plan.record);
        std::optional<std::size_t> matchedOffset;
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
            if (matches)
            {
                matchedOffset = offset;
                break;
            }
        }
        Need(matchedOffset.has_value(), "NativeDirectedSourcePathMismatch", m_plan.record);
        return *matchedOffset;
    }
    LoopMapping CaptureLoop(const FacePlan& facePlan, const LoopPlan& plan, SmLoopuse* selected, std::size_t offset)
    {
        LoopMapping result{ Id(facePlan.face), Id(plan.bound), Id(plan.loop), plan.outer, plan.sourceOrientation, {} };
        SmTArray<SmEdgeuse*> uses;
        selected->GetEdgeuses(uses);
        for (ULONG i = 0; i < uses.GetSize(); ++i)
        {
            const auto use = uses[i];
            const auto& wanted = plan.selectedUses[(offset + i) % plan.selectedUses.size()];
            const auto& edge = m_plan.edges[wanted.edge];
            SmEdgeuse* owner = nullptr;
            const auto uv = use->GetUVTrimCurve(&owner);
            Need(
                uv && owner && owner->GetOrientation() == SM_OT_SAME && owner->GetEdge() == use->GetEdge() && uv->GetOwner() == owner &&
                    uv->GetContext() == m_model->context.get(),
                "NativeUseUvOwnerMismatch",
                wanted.orientedEdge
            );
            SmTArray<SmPoint3d> points;
            SmTArray<double> weights;
            Good(uv->GetControlPolygon(points, weights));
            Need(points.GetSize() == 2, "NativeUseUvControlShape", wanted.orientedEdge);
            const bool forward = use->GetOrientation() == SM_OT_SAME;
            const auto start = points[forward ? 0 : 1], end = points[forward ? 1 : 0];
            result.uses.push_back(
                { Id(wanted.orientedEdge),
                  Id(edge.record),
                  Id(m_plan.vertices[wanted.forward ? edge.minimumVertex : edge.maximumVertex].record),
                  Id(m_plan.vertices[wanted.forward ? edge.maximumVertex : edge.minimumVertex].record),
                  forward,
                  owner->GetLoopuse()->GetFaceuse() == selected->GetFaceuse(),
                  { start.x, start.y },
                  { end.x, end.y } }
            );
        }
        return result;
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
            faces.GetSize() == m_plan.faces.size() && edges.GetSize() == m_plan.edges.size() && vertices.GetSize() == m_plan.vertices.size() &&
                (m_model->brep->IsManifoldSolid() == (m_plan.closedBoundary ? TRUE : FALSE)),
            "NativeShapeRoleOrIdentityMismatch",
            m_plan.record
        );
        std::set<SmFace*> actualFaces, expectedFaces;
        for (ULONG i = 0; i < faces.GetSize(); ++i)
        {
            actualFaces.insert(faces[i]);
        }
        for (const auto& face : m_model->faces)
        {
            expectedFaces.insert(face.face);
        }
        Need(actualFaces == expectedFaces, "NativeFaceIdentityMismatch", m_plan.record);
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
            m_plan.record
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
        VerifyAdjacency();
        for (std::size_t i = 0; i < m_plan.faces.size(); ++i)
        {
            VerifyFace(m_plan.faces[i], m_model->faces[i]);
        }
    }
    void VerifyFace(const FacePlan& facePlan, const NativeFaceData& native)
    {
        Need(
            native.face->GetSurface() == native.plane && native.plane->GetOwner() == native.face &&
                native.face->GetContext() == m_model->context.get(),
            "NativeSurfaceOwnershipMismatch",
            facePlan.face
        );
        SmFaceuse* same = nullptr;
        SmFaceuse* opposite = nullptr;
        native.face->GetFaceuses(same, opposite);
        Need(
            same && opposite && same->GetOrientation() == SM_OT_SAME && opposite->GetOrientation() == SM_OT_OPPOSITE,
            "NativeFaceSideMismatch",
            facePlan.face
        );
        for (auto side : { same, opposite })
        {
            SmTArray<SmLoopuse*> loops;
            side->GetLoopuses(loops);
            Need(
                loops.GetSize() == facePlan.loops.size() && loops[0]->GetLoop() == native.loops[0],
                "NativeLoopMultiplicityOrOuterIdentity",
                m_plan.record
            );
            std::set<SmLoop*> actual;
            for (ULONG i = 0; i < loops.GetSize(); ++i)
            {
                actual.insert(loops[i]->GetLoop());
            }
            Need(actual == std::set<SmLoop*>(native.loops.begin(), native.loops.end()), "NativeLoopIdentityMismatch", facePlan.face);
        }
        for (std::size_t i = 0; i < facePlan.loops.size(); ++i)
        {
            const auto primary = native.loops[i]->GetLoopuse();
            const auto mate = primary->GetOtherLoopuse();
            Need(
                primary->GetFaceuse() == same && mate->GetFaceuse() == opposite && mate->GetOtherLoopuse() == primary,
                "NativeLoopSideMismatch",
                facePlan.loops[i].bound
            );
            VerifyTraversal(primary, facePlan.loops[i].primaryUses, facePlan.loops[i]);
            const auto selected = facePlan.faceSameSense ? primary : mate;
            const auto offset = VerifyTraversal(selected, facePlan.loops[i].selectedUses, facePlan.loops[i]);
            m_report.loops.push_back(CaptureLoop(facePlan, facePlan.loops[i], selected, offset));
        }
        double definition = 0, consistency = 0, uvError = 0;
        const auto domain = native.plane->GetNaturalUVDomain();
        const auto faceDomain = native.face->GetUVDomain();
        Need(
            domain.GetMin().x == faceDomain.GetMin().x && domain.GetMin().y == faceDomain.GetMin().y && domain.GetMax().x == faceDomain.GetMax().x &&
                domain.GetMax().y == faceDomain.GetMax().y,
            "NativeFaceDomainMismatch",
            facePlan.face
        );
        for (bool polygon : { false, true })
        {
            const auto nativePlane = PlaneCoefficients(*native.plane, polygon);
            for (const auto vertexIndex : facePlan.vertices)
            {
                const auto point = PointOf(m_model->vertices[vertexIndex]->GetPoint());
                const Affine constantUv{ Exact(Uv(native, point)), Exact(Point{}) };
                const Affine constantPoint{ Exact(point), Exact(Point{}) };
                consistency = std::max(consistency, LiftBound(nativePlane, constantUv, constantPoint));
            }
            const auto sourceOrigin = Enclose(facePlan.originMm), sourceX = Unit(facePlan.x, facePlan.xNormSquared),
                       sourceY = Unit(facePlan.y, facePlan.yNormSquared);
            for (double u : { domain.GetMin().x, domain.GetMax().x })
            {
                for (double v : { domain.GetMin().y, domain.GetMax().y })
                {
                    const auto jacobian = Cross(
                        Add(nativePlane.u, Times(nativePlane.uv, Exact(v))),
                        Add(nativePlane.v, Times(nativePlane.uv, Exact(u)))
                    );
                    if (!(Dot(jacobian, Cross(sourceX, sourceY)).low > SM_EFF_ZERO * NormUpper(Cross(sourceX, sourceY))))
                    {
                        throw Failure{ Status::Indeterminate,
                                       "NativeSurfaceJacobianUnestablished",
                                       "Actual analytic/NURBS surface orientation is not proved positive over the finite window",
                                       facePlan.face };
                    }
                    const auto exact = Add(Add(sourceOrigin, Times(sourceX, Exact(u))), Times(sourceY, Exact(v)));
                    definition = std::max(definition, NormUpper(Subtract(Evaluate(nativePlane, Exact(u), Exact(v)), exact)));
                }
            }
            for (const auto i : facePlan.edges)
            {
                const auto sameUse = SameUse(i, native);
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
                                facePlan.face
                            );
                        }
                    }
                }
            }
        }
        for (const auto i : facePlan.vertices)
        {
            definition = std::max(
                definition,
                NormUpper(Subtract(Exact(PointOf(m_model->vertices[i]->GetPoint())), Enclose(m_plan.vertices[i].pointMm)))
            );
        }
        m_report.definitionBoundMm = std::max(m_report.definitionBoundMm.value_or(0), definition);
        m_report.consistencyBoundMm = std::max(m_report.consistencyBoundMm.value_or(0), std::max(consistency, uvError)); // Whole-edge lift also
                                                                                                                         // bounds edge/plane
                                                                                                                         // consistency.
        m_report.uvBoundMm = std::max(m_report.uvBoundMm.value_or(0), uvError);
        Certify(definition, m_options.quality.definitionMm, "NativeDefinitionErrorBudget", facePlan.face);
        Certify(*m_report.consistencyBoundMm, m_options.quality.consistencyMm, "NativeConsistencyErrorBudget", facePlan.face);
        Certify(uvError, m_options.quality.derivedUvMm, "NativeUvErrorBudget", facePlan.face);
    }
    void MeasureFace(const FacePlan& facePlan, NativeFaceData& native)
    {
        Statistics info;
        info.faces = 1;
        info.edges = facePlan.edges.size();
        info.vertices = facePlan.vertices.size();
        info.shapeRole = "FaceSurface";
        Step(facePlan.face);
        SmPoint3d centroid;
        Good(SmApiFaceComputeCentroid(native.face, 1e-4, info.areaMm2, centroid));
        info.centroidMm = PointOf(centroid);
        for (const auto i : facePlan.edges)
        {
            double length = 0;
            Step(m_plan.edges[i].record);
            Good(SmApiEdgeComputeLength(m_model->edges[i], Value(m_options.quality.queryMm), length));
            info.boundaryMm += length;
        }
        SmPoint3d minimum, maximum;
        Step(facePlan.face);
        Good(SmApiFaceBoundingBox(native.face, TRUE, minimum, maximum));
        info.boundsMm = { PointOf(minimum), PointOf(maximum) };
        info.normal = PointOf(native.plane->GetPosition().GetZAxis());
        if (!facePlan.faceSameSense)
        {
            for (auto& value : *info.normal)
            {
                value = -value;
            }
        }
        const auto domain = native.plane->GetNaturalUVDomain();
        info.windowMm = std::array<double, 4>{ domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y };
        const auto expectedArea = SqrtNonnegative(Enclose(facePlan.sourceAreaSquared));
        const auto error = Exact(info.areaMm2) - expectedArea;
        Certify(std::max(std::abs(error.low), std::abs(error.high)), m_options.quality.areaMm2, "NativeAreaQueryMismatch", facePlan.face);
        const auto checkVector = [&](const Point& actual, const IntervalPoint& expected, const char* key)
        {
            Certify(NormUpper(detail::Subtract(Exact(actual), expected)), m_options.quality.queryMm, key, facePlan.face);
        };
        checkVector(info.centroidMm, Enclose(facePlan.sourceCentroid), "NativeCentroidQueryMismatch");
        checkVector(info.boundsMm[0], Enclose(facePlan.sourceMinimum), "NativeBoundsQueryMismatch");
        checkVector(info.boundsMm[1], Enclose(facePlan.sourceMaximum), "NativeBoundsQueryMismatch");
        auto expectedNormal = Cross(Unit(facePlan.x, facePlan.xNormSquared), Unit(facePlan.y, facePlan.yNormSquared));
        if (!facePlan.faceSameSense)
        {
            expectedNormal = Times(expectedNormal, Exact(-1));
        }
        checkVector(*info.normal, expectedNormal, "NativeNormalMismatch");
        auto expectedLength = Exact(0);
        for (const auto index : facePlan.edges)
        {
            const auto& edge = m_plan.edges[index];
            expectedLength = expectedLength + SqrtNonnegative(Enclose(edge.sourceLengthSquared));
        }
        const auto lengthError = Exact(info.boundaryMm) - expectedLength;
        Certify(
            std::max(std::abs(lengthError.low), std::abs(lengthError.high)),
            m_options.quality.queryMm,
            "NativeLengthQueryMismatch",
            facePlan.face
        );
        native.info = info;
        for (const auto index : facePlan.edges)
        {
            if (m_model->edges[index]->IsLamina())
            {
                ++native.info.freeBoundaryEdges;
            }
        }
        m_report.faces.push_back({ Id(facePlan.face), Id(facePlan.plane), facePlan.faceSameSense, native.info });
    }
    void VerifyAdjacency();
    void Measure();
    Interval BoundaryVolume();
};
namespace
{
Interval Hull(Interval a, Interval b)
{
    return { std::min(a.low, b.low), std::max(a.high, b.high) };
}
IntervalPoint Hull(IntervalPoint a, IntervalPoint b)
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        a[i] = Hull(a[i], b[i]);
    }
    return a;
}
double AbsoluteBound(Interval value)
{
    return std::max(std::abs(value.low), std::abs(value.high));
}
struct OwnedBoundary
{
    SmTArray<SmBSplineCurve*> curves;
    ~OwnedBoundary()
    {
        for (ULONG i = 0; i < curves.GetSize(); ++i)
        {
            delete curves[i];
        }
    }
};
Affine CurveEnvelope(const SmLine& line)
{
    const auto analytic = LineCoefficients(line, false), polygon = LineCoefficients(line, true);
    return { Hull(analytic.origin, polygon.origin), Hull(analytic.delta, polygon.delta) };
}
SurfaceCoefficients SurfaceEnvelope(const SmPlane& plane)
{
    const auto a = PlaneCoefficients(plane, false), b = PlaneCoefficients(plane, true);
    return { Hull(a.origin, b.origin), Hull(a.u, b.u), Hull(a.v, b.v), Hull(a.uv, b.uv) };
}
using FluxCoefficients = std::array<std::array<Interval, 3>, 3>;
FluxCoefficients ZFlux(const SurfaceCoefficients& surface, const IntervalPoint& reference)
{
    const auto o = Subtract(surface.origin, reference);
    const auto n = Cross(surface.u, surface.v), nu = Cross(surface.u, surface.uv), nv = Cross(surface.uv, surface.v);
    FluxCoefficients c{};
    c[0][0] = o[2] * n[2];
    c[1][0] = surface.u[2] * n[2] + o[2] * nu[2];
    c[0][1] = surface.v[2] * n[2] + o[2] * nv[2];
    c[1][1] = surface.uv[2] * n[2] + surface.u[2] * nv[2] + surface.v[2] * nu[2];
    c[2][0] = surface.u[2] * nu[2];
    c[0][2] = surface.v[2] * nv[2];
    c[2][1] = surface.uv[2] * nu[2];
    c[1][2] = surface.uv[2] * nv[2];
    return c;
}
Interval SegmentFlux(const FluxCoefficients& c, const Affine& line, double minimumV)
{
    // Native gauge: P(u,v)=-integral(vmin..v) (S_z-c_z)*(Su x Sv)_z dw, integrated as P du.
    // Affine UV and bilinear surface imply degree <=4 in t. Three-point Gauss-Legendre is exact
    // through degree 5; interval algebraic nodes/weights enclose its roundoff. This is exact
    // polynomial integration, not a sampling-based error estimate.
    const auto root = SqrtNonnegative(Exact(3) / Exact(5));
    const std::array<Interval, 3> nodes = { (Exact(1) - root) / Exact(2), Exact(.5), (Exact(1) + root) / Exact(2) };
    const std::array<Interval, 3> weights = { Exact(5) / Exact(18), Exact(4) / Exact(9), Exact(5) / Exact(18) };
    std::array<Interval, 4> base{ Exact(1), Exact(minimumV), Exact(0), Exact(0) };
    base[2] = base[1] * base[1];
    base[3] = base[2] * base[1];
    Interval result = Exact(0);
    for (std::size_t k = 0; k < 3; ++k)
    {
        const auto u = line.origin[0] + nodes[k] * line.delta[0], v = line.origin[1] + nodes[k] * line.delta[1];
        const std::array<Interval, 3> up{ Exact(1), u, u * u };
        const std::array<Interval, 4> vp{ Exact(1), v, v * v, v * v * v };
        Interval primitive = Exact(0);
        for (std::size_t i = 0; i < 3; ++i)
        {
            for (std::size_t j = 0; j < 3; ++j)
            {
                if (i + j <= 3)
                {
                    primitive = primitive - c[i][j] * up[i] * (vp[j + 1] - base[j + 1]) / Exact(static_cast<double>(j + 1));
                }
            }
        }
        result = result + weights[k] * primitive * line.delta[0];
    }
    return result;
}
} // namespace
Interval Builder::BoundaryVolume()
{
    Interval closedVolume = Exact(0);
    double gapFlux = 0, skippedFlux = 0;
    for (std::size_t f = 0; f < m_plan.faces.size(); ++f)
    {
        const auto& native = m_model->faces[f];
        const auto& plan = m_plan.faces[f];
        std::vector<std::pair<SmLine*, Affine>> originals;
        for (const auto e : plan.edges)
        {
            const auto line = dynamic_cast<SmLine*>(SameUse(e, native)->GetUVTrimCurvePointer());
            Need(line != nullptr, "NativeMassBoundaryUvMissing", plan.face);
            originals.push_back({ line, CurveEnvelope(*line) });
        }
        OwnedBoundary boundary;
        SmTArray<ULONG> counts;
        Step(plan.face);
        Good(native.face->CreateOrientedTrimBoundary(*m_model->context, TRUE, TRUE, counts, boundary.curves));
        Need(counts.GetSize() == plan.loops.size(), "NativeMassBoundaryLoopMismatch", plan.face);
        for (std::size_t e = 0; e < plan.edges.size(); ++e)
        {
            const auto line = dynamic_cast<SmLine*>(SameUse(plan.edges[e], native)->GetUVTrimCurvePointer());
            Need(line == originals[e].first, "NativeMassBoundaryMutatedUvOwner", plan.face);
            const auto now = CurveEnvelope(*line);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                Need(
                    now.origin[axis].low == originals[e].second.origin[axis].low && now.origin[axis].high == originals[e].second.origin[axis].high &&
                        now.delta[axis].low == originals[e].second.delta[axis].low && now.delta[axis].high == originals[e].second.delta[axis].high,
                    "NativeMassBoundaryMutatedUvGeometry",
                    plan.face
                );
            }
        }
        const auto surface = SurfaceEnvelope(*native.plane);
        const auto flux = ZFlux(surface, Exact(Value(m_plan.box->center)));
        const auto domain = native.plane->GetNaturalUVDomain();
        const auto sign = Exact(plan.faceSameSense ? 1.0 : -1.0);
        std::size_t offset = 0;
        for (ULONG loop = 0; loop < counts.GetSize(); ++loop)
        {
            std::vector<Affine> segments;
            Need(counts[loop] > 0 && counts[loop] <= boundary.curves.GetSize() - offset, "NativeMassBoundaryCurveCount", plan.face);
            for (ULONG i = 0; i < counts[loop]; ++i)
            {
                const auto line = dynamic_cast<const SmLine*>(boundary.curves[static_cast<ULONG>(offset + i)]);
                Need(line != nullptr, "NativeMassBoundaryNotAffine", plan.face);
                segments.push_back(CurveEnvelope(*line));
            }
            for (std::size_t i = 0; i < segments.size(); ++i)
            {
                const auto& segment = segments[i];
                const auto value = SegmentFlux(flux, segment, domain.GetMin().y);
                closedVolume = closedVolume + sign * value;
                if (segment.delta[0].low <= SM_EFF_ZERO && segment.delta[0].high >= -SM_EFF_ZERO)
                {
                    skippedFlux = Up(skippedFlux + AbsoluteBound(value));
                }
                const auto end = Add(segment.origin, segment.delta), next = segments[(i + 1) % segments.size()].origin;
                const Affine gap{ end, Subtract(next, end) };
                const auto correction = SegmentFlux(flux, gap, domain.GetMin().y);
                closedVolume = closedVolume + sign * correction;
                gapFlux = Up(gapFlux + AbsoluteBound(correction));
                const auto physical = NormUpper(Subtract(Evaluate(surface, end[0], end[1]), Evaluate(surface, next[0], next[1])));
                m_report.volumeJunctionBoundMm = std::max(m_report.volumeJunctionBoundMm.value_or(0), physical);
                Certify(physical, m_options.quality.consistencyMm, "NativeVolumeGapBudget", plan.face);
                for (const auto& endpoint : { segment.origin, end })
                {
                    if (endpoint[0].low < domain.GetMin().x || endpoint[0].high > domain.GetMax().x || endpoint[1].low < domain.GetMin().y ||
                        endpoint[1].high > domain.GetMax().y)
                    {
                        throw Failure{ Status::Indeterminate,
                                       "NativeMassBoundaryOutsideWindow",
                                       "Actual oriented finite trim exceeds verified window",
                                       plan.face };
                    }
                }
            }
            offset += counts[loop];
        }
        Need(offset == boundary.curves.GetSize(), "NativeMassBoundaryExtraCurves", plan.face);
    }
    m_report.volumeGapFluxBoundMm3 = gapFlux;
    m_report.volumeSkippedFluxBoundMm3 = skippedFlux;
    const auto omissions = Up(gapFlux + skippedFlux);
    return closedVolume + Interval{ -omissions, omissions };
}
void Builder::VerifyAdjacency()
{
    SmTArray<SmRegion*> regions;
    m_model->brep->GetRegions(regions);
    Need(regions.GetSize() == (m_plan.closedBoundary ? 2U : 1U), "NativeRegionMultiplicity", m_plan.record);
    std::set<SmRegion*> actualRegions;
    for (ULONG i = 0; i < regions.GetSize(); ++i)
    {
        actualRegions.insert(regions[i]);
    }
    std::set<SmRegion*> expectedRegions{ m_model->brep->GetInfiniteRegion() };
    if (m_plan.closedBoundary)
    {
        expectedRegions.insert(m_model->bounded);
    }
    Need(actualRegions == expectedRegions, "NativeRegionIdentityMismatch", m_plan.record);
    SmTArray<SmShell*> shells;
    m_model->brep->GetShells(shells);
    std::set<SmShell*> actualShells;
    for (ULONG i = 0; i < shells.GetSize(); ++i)
    {
        actualShells.insert(shells[i]);
    }
    std::set<SmShell*> expectedShells{ m_model->outside };
    if (m_plan.closedBoundary)
    {
        expectedShells.insert(m_model->inside);
    }
    Need(actualShells == expectedShells && shells.GetSize() == expectedShells.size(), "NativeShellIdentityMismatch", m_plan.record);
    NativeRoleObservation role;
    role.regions = regions.GetSize();
    role.shells = shells.GetSize();
    role.manifoldSolidPredicate = m_model->brep->IsManifoldSolid() != FALSE;
    for (ULONG i = 0; i < regions.GetSize(); ++i)
    {
        if (!regions[i]->IsVoid())
        {
            ++role.materialRegions;
        }
        else if (regions[i] != m_model->brep->GetInfiniteRegion())
        {
            ++role.boundedVoidRegions;
        }
    }
    Need(
        role.materialRegions == (m_plan.materialSolid ? 1U : 0U) &&
            role.boundedVoidRegions == (m_plan.closedBoundary && !m_plan.materialSolid ? 1U : 0U) && bool(m_model->material) == m_plan.materialSolid,
        "NativeMaterialRoleMismatch",
        m_plan.record
    );
    Need(
        m_model->outside->GetRegion() == m_model->brep->GetInfiniteRegion() && m_model->brep->GetInfiniteRegion()->IsVoid() == TRUE,
        "NativeInfiniteRegionRole",
        m_plan.record
    );
    if (m_plan.closedBoundary)
    {
        Need(
            m_model->inside && m_model->inside->GetRegion() == m_model->bounded &&
                m_model->bounded->IsVoid() == (m_plan.materialSolid ? FALSE : TRUE) && m_model->inside->IsClosed() == TRUE &&
                m_model->outside->IsClosed() == TRUE,
            m_plan.materialSolid ? "NativeMaterialShellRole" : "NativeBoundedShellRole",
            m_plan.record
        );
        SmTArray<SmShell*> boundedShells;
        m_model->bounded->GetShells(boundedShells);
        Need(
            boundedShells.GetSize() == 1 && boundedShells[0] == m_model->inside,
            m_plan.materialSolid ? "NativeMaterialOuterShellIdentity" : "NativeBoundedOuterShellIdentity",
            m_plan.record
        );
    }
    if (m_plan.materialSolid)
    {
        long solids = 0, voids = 0;
        Step(m_plan.record);
        Good(SmApiBrepMaterialCensus(m_model->brep.get(), solids, voids));
        Need(solids == 1 && voids == 0, "NativeMaterialCensusMismatch", m_plan.record);
        m_report.solidRegions = static_cast<std::size_t>(solids);
        m_report.voidCavities = static_cast<std::size_t>(voids);
    }
    for (std::size_t f = 0; f < m_plan.faces.size(); ++f)
    {
        SmFaceuse* same = nullptr;
        SmFaceuse* opposite = nullptr;
        m_model->faces[f].face->GetFaceuses(same, opposite);
        const auto selected = m_plan.faces[f].faceSameSense ? same : opposite;
        Need(
            selected->GetShell() == m_model->outside &&
                selected->GetMate()->GetShell() == (m_plan.closedBoundary ? m_model->inside : m_model->outside),
            "NativeOutwardSideMismatch",
            m_plan.faces[f].face
        );
    }
    std::vector<std::vector<std::size_t>> incidentFaces(m_plan.edges.size());
    for (std::size_t f = 0; f < m_plan.faces.size(); ++f)
    {
        for (const auto e : m_plan.faces[f].edges)
        {
            incidentFaces.at(e).push_back(f);
        }
    }
    for (std::size_t e = 0; e < m_plan.edges.size(); ++e)
    {
        std::set<SmFace*> expected, actual;
        EdgeAdjacency row;
        row.edgeId = Id(m_plan.edges[e].record);
        for (const auto f : incidentFaces[e])
        {
            expected.insert(m_model->faces[f].face);
            row.faceIds.push_back(Id(m_plan.faces[f].face));
        }
        SmTArray<SmEdgeuse*> uses;
        Good(m_model->edges[e]->GetEdgeuses(uses));
        const bool shared = m_plan.closedBoundary || (m_plan.openSheet && m_plan.openSheet->sharedEdge == e);
        Need(bool(m_model->edges[e]->IsLamina()) == !shared, "NativeSourceFreeEdgeMismatch", m_plan.edges[e].record);
        Need(expected.size() == (shared ? 2U : 1U) && uses.GetSize() == 2 * expected.size(), "NativeAdjacencyMultiplicity", m_plan.edges[e].record);
        for (ULONG i = 0; i < uses.GetSize(); ++i)
        {
            const auto use = uses[i];
            actual.insert(use->GetLoopuse()->GetFaceuse()->GetFace());
            Need(
                use->GetEdge() == m_model->edges[e] && use->GetMate()->GetMate() == use && use->GetRadial()->GetRadial() == use &&
                    use->GetRadial()->GetLoopuse()->GetFaceuse()->GetShell() == use->GetLoopuse()->GetFaceuse()->GetShell(),
                "NativeRadialOrMateMismatch",
                m_plan.edges[e].record
            );
            if (shared)
            {
                Need(
                    use->GetRadial()->GetLoopuse()->GetFaceuse()->GetFace() != use->GetLoopuse()->GetFaceuse()->GetFace(),
                    "NativeSharedFaceIdentityMismatch",
                    m_plan.edges[e].record
                );
            }
        }
        Need(actual == expected, "NativeSourceAdjacencyMismatch", m_plan.edges[e].record);
        row.nativeUses = uses.GetSize();
        m_report.adjacency.push_back(std::move(row));
    }
    m_report.nativeRole = role;
}
void Builder::Measure()
{
    for (std::size_t i = 0; i < m_plan.faces.size(); ++i)
    {
        MeasureFace(m_plan.faces[i], m_model->faces[i]);
    }
    Statistics info;
    if (m_plan.faces.size() == 1)
    {
        info = m_model->faces[0].info;
    }
    else
    {
        info.faces = m_model->faces.size();
        info.edges = m_model->edges.size();
        info.vertices = m_model->vertices.size();
        if (m_plan.materialSolid)
        {
            SmPoint3d centroid;
            SmVector3d moments, products;
            double volume = 0, mass = 0;
            Step(m_plan.record);
            Good(SmApiBrepComputeMassProperties(
                m_model->brep.get(),
                1e-4,
                1.0,
                Native(Value(m_plan.box->center)),
                info.areaMm2,
                volume,
                mass,
                centroid,
                moments,
                products
            ));
            info.materialVolumeMm3 = volume;
            info.centroidMm = PointOf(centroid);
            const auto directVolumeError = Exact(volume) - Enclose(m_plan.box->volume);
            double volumeBound = std::max(std::abs(directVolumeError.low), std::abs(directVolumeError.high));
            const auto integral = BoundaryVolume();
            if (!(integral.low > 0))
            {
                throw Failure{ Status::Indeterminate,
                               "NativeVolumeSignUnestablished",
                               "Actual finite boundary flux is not proved positive",
                               m_plan.record };
            }
            for (const auto error : { integral - Enclose(m_plan.box->volume), Exact(volume) - integral })
            {
                volumeBound = std::max({ volumeBound, std::abs(error.low), std::abs(error.high) });
            }
            m_report.volumeBoundMm3 = volumeBound;
            Certify(volumeBound, m_options.quality.volumeMm3, "NativeBoundaryVolumeBudget", m_plan.record);
            Certify(
                std::max(std::abs((Exact(volume) - Enclose(m_plan.box->volume)).low), std::abs((Exact(volume) - Enclose(m_plan.box->volume)).high)),
                m_options.quality.volumeMm3,
                "NativeVolumeQueryMismatch",
                m_plan.record
            );
            Certify(
                NormUpper(Subtract(Exact(info.centroidMm), Enclose(m_plan.box->center))),
                m_options.quality.queryMm,
                "NativeMassCentroidMismatch",
                m_plan.record
            );
        }
        else
        {
            // Surface properties come from actual face areas, never from material mass/volume APIs.
            for (const auto& face : m_model->faces)
            {
                info.areaMm2 += face.info.areaMm2;
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    info.centroidMm[axis] += face.info.areaMm2 * face.info.centroidMm[axis];
                }
            }
            Need(
                std::isfinite(info.areaMm2) && info.areaMm2 > 0,
                m_plan.closedBoundary ? "NativeClosedSurfaceAreaInvalid" : "NativeOpenSheetAreaInvalid",
                m_plan.record
            );
            for (auto& value : info.centroidMm)
            {
                value /= info.areaMm2;
            }
            if (m_plan.closedBoundary)
            {
                Certify(
                    NormUpper(Subtract(Exact(info.centroidMm), Enclose(m_plan.box->center))),
                    m_options.quality.queryMm,
                    "NativeClosedSurfaceCentroidMismatch",
                    m_plan.record
                );
            }
            else
            {
                auto area = Exact(0);
                IntervalPoint moment{ Exact(0), Exact(0), Exact(0) };
                for (const auto& face : m_plan.faces)
                {
                    const auto weight = SqrtNonnegative(Enclose(face.sourceAreaSquared));
                    area = area + weight;
                    moment = Add(moment, Times(Enclose(face.sourceCentroid), weight));
                }
                Certify(
                    NormUpper(Subtract(Exact(info.centroidMm), Times(moment, Exact(1) / area))),
                    m_options.quality.queryMm,
                    "NativeOpenSheetCentroidMismatch",
                    m_plan.record
                );
            }
        }
        SmPoint3d minimum, maximum;
        Step(m_plan.record);
        Good(SmApiBrepBoundingBox(m_model->brep.get(), TRUE, minimum, maximum));
        info.boundsMm = { PointOf(minimum), PointOf(maximum) };
        const auto& minimumExpected = m_plan.closedBoundary ? m_plan.box->minimum : m_plan.openSheet->minimum;
        const auto& maximumExpected = m_plan.closedBoundary ? m_plan.box->maximum : m_plan.openSheet->maximum;
        Certify(
            NormUpper(Subtract(Exact(info.boundsMm[0]), Enclose(minimumExpected))),
            m_options.quality.queryMm,
            m_plan.materialSolid  ? "NativeSolidBoundsMismatch" :
            m_plan.closedBoundary ? "NativeClosedSurfaceBoundsMismatch" :
                                    "NativeOpenSheetBoundsMismatch",
            m_plan.record
        );
        Certify(
            NormUpper(Subtract(Exact(info.boundsMm[1]), Enclose(maximumExpected))),
            m_options.quality.queryMm,
            m_plan.materialSolid  ? "NativeSolidBoundsMismatch" :
            m_plan.closedBoundary ? "NativeClosedSurfaceBoundsMismatch" :
                                    "NativeOpenSheetBoundsMismatch",
            m_plan.record
        );
        auto expectedArea = Exact(0), expectedLength = Exact(0);
        for (const auto& face : m_plan.faces)
        {
            expectedArea = expectedArea + SqrtNonnegative(Enclose(face.sourceAreaSquared));
        }
        auto areaError = Exact(info.areaMm2) - expectedArea;
        Certify(
            std::max(std::abs(areaError.low), std::abs(areaError.high)),
            m_options.quality.areaMm2,
            m_plan.materialSolid  ? "NativeSolidAreaMismatch" :
            m_plan.closedBoundary ? "NativeClosedSurfaceAreaMismatch" :
                                    "NativeOpenSheetAreaMismatch",
            m_plan.record
        );
        for (std::size_t e = 0; e < m_plan.edges.size(); ++e)
        {
            double length = 0;
            Step(m_plan.edges[e].record);
            Good(SmApiEdgeComputeLength(m_model->edges[e], Value(m_options.quality.queryMm), length));
            info.boundaryMm += length;
            expectedLength = expectedLength + SqrtNonnegative(Enclose(m_plan.edges[e].sourceLengthSquared));
        }
        auto lengthError = Exact(info.boundaryMm) - expectedLength;
        Certify(
            std::max(std::abs(lengthError.low), std::abs(lengthError.high)),
            m_options.quality.queryMm,
            m_plan.materialSolid  ? "NativeSolidEdgeLengthMismatch" :
            m_plan.closedBoundary ? "NativeClosedSurfaceEdgeLengthMismatch" :
                                    "NativeOpenSheetEdgeLengthMismatch",
            m_plan.record
        );
    }
    info.shapeRole = m_plan.source->PartDefinition().shapeRole;
    info.freeBoundaryEdges = 0;
    for (const auto edge : m_model->edges)
    {
        if (edge->IsLamina())
        {
            ++info.freeBoundaryEdges;
        }
    }
    const auto expectedFreeEdges = m_plan.closedBoundary ? 0U : m_plan.edges.size() - (m_plan.openSheet ? 1U : 0U);
    Need(info.freeBoundaryEdges == expectedFreeEdges, "NativeFreeBoundaryMismatch", m_plan.record);
    m_model->info = info;
    m_report.native = info;
}
} // namespace
std::shared_ptr<ModelData> Build(const Plan& plan, const Options& options, Report& report, Execution& execution)
{
    std::lock_guard lock(NativeMutex());
    return Builder(plan, options, report, execution).Run();
}
std::shared_ptr<const ModelSnapshot> Access::Snapshot(std::shared_ptr<ModelData> data)
{
    return std::shared_ptr<const ModelSnapshot>(new ModelSnapshot(std::move(data)));
}
} // namespace step_import::detail

namespace step_import
{
EditableBrep::EditableBrep(std::unique_ptr<detail::EditableData> data) : m_data(std::move(data))
{
}
EditableBrep::~EditableBrep() = default;
void EditableBrep::Edit(const std::function<void(SmBrep&)>& edit)
{
    if (!edit)
    {
        throw std::invalid_argument("A scoped, non-consuming edit callback is required");
    }
    std::lock_guard lock(detail::NativeMutex());
    edit(*m_data->brep);
}
EditableCopyOutcome ModelSnapshot::CopyForModeling() const
{
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard lock(m_data->mutex);
    try
    {
        auto copy = std::make_unique<detail::EditableData>();
        copy->context = std::make_unique<SmContext>();
        copy->brep.reset(new (*copy->context) SmBrep(*m_data->brep, nullptr, nullptr, TRUE, FALSE));
        if (!copy->brep || copy->brep->GetContext() != copy->context.get())
        {
            return { Status::BuildFailure, {}, "EditableCopyContextMismatch", "Copy must own a separate native context" };
        }
        SmAssertArray pointers;
        if (copy->brep->ValidatePointers(&pointers) != SM_SUCCESS || pointers.GetSize() != 0)
        {
            return { Status::PostcheckFailure, {}, "EditableCopyPointerValidation", "Copied topology failed pointer validation" };
        }
        SmAssertArray deep;
        if (!copy->brep->AssertValid(&deep, SM_LEVEL_2, SM_WALK) || deep.GetSize() != 0)
        {
            return { Status::PostcheckFailure, {}, "EditableCopyDeepValidation", "Copied topology failed level-2 validation" };
        }
        return { Status::Accepted, std::unique_ptr<EditableBrep>(new EditableBrep(std::move(copy))), {}, {} };
    }
    catch (const std::bad_alloc&)
    {
        throw;
    }
    catch (const std::exception& error)
    {
        return { Status::BuildFailure, {}, "EditableCopyFailure", error.what() };
    }
    catch (...)
    {
        return { Status::BuildFailure, {}, "EditableCopyFailure", "Unknown native copy failure" };
    }
}
ModelSnapshot::ModelSnapshot(std::shared_ptr<detail::ModelData> data) : m_data(std::move(data))
{
}
Statistics ModelSnapshot::Info() const
{
    return m_data->info;
}
FaceView ModelSnapshot::Face(std::size_t index) const
{
    if (index >= m_data->faces.size())
    {
        throw std::out_of_range("Source face index");
    }
    return FaceView(m_data, index);
}
std::vector<FaceView> ModelSnapshot::Faces() const
{
    std::vector<FaceView> result;
    for (std::size_t i = 0; i < m_data->faces.size(); ++i)
    {
        result.push_back(Face(i));
    }
    return result;
}
std::shared_ptr<const step_semantics::Snapshot> ModelSnapshot::Source() const
{
    return m_data->source;
}
std::optional<double> ModelSnapshot::MaterialVolumeMm3() const
{
    return m_data->info.materialVolumeMm3;
}
void ModelSnapshot::WriteNative(const std::filesystem::path& path) const
{
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard lock(m_data->mutex);
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    // SmBrepData's writer canonicalizes 2D UV storage in place (z=NL_NOZ).
    // Give it a private deep copy so serialization cannot alter snapshot data,
    // including the already measured UV curves. Destroy BRep before its context.
    auto context = std::make_unique<SmContext>();
    std::unique_ptr<SmBrep> archive(new (*context) SmBrep(*m_data->brep, nullptr, nullptr, TRUE, FALSE));
    if (!archive || archive->GetContext() != context.get())
    {
        throw std::runtime_error("Native archive copy context mismatch");
    }
    breps.Add(archive.get());
    // Retain defining analytic/NURBS classes; optional fitting is disabled.
    detail::Good(SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, breps, SM_BINARY, TRUE, FALSE));
}
FaceView::FaceView(std::shared_ptr<detail::ModelData> data, std::size_t face) : m_data(std::move(data)), m_face(face)
{
}
std::string FaceView::SourceId() const
{
    return m_data->faces[m_face].faceId;
}
step_semantics::SchemaInterpretation FaceView::AppliedSourceInterpretation() const
{
    // Immutable semantic ownership survives a dropped import outcome/report.
    // Reading this qualification does not dispatch into the native kernel.
    return m_data->source->AppliedInterpretation();
}
Statistics FaceView::Info() const
{
    return m_data->faces[m_face].info;
}
std::array<double, 4> FaceView::ParameterDomain() const
{
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard lock(m_data->mutex);
    const auto domain = m_data->faces[m_face].face->GetSurface()->GetNaturalUVDomain();
    return { domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y };
}
Point FaceView::EvaluateSurface(double u, double v) const
{
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard lock(m_data->mutex);
    const auto plane = m_data->faces[m_face].face->GetSurface();
    const auto domain = plane->GetNaturalUVDomain();
    if (!std::isfinite(u) || !std::isfinite(v) || u < domain.GetMin().x || u > domain.GetMax().x || v < domain.GetMin().y || v > domain.GetMax().y)
    {
        throw std::out_of_range("Surface query lies outside the verified window");
    }
    SmPoint3d point;
    detail::Good(plane->EvaluatePoint(SmPoint2d(u, v), point));
    return detail::PointOf(point);
}
ClosestPoint FaceView::Closest(const Point& point) const
{
    if (!std::all_of(
            point.begin(),
            point.end(),
            [](double v)
            {
                return std::isfinite(v);
            }
        ))
    {
        throw std::invalid_argument("Finite query required");
    }
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard lock(m_data->mutex);
    SmPoint3d closest;
    double distance = 0;
    detail::Good(SmApiFaceClosestPoint(m_data->faces[m_face].face, detail::Native(point), closest, distance));
    return { detail::PointOf(closest), distance };
}
ClosestPoint ModelSnapshot::Closest(const Point& point) const
{
    if (m_data->faces.size() == 1)
    {
        return Face().Closest(point);
    }
    if (!std::all_of(
            point.begin(),
            point.end(),
            [](double v)
            {
                return std::isfinite(v);
            }
        ))
    {
        throw std::invalid_argument("Finite query required");
    }
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard lock(m_data->mutex);
    SmPoint3d closest;
    double distance = 0;
    detail::Good(SmApiBrepClosestPoint(m_data->brep.get(), detail::Native(point), closest, distance));
    return { detail::PointOf(closest), distance };
}
PointClass FaceView::ClassifyUV(double u, double v) const
{
    if (!std::isfinite(u) || !std::isfinite(v))
    {
        throw std::invalid_argument("Finite query required");
    }
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard lock(m_data->mutex);
    int classification = 0;
    detail::Good(SmApiFaceClassifyUV(m_data->faces[m_face].face, SmPoint2d(u, v), classification));
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
std::optional<MaterialPointClass> ModelSnapshot::ClassifyMaterial(const Point& point) const
{
    if (!m_data->material)
    {
        return {};
    }
    if (!std::all_of(
            point.begin(),
            point.end(),
            [](double v)
            {
                return std::isfinite(v);
            }
        ))
    {
        throw std::invalid_argument("Finite query required");
    }
    std::lock_guard nativeLock(detail::NativeMutex());
    std::lock_guard lock(m_data->mutex);
    SmPointClassification classification(m_data->queryTolerance, m_data->context.get());
    detail::Good(m_data->brep->Point3DClassify(detail::Native(point), m_data->queryTolerance, TRUE, classification));
    const auto kind = classification.GetPointClass();
    if (kind == SM_PC_REGION)
    {
        if (classification.GetObject() == m_data->material)
        {
            return MaterialPointClass::Inside;
        }
        if (classification.GetObject() == m_data->brep->GetInfiniteRegion())
        {
            return MaterialPointClass::Outside;
        }
        return MaterialPointClass::Indeterminate;
    }
    SmPoint3d closest;
    double distance = 0;
    if (kind == SM_PC_FACE)
    {
        detail::Good(SmApiFaceClosestPoint(classification.GetFaceObject(), detail::Native(point), closest, distance));
    }
    else if (kind == SM_PC_EDGE)
    {
        double parameter = 0;
        detail::Good(SmApiEdgeClosestPoint(classification.GetEdgeObject(), detail::Native(point), closest, parameter, distance));
    }
    else if (kind == SM_PC_VERTEX)
    {
        closest = classification.GetVertexObject()->GetPoint();
        const auto delta = detail::Delta(point, detail::PointOf(classification.GetVertexObject()->GetPoint()));
        distance = std::sqrt(detail::DotPoint(delta, delta));
    }
    else
    {
        return MaterialPointClass::Indeterminate;
    }
    const auto bound = detail::NormUpper(detail::Subtract(detail::Exact(point), detail::Exact(detail::PointOf(closest))));
    if (!std::isfinite(distance) || bound > detail::Enclose(m_data->queryBudget).low)
    {
        return MaterialPointClass::Indeterminate;
    }
    return kind == SM_PC_FACE ? MaterialPointClass::Face : kind == SM_PC_EDGE ? MaterialPointClass::Edge : MaterialPointClass::Vertex;
}
} // namespace step_import
