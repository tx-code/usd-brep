// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportCylindricalBandRepair.h"

#include "StepImportExecution.h"
#include "StepImportNativeData.h"

#include <SmEdge.h>
#include <SmFace.h>
#include <SmTol.h>
#include <SmVertex.h>
#include <algorithm>
#include <boost/multiprecision/cpp_int.hpp>
#include <map>

namespace step_import::detail
{
namespace
{
void Need(bool condition, const char* key)
{
    if (!condition)
    {
        throw Failure{ Status::Indeterminate, key, "Exact oriented cylindrical-band repair evidence is unestablished", {}, "Repair" };
    }
}
using Integer = boost::multiprecision::number<
    boost::multiprecision::cpp_int_backend<4096, 4096, boost::multiprecision::signed_magnitude, boost::multiprecision::checked, void>>;
struct Ratio
{
    Integer n = 0, d = 1;
};
using Vector = std::array<Ratio, 3>;
class Exact
{
public:

    explicit Exact(Execution& execution) : e(execution)
    {
    }
    Ratio Normalize(Ratio a)
    {
        e.SpendCylindricalBandProof();
        Need(a.d != 0, "CylindricalBandExactDivision");
        if (a.d < 0)
        {
            a.n = -a.n;
            a.d = -a.d;
        }
        Integer x = a.n < 0 ? -a.n : a.n, y = a.d;
        while (y != 0)
        {
            e.SpendCylindricalBandProof();
            Integer r = x % y;
            x = y;
            y = r;
        }
        if (x != 0)
        {
            a.n /= x;
            a.d /= x;
        }
        return a;
    }
    Ratio Parse(std::string_view text)
    {
        Ratio a;
        bool negative = false;
        std::size_t i = 0;
        int decimals = 0, exponent = 0;
        bool fractional = false;
        if (text[i] == '+' || text[i] == '-')
        {
            negative = text[i] == '-';
            ++i;
        }
        for (; i < text.size() && text[i] != 'E' && text[i] != 'e'; ++i)
        {
            e.SpendCylindricalBandProof();
            if (text[i] == '.')
            {
                fractional = true;
                continue;
            }
            a.n = a.n * 10 + (text[i] - '0');
            if (fractional)
            {
                ++decimals;
            }
        }
        if (i < text.size())
        {
            ++i;
            bool minus = false;
            if (text[i] == '+' || text[i] == '-')
            {
                minus = text[i] == '-';
                ++i;
            }
            for (; i < text.size(); ++i)
            {
                e.SpendCylindricalBandProof();
                Need(exponent < 1234, "CylindricalBandExactCapacity");
                exponent = exponent * 10 + text[i] - '0';
            }
            if (minus)
            {
                exponent = -exponent;
            }
        }
        Need(decimals <= 1233 && exponent >= -1233 && exponent <= 1233, "CylindricalBandExactCapacity");
        const int power = exponent - decimals;
        Need(power >= -1233 && power <= 1233, "CylindricalBandExactCapacity");
        for (int p = 0; p < std::abs(power); ++p)
        {
            e.SpendCylindricalBandProof();
            if (power > 0)
            {
                a.n *= 10;
            }
            else
            {
                a.d *= 10;
            }
        }
        if (negative)
        {
            a.n = -a.n;
        }
        return Normalize(std::move(a));
    }
    Ratio Add(const Ratio& a, const Ratio& b)
    {
        return Normalize({ a.n * b.d + b.n * a.d, a.d * b.d });
    }
    Ratio Sub(const Ratio& a, const Ratio& b)
    {
        return Normalize({ a.n * b.d - b.n * a.d, a.d * b.d });
    }
    Ratio Mul(const Ratio& a, const Ratio& b)
    {
        return Normalize({ a.n * b.n, a.d * b.d });
    }
    Ratio Dot(const Vector& a, const Vector& b)
    {
        Ratio r;
        for (std::size_t i = 0; i < 3; ++i)
        {
            r = Add(r, Mul(a[i], b[i]));
        }
        return r;
    }
    Vector Difference(const Vector& a, const Vector& b)
    {
        Vector v;
        for (std::size_t i = 0; i < 3; ++i)
        {
            v[i] = Sub(a[i], b[i]);
        }
        return v;
    }
    bool Parallel(const Vector& a, const Vector& b)
    {
        for (std::size_t i = 0; i < 3; ++i)
        {
            if (Sub(Mul(a[(i + 1) % 3], b[(i + 2) % 3]), Mul(a[(i + 2) % 3], b[(i + 1) % 3])).n != 0)
            {
                return false;
            }
        }
        return true;
    }
    bool Less(const Ratio& a, const Ratio& b)
    {
        e.SpendCylindricalBandProof();
        return a.n * b.d < b.n * a.d;
    }
    bool Equal(const Ratio& a, const Ratio& b)
    {
        e.SpendCylindricalBandProof();
        return a.n == b.n && a.d == b.d;
    }

private:

