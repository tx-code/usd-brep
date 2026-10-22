// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportNativeData.h"

#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmBrepData.h>
#include <SmNativeArchive.h>
#include <SmPlane.h>
#include <SmSurface.h>
#include <SmVertex.h>
#include <algorithm>
#include <cmath>
#include <limits>
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
    // SRR does not imply containment or union. Store the material BRep first,
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
    std::unique_ptr<SmBrep> body(new (*context) SmBrep(*model.brep, nullptr, nullptr, TRUE, FALSE));
    brepOwners.push_back(std::move(body)); // Own before any vector growth can fail.
    Require(brepOwners.back()->GetContext() == context.get(), "Linked archive BRep copy context mismatch");
    breps.Add(brepOwners.back().get());
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
    }
    for (ULONG i = 0; i < surfaces.GetSize(); ++i)
    {
        Require(read.surfaces[i], "Supporting archive surface missing");
        ComparePlane(*model.supportingSurfaces[i], *read.surfaces[i], archiveToleranceMm);
    }
    for (ULONG i = 0; i < read.breps.GetSize(); ++i)
    {
        Require(read.breps[i], "Linked archive BRep missing");
        Validate(*read.breps[i], i ? "point adapter after read" : "material BRep after read");
        if (i)
        {
            SmTArray<SmVertex*> vertices;
            SmTArray<SmEdge*> edges;
            SmTArray<SmFace*> faces;
            read.breps[i]->GetVertices(vertices);
            read.breps[i]->GetEdges(edges);
            read.breps[i]->GetFaces(faces);
            Require(vertices.GetSize() == 1 && edges.GetSize() == 0 && faces.GetSize() == 0, "Linked point adapter topology changed");
            Require(
                Distance(Coordinates(vertices[0]->GetPoint()), model.curvePointDefinitions.Points()[i - 1].pointMm) <= archiveToleranceMm,
                "Linked archive point changed"
            );
        }
    }
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
} // namespace detail
} // namespace step_import