    Execution& e;
};
class BandProof
{
public:

    BandProof(const step_semantics::Snapshot& source, Execution& execution) : s(source), q(execution)
    {
    }
    std::size_t Ref(std::size_t r, std::string_view name) const
    {
        return s.Field(r, name).ReferenceTarget();
    }
    std::vector<std::size_t> Refs(std::size_t r, std::string_view name) const
    {
        return s.Field(r, name).ReferenceTargets();
    }
    bool Bool(std::size_t r, std::string_view name) const
    {
        return s.Field(r, name).Value().text == "T";
    }
    Vector Coordinates(std::size_t r, std::string_view name)
    {
        const auto& values = s.Field(r, name).Value().children;
        Need(values.size() == 3, "CylindricalBandDimension");
        Vector v;
        for (std::size_t i = 0; i < 3; ++i)
        {
            v[i] = q.Parse(values[i].text);
        }
        return v;
    }
    std::pair<Vector, Vector> Frame(std::size_t r)
    {
        Need(s.IsA(r, "AXIS2_PLACEMENT_3D"), "CylindricalBandPlacement");
        auto origin = Coordinates(Ref(r, "location"), "coordinates");
        Vector axis{ { { 0, 1 }, { 0, 1 }, { 1, 1 } } };
        if (s.Field(r, "axis").Value().kind != step_source::ValueKind::Unset)
        {
            axis = Coordinates(Ref(r, "axis"), "direction_ratios");
        }
        Need(q.Dot(axis, axis).n > 0, "CylindricalBandAxis");
        return { origin, axis };
    }
    CylindricalBandRepairAction Prove(std::size_t face, const step_semantics::CylindricalBandRepairDeferral& edit)
    {
        const auto surface = Ref(face, "face_geometry");
        Need(s.IsA(surface, "CYLINDRICAL_SURFACE"), "CylindricalBandSurface");
        const auto [origin, axis] = Frame(Ref(surface, "position"));
        const auto radius = q.Parse(s.Field(surface, "radius").Value().text);
        Need(radius.n > 0, "CylindricalBandRadius");
        const auto bounds = Refs(face, "bounds");
        Need(bounds.size() == 2, "CylindricalBandBoundaryCount");
        struct Ring
        {
            Ratio height;
            int orientation;
            std::size_t bound, circle;
        };
        std::array<Ring, 2> rings;
        for (std::size_t i = 0; i < 2; ++i)
        {
            const auto b = bounds[i];
            Need(s.IsA(b, "FACE_OUTER_BOUND"), "CylindricalBandOriginalMarkers");
            const auto loop = Ref(b, "bound");
            Need(s.IsA(loop, "EDGE_LOOP"), "CylindricalBandWholeCircleRequired");
            const auto uses = Refs(loop, "edge_list");
            Need(uses.size() == 1, "CylindricalBandWholeCircleRequired");
            const auto use = uses[0];
            Need(s.IsA(use, "ORIENTED_EDGE"), "CylindricalBandWholeCircleRequired");
            const auto edge = Ref(use, "edge_element");
            // EDGE admits structures beyond this profile. Establish EDGE_CURVE
            // before reading its fields so unsupported source uses stay within
            // the structured repair outcome, rather than throwing from Field().
            Need(s.IsA(edge, "EDGE_CURVE"), "CylindricalBandWholeCircleRequired");
            const auto circle = Ref(edge, "edge_geometry");
            Need(s.IsA(circle, "CIRCLE") && Ref(edge, "edge_start") == Ref(edge, "edge_end"), "CylindricalBandWholeCircleRequired");
            const auto [center, normal] = Frame(Ref(circle, "position"));
            const auto delta = q.Difference(center, origin);
            Need(q.Parallel(delta, axis) && q.Parallel(normal, axis), "CylindricalBandCoaxialityUnestablished");
            Need(q.Equal(radius, q.Parse(s.Field(circle, "radius").Value().text)), "CylindricalBandRadiusMismatch");
            const auto normalSense = q.Dot(normal, axis).n;
            Need(normalSense != 0, "CylindricalBandCircleAxis");
            int direction = normalSense > 0 ? 1 : -1;
            for (const bool same : { Bool(b, "orientation"), Bool(face, "same_sense"), Bool(use, "orientation"), Bool(edge, "same_sense") })
            {
                if (!same)
                {
                    direction = -direction;
                }
            }
            rings[i] = { q.Dot(delta, axis), direction, b, circle };
        }
        if (q.Less(rings[1].height, rings[0].height))
        {
            std::swap(rings[0], rings[1]);
        }
        // On S1 x R, these normalized oriented boundaries uniquely select the
        // FINITE band: lower ring increases theta, upper ring decreases it.
        // The complement has opposite boundary orientation. Neither serialized
        // SET order nor an arbitrary distinguished outer loop is used here.
        Need(
            q.Less(rings[0].height, rings[1].height) && rings[0].orientation == 1 && rings[1].orientation == -1,
            "CylindricalBandFiniteOrientedRegionUnestablished"
        );
        const auto& id = s.Source().Records();
        return { edit, id[surface].id, { id[rings[0].circle].id, id[rings[1].circle].id }, { id[rings[0].bound].id, id[rings[1].bound].id } };
    }

private:

    const step_semantics::Snapshot& s;
    Exact q;
};
struct VertexDefinition
{
    const SmVertex* key;
    SmPoint3d point;
    double tolerance;
};
struct EdgeDefinition
{
    const SmEdge* key;
    SmExtent1d interval;
    std::unique_ptr<SmCurve> curve;
    double tolerance;
};
struct FaceDefinition
{
    const SmFace* key;
    std::unique_ptr<SmSurface> surface;
    double tolerance;
};
void Same(double a, double b)
{
    Need(std::isfinite(a) && std::isfinite(b) && a == b, "CylindricalBandChangedDefinitionOrTolerance");
}
} // namespace
struct CylindricalBandRepairTrial
{
    std::shared_ptr<ModelData> owner; // Copies below must be destroyed before their allocating context.
    std::vector<VertexDefinition> vertices;
    std::vector<EdgeDefinition> edges;
    std::vector<FaceDefinition> faces;
    std::vector<std::size_t> sourceFaces;
    CylindricalBandRepairHistory history;
};
std::shared_ptr<CylindricalBandRepairTrial> PrepareCylindricalBandRepair(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const std::optional<step_semantics::Part::Shape>& binding,
    const Options& options,
    Report& report,
    Execution& execution
)
{
    if (!options.cylindricalBandRepair)
    {
        return {};
    }
    const auto& roles = source->PartDefinition().roles;
    const auto definition = std::find_if(
        roles.begin(),
        roles.end(),
        [](const auto& r)
        {
            return r.name == "definition";
        }
    );
    Need(definition != roles.end(), "CylindricalBandDefinitionIdentity");
    const auto shape = binding ? *binding : source->PartDefinition().shapes.at(0);
    const auto& records = source->Source().Records();
    auto trial = std::make_shared<CylindricalBandRepairTrial>();
    std::vector<std::size_t> claimed;
    BandProof proof(*source, execution);
    try
    {
        for (std::size_t i = 0; i < options.cylindricalBandRepair->faces.size(); ++i)
        {
            const auto& edit = options.cylindricalBandRepair->faces[i];
            if (edit.definitionId != records[definition->recordIndex].id || edit.shapeId != records[shape.shape].id)
            {
                continue;
            }
            const auto face = source->Source().Find(edit.faceId);
            Need(face != nullptr, "CylindricalBandFaceIdentity");
            const auto index = static_cast<std::size_t>(face - records.data());
            trial->history.actions.push_back(proof.Prove(index, edit));
            trial->sourceFaces.push_back(index);
            claimed.push_back(i);
        }
    }
    catch (const std::overflow_error&)
    {
        throw Failure{ Status::Indeterminate, "CylindricalBandExactCapacity", "4096-bit exact relation capacity exceeded", {}, "Repair" };
    }
    if (claimed.empty())
    {
        return {};
    }
    execution.ClaimCylindricalBandRepairs(claimed);
    execution.Check(Phase::BeforeRepair);
    trial->history.sourceSha256 = options.cylindricalBandRepair->sourceSha256;
    trial->history.sourceInterpretationApplied = HasAppliedSourceInterpretation(*source);
    for (const auto& action : trial->history.actions)
    {
        const auto& diagnostics = source->CylindricalBandRepairDiagnostics();
        const auto found = std::find_if(
            diagnostics.begin(),
            diagnostics.end(),
            [&](const auto& d)
            {
                return action.edit.faceId == d.source.recordId && d.source.key == "SourceRuleViolation:FACE.WR2";
            }
        );
        Need(found != diagnostics.end(), "CylindricalBandOriginalFailureEvidence");
        // One source entity may be checked in several independent definitions.
        // Its failed clause is an entity fact, repeated by those traversals.
        // Keep one original fact per already-qualified definition/shape action;
        // the global source history and every requested use remain unchanged.
        trial->history.originalSourceDiagnostics.push_back(*found);
    }
    (void)report;
    return trial;
}
bool NeutralizesCylindricalBandBounds(const std::shared_ptr<CylindricalBandRepairTrial>& t, std::size_t face)
{
    return t && std::find(t->sourceFaces.begin(), t->sourceFaces.end(), face) != t->sourceFaces.end();
}
void CaptureCylindricalBandDefinitions(
    const std::shared_ptr<CylindricalBandRepairTrial>& t,
    std::shared_ptr<ModelData> model,
    Report& report,
    Execution& execution
)
{
    if (!t)
    {
        return;
    }
    t->owner = std::move(model);
    auto& m = *t->owner;
    SmTArray<SmVertex*> vertices;
    SmTArray<SmEdge*> edges;
    SmTArray<SmFace*> faces;
    m.brep->GetVertices(vertices);
    m.brep->GetEdges(edges);
    m.brep->GetFaces(faces);
    for (ULONG i = 0; i < vertices.GetSize(); ++i)
    {
        t->vertices.push_back({ vertices[i], vertices[i]->GetPoint(), SmTol::GetZoneTol3d(vertices[i]) });
    }
    for (ULONG i = 0; i < edges.GetSize(); ++i)
    {
        execution.ChargeNative(report, 0);
        auto source = edges[i]->GetCurve();
        SmCurve* raw = nullptr;
        const auto status = source->Copy(*m.context, raw);
        Need(raw != source, "CylindricalBandBorrowedCopy");
        std::unique_ptr<SmCurve> copy(raw);
        Need(status == SM_SUCCESS && copy && copy->GetContext() == m.context.get(), "CylindricalBandCurveCopy");
        copy->SetOwner(nullptr);
        t->edges.push_back({ edges[i], edges[i]->GetInterval(), std::move(copy), SmTol::GetZoneTol3d(edges[i]) });
    }
    for (ULONG i = 0; i < faces.GetSize(); ++i)
    {
        execution.ChargeNative(report, 0);
        auto source = faces[i]->GetSurface();
        SmSurface* raw = nullptr;
        const auto status = source->Copy(*m.context, raw);
        Need(raw != source, "CylindricalBandBorrowedCopy");
        std::unique_ptr<SmSurface> copy(raw);
        Need(status == SM_SUCCESS && copy && copy->GetContext() == m.context.get(), "CylindricalBandSurfaceCopy");
        copy->SetOwner(nullptr);
        t->faces.push_back({ faces[i], std::move(copy), SmTol::GetZoneTol3d(faces[i]) });
    }
}
void CompleteCylindricalBandRepair(const std::shared_ptr<CylindricalBandRepairTrial>& t, ModelData& m, Report& report, Execution& execution)
{
    if (!t)
    {
        return;
    }
    SmTArray<SmVertex*> vertices;
    SmTArray<SmEdge*> edges;
    SmTArray<SmFace*> faces;
    m.brep->GetVertices(vertices);
    m.brep->GetEdges(edges);
    m.brep->GetFaces(faces);
    Need(
        vertices.GetSize() == t->vertices.size() && edges.GetSize() == t->edges.size() && faces.GetSize() == t->faces.size(),
        "CylindricalBandChangedNativeInventory"
    );
    const auto present = [](const auto& list, const auto* key)
    {
        for (ULONG i = 0; i < list.GetSize(); ++i)
        {
            if (list[i] == key)
            {
                return true;
            }
        }
        return false;
    };
    for (const auto& v : t->vertices)
    {
        execution.SpendCylindricalBandProof();
        Need(present(vertices, v.key), "CylindricalBandReplacedVertex");
        const auto p = v.key->GetPoint();
        Same(p.x, v.point.x);
        Same(p.y, v.point.y);
        Same(p.z, v.point.z);
        Same(v.tolerance, SmTol::GetZoneTol3d(v.key));
    }
    for (const auto& e : t->edges)
    {
        execution.SpendCylindricalBandProof();
        Need(present(edges, e.key), "CylindricalBandReplacedEdge");
        Same(e.interval.GetMin(), e.key->GetInterval().GetMin());
        Same(e.interval.GetMax(), e.key->GetInterval().GetMax());
        RequireSameNativeCurveDefinition(*e.curve, *e.key->GetCurve());
        Same(e.tolerance, SmTol::GetZoneTol3d(e.key));
    }
    for (const auto& f : t->faces)
    {
        execution.SpendCylindricalBandProof();
        Need(present(faces, f.key), "CylindricalBandReplacedFace");
        RequireSameNativeSurfaceDefinition(*f.surface, *f.key->GetSurface());
        Same(f.tolerance, SmTol::GetZoneTol3d(f.key));
    }
    t->history.verticesCompared = vertices.GetSize();
    t->history.edgesCompared = edges.GetSize();
    t->history.surfacesCompared = faces.GetSize();
    t->history.definitionsIdentical = t->history.workingTolerancesIdentical = true;
    m.cylindricalBandRepairs = t->history;
    report.cylindricalBandRepairs = t->history;
    execution.Check(Phase::RepairReady);
}
} // namespace step_import::detail
