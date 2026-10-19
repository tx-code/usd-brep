// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportCylinderChart.h"
#include "StepImportExecution.h"
#include "StepImportGeneralMeasurements.h"
#include "StepImportNativeData.h"
#include "StepImportTrimMeasurement.h"

#include <SmApiQueries.h>
#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmBrepData.h>
#include <SmCircle.h>
#include <SmCone.h>
#include <SmCurveClass.h>
#include <SmCylinder.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmEllipse.h>
#include <SmFace.h>
#include <SmFaceProps.h>
#include <SmHealProbArray.h>
#include <SmLine.h>
#include <SmLoop.h>
#include <SmLoopProps.h>
#include <SmPlane.h>
#include <SmRegion.h>
#include <SmTol.h>
#include <SmTorus.h>
#include <SmVertex.h>
#include <algorithm>
#include <cfenv>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <set>

// This path constructs defining geometry directly from a typed source graph.
// It deliberately does not inherit the planar profile's exact region/volume
// proofs. Native topology, measured projection gaps and independent controls
// qualify a development import, not full EXPRESS or global geometric validity.
namespace step_import::detail
{
namespace
{
using Index = std::size_t;
using V = Point;
V Add(V a, V b)
{
    for (Index i = 0; i < 3; ++i)
    {
        a[i] += b[i];
    }
    return a;
}
V Sub(V a, V b)
{
    for (Index i = 0; i < 3; ++i)
    {
        a[i] -= b[i];
    }
    return a;
}
V Mul(V a, double b)
{
    for (auto& x : a)
    {
        x *= b;
    }
    return a;
}
double Dot(V a, V b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
V Cross(V a, V b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
double Norm(V a)
{
    return std::hypot(a[0], a[1], a[2]);
}
V Unit(V a)
{
    const double n = Norm(a);
    if (!(n > 0) || !std::isfinite(n))
    {
        throw std::domain_error("GeneralDegenerateFrame");
    }
    return Mul(a, 1 / n);
}
SmPoint3d P(V a)
{
    return { a[0], a[1], a[2] };
}
SmVector3d D(V a)
{
    return { a[0], a[1], a[2] };
}
V P(const SmPoint3d& a)
{
    return { a.x, a.y, a.z };
}
double Q(Fraction a)
{
    return double(a.Numerator()) / double(a.Denominator());
}
std::array<double, 2> FullPeriodDegrees(double startRadians)
{
    // Kernel analytic angles are degrees. Converting both radian endpoints
    // independently can produce a span one ulp above 360, outside the native
    // full-revolution constructor's range. Convert the phase once and add the
    // native period; this only chooses the computational chart. Source angles,
    // coefficients, topology and the later mm error budgets remain unchanged.
    const double startDegrees = startRadians * 180 / std::numbers::pi;
    return { startDegrees, startDegrees + 360.0 };
}
void Good(SmStatus a)
{
    if (a != SM_SUCCESS)
    {
        throw std::runtime_error("GeneralNativeReturn:" + std::to_string(a));
    }
}
struct Frame
{
    V o, x, y, z;
    SmAxis2Placement Native() const
    {
        return { P(o), D(x), D(y) };
    }
};
struct Curve
{
    Index record = 0, geometry = 0, v0 = 0, v1 = 0;
    bool same = true;
    std::unique_ptr<SmCurve> native;
    double t0 = 0, t1 = 0;
    Bounds bounds{};
    V p0{}, p1{}; // Evaluated endpoints of the defining curve, not the possibly gapped topology vertices.
};
struct Use
{
    Index edge, source;
    bool forward;
};
struct Loop
{
    Index record, bound;
    bool outer, orientation;
    std::vector<Use> uses;
};
struct Face
{
    Index record, surface;
    bool same;
    std::vector<Loop> loops;
};
struct SeamPlan
{
    Index v0, v1;
    V a, b;
    double angle;
};

class GeneralBuilder
{
public:

    GeneralBuilder(std::shared_ptr<const step_semantics::Snapshot> source, const Options& options, Report& report, Execution& execution)
        : m_source(std::move(source)), m_options(options), m_report(report), m_execution(execution), m_model(std::make_shared<ModelData>())
    {
        m_model->source = m_source;
        m_model->queryTolerance = Q(m_options.quality.queryMm);
        m_model->definitionToleranceMm = Q(m_options.quality.definitionMm);
        m_model->queryBudget = m_options.quality.queryMm;
    }
    std::shared_ptr<ModelData> Run()
    {
        if (!std::numeric_limits<double>::is_iec559 || std::fegetround() != FE_TONEAREST)
        {
            throw Failure{ Status::Indeterminate, "UnsupportedNativeNumericEnvironment", "Nearest binary64 environment required", {}, "GeneralBrep" };
        }
        Prepare();
        Step(shape);
        m_model->context = std::make_unique<SmContext>();
        // All native candidates belong to this context and remain private until
        // the caller's existing bounded-report and cancellation commit point.
        SmBrepConstructor c;
        Step(shape);
        m_model->brep.reset(c.StartBrep(*m_model->context));
        Need(bool(m_model->brep), "GeneralBrepAllocation", shape);
        m_model->brep->SetTolerance(Tol(), FALSE);
        for (auto& curve : curves)
        {
            MakeCurve(curve);
        }
        for (Index i = 0; i < vertices.size(); ++i)
        {
            Step(vertices[i]);
            auto v = c.StartVertex(P(points[i]));
            Need(v != nullptr, "GeneralVertexAllocation", vertices[i]);
            v->SetTolerance(Tol(), FALSE);
            m_model->vertices.push_back(v);
            Step(vertices[i]);
            Good(c.EndVertex());
        }
        Step(shape);
        m_model->bounded = c.StartRegion(TRUE, nullptr);
        Need(m_model->bounded != nullptr, "GeneralRegionAllocation", shape);
        m_model->bounded->SetIsVoid(FALSE);
        m_model->material = m_model->bounded;
        Step(shell);
        Good(c.StartShell(m_model->outside, m_model->inside));
        m_model->edges.resize(curves.size());
        for (auto& f : faces)
        {
            BuildFace(c, f);
        }
        Step(shell);
        Good(c.EndShell(FALSE)); // No implicit stitching or source repair.
        Step(shape);
        Good(c.EndRegion());
        Step(shape);
        Good(c.EndBrep());
        InstallUv();
        Validate();
        ObserveTolerances();
        Measure();
        BuildWireframes(); // The same unpublished candidate owns every selected representation.
        BuildSupportingGeometry();
        return m_model;
    }

private:

    std::shared_ptr<const step_semantics::Snapshot> m_source;
    const Options& m_options;
    Report& m_report;
    Execution& m_execution;
    std::shared_ptr<ModelData> m_model;
    Index shape = 0, shell = 0;
    double scale = 0;
    std::vector<Index> vertices;
    std::vector<V> points;
    std::map<Index, Index> vi, ei;
    std::vector<Curve> curves;
    std::vector<Face> faces;
    double consistency = 0, uvGap = 0;
    std::vector<Index> derivedEdgeFaces;
    std::optional<Index> wireframeRepresentation;
    std::optional<Index> supportingRepresentation;
    std::map<Index, WireframeCurveInfo> detachedCurveInfo;
    std::map<Index, WireframePointInfo> detachedPointInfo;
    double Tol() const
    {
        return Q(m_options.quality.initialWorkToleranceMm);
    }
    bool Is(Index a, std::string_view type) const
    {
        const auto registered = m_source->RegisteredTypes();
        return std::find(registered.begin(), registered.end(), type) != registered.end() && m_source->IsA(a, type);
    }
    std::string Id(Index a) const
    {
        return m_source->Source().Records().at(a).id;
    }
    [[noreturn]] void Fail(const char* key, Index a, Status status = Status::PostcheckFailure) const
    {
        throw Failure{ status, key, "General source-to-native contract is unestablished", a, "GeneralBrep" };
    }
    void Need(bool b, const char* key, Index a) const
    {
        if (!b)
        {
            Fail(key, a);
        }
    }
    void Step(Index a)
    {
        m_execution.Check(Phase::BeforeNativeCall);
        if (m_report.nativeCalls >= m_options.nativeLimits.nativeCalls)
        {
            Fail("NativeCallLimit", a, Status::ResourceLimit);
        }
        ++m_report.nativeCalls;
    }
    const step_source::Value& F(Index a, std::string_view name) const
    {
        return m_source->Field(a, name).Value();
    }
    Index Ref(Index a, std::string_view name) const
    {
        return m_source->Field(a, name).ReferenceTarget();
    }
    std::vector<Index> Refs(Index a, std::string_view name) const
    {
        return m_source->Field(a, name).ReferenceTargets();
    }
    bool B(Index a, std::string_view name) const
    {
        return F(a, name).text == "T";
    }
    double Number(const step_source::Value& v) const
    {
        std::string_view t = v.text;
        if (t.starts_with('+'))
        {
            t.remove_prefix(1);
        }
        double n = 0;
        auto parsed = std::from_chars(t.data(), t.data() + t.size(), n, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != t.data() + t.size() || !std::isfinite(n))
        {
            throw std::domain_error("GeneralNumericRangeUnestablished");
        }
        return n; // Source lexemes remain owned by the immutable snapshot.
    }
    double N(Index a, std::string_view name) const
    {
        return Number(F(a, name));
    }
    V Triple(const step_source::Value& v) const
    {
        if (v.children.size() != 3)
        {
            throw std::domain_error("GeneralRequires3DGeometry");
        }
        return { Number(v.children[0]), Number(v.children[1]), Number(v.children[2]) };
    }
    V Position(Index a) const
    {
        const auto point = Mul(Triple(F(a, "coordinates")), scale);
        for (const auto value : point)
        {
            if (!std::isfinite(value))
            {
                throw std::domain_error("GeneralLengthRangeUnestablished");
            }
        }
        return point;
    }
    V Direction(Index a) const
    {
        return Unit(Triple(F(a, "direction_ratios")));
    }
    Frame Placement(Index a) const
    {
        Frame f;
        f.o = Position(Ref(a, "location"));
        f.z = F(a, "axis").kind == step_source::ValueKind::Unset ? V{ 0, 0, 1 } : Direction(Ref(a, "axis"));
        V x;
        if (F(a, "ref_direction").kind != step_source::ValueKind::Unset)
        {
            x = Direction(Ref(a, "ref_direction"));
        }
        else
        {
            x = f.z[1] != 0 || f.z[2] != 0 ? V{ 1, 0, 0 } : V{ 0, 1, 0 };
        }
        // STEP build_axes projects the reference direction onto the normal
        // plane. Ratios are dimensionless; arbitrary source magnitudes are not scales.
        // AP214E3_2010.exp FIRST_PROJ_AXIS defaults to X unless exactly
        // parallel to it. The double cross avoids cancellation near parallel.
        f.x = Unit(Cross(f.z, Cross(x, f.z)));
        f.y = Unit(Cross(f.z, f.x));
        return f;
    }
    Index Role(std::string_view name) const
    {
        for (const auto& role : m_source->PartDefinition().roles)
        {
            if (role.name == name)
            {
                return role.recordIndex;
            }
        }
        throw std::domain_error("GeneralRequiredSourceRoleMissing");
    }
    Index Vertex(Index a)
    {
        if (auto it = vi.find(a); it != vi.end())
        {
            return it->second;
        }
        const auto i = vertices.size();
        vi.emplace(a, i);
        vertices.push_back(a);
        points.push_back(Position(Ref(a, "vertex_geometry")));
        return i;
    }
    Index Edge(Index a)
    {
        if (auto it = ei.find(a); it != ei.end())
        {
            return it->second;
        }
        const auto i = curves.size();
        ei.emplace(a, i);
        // AP214E3_2010.exp EDGE_CURVE.same_sense: orient the private edge
        // along increasing defining-curve parameter, reversing source endpoints
        // only in this mapping. Source EDGE_CURVE and vertex records stay intact.
        const auto geometry = Ref(a, "edge_geometry");
        // ISO 10303-42:2003 4.4.40: a trimmed curve's parameter increases
        // from trim_1, opposing its basis when sense_agreement is FALSE.
        // Native conics increase along the BASIS, so compose this extra sense
        // with EC and subsequently OE; never change the immutable source flags.
        const bool same = Is(geometry, "TRIMMED_CURVE") ? B(a, "same_sense") == B(geometry, "sense_agreement") : B(a, "same_sense");
        const auto start = Ref(a, "edge_start"), end = Ref(a, "edge_end");
        curves.push_back({ a, geometry, Vertex(same ? start : end), Vertex(same ? end : start), same, {}, 0, 0, {} });
        return i;
    }
    void Prepare()
    {
        for (const auto& role : m_source->PartDefinition().roles)
        {
            if (role.name == "wireframe_representation")
            {
                wireframeRepresentation = role.recordIndex;
            }
            if (role.name == "construction_representation")
            {
                supportingRepresentation = role.recordIndex;
            }
        }
        Need(m_source->PartDefinition().shapeRole == "MaterialSolid", "GeneralRequiresMaterialSolid", Role("shape"));
        shape = Role("shape");
        shell = Role("shell");
        Need(Is(shape, "MANIFOLD_SOLID_BREP") && Is(shell, "CLOSED_SHELL"), "GeneralUnsupportedShape", shape);
        const auto power = m_source->UnitContext().lengthPower10Mm;
        if (!power)
        {
            Fail("GeneralConversionUnitNotImplemented", shape, Status::Unsupported);
        }
        scale = std::pow(10.0, *power);
        Need(scale > 0 && std::isfinite(scale), "GeneralUnitScaleRange", shape);
        // Angular placement conventions do not use the file's angle unit.
        // CONICAL_SURFACE.semi_angle does, and requires an explicit radian SI context.
        for (const auto a : Refs(shell, "cfs_faces"))
        {
            Face f{ a, Ref(a, "face_geometry"), B(a, "same_sense"), {} };
            for (const auto bound : Refs(a, "bounds"))
            {
                const auto loop = Ref(bound, "bound");
                Loop l{ loop, bound, Is(bound, "FACE_OUTER_BOUND"), B(bound, "orientation"), {} };
                const auto oriented = Refs(loop, "edge_list");
                // AP214E3_2010.exp ORIENTED_EDGE.orientation is relative to EC;
                // FACE_BOUND.orientation reverses the whole source loop, while
                // ADVANCED_FACE.same_sense relates material face to surface normal.
                // Native primary loops use the surface-normal sense, so compose
                // all three with EC.same_sense before constructing ordered uses.
                const bool forward = l.orientation == f.same;
                for (Index k = 0; k < oriented.size(); ++k)
                {
                    const auto use = oriented[forward ? k : oriented.size() - 1 - k];
                    const auto edge = Edge(Ref(use, "edge_element"));
                    const bool along = B(use, "orientation") == curves[edge].same;
                    l.uses.push_back({ edge, use, forward ? along : !along });
                }
                // Closure is by source vertex identity, not a distance-based weld.
                for (Index k = 0; k < l.uses.size(); ++k)
                {
                    const auto& u = l.uses[k];
                    const auto& next = l.uses[(k + 1) % l.uses.size()];
                    const auto& c = curves[u.edge];
                    const auto& d = curves[next.edge];
                    Need((u.forward ? c.v1 : c.v0) == (next.forward ? d.v0 : d.v1), "GeneralSourceLoopDisconnected", bound);
                }
                f.loops.push_back(std::move(l));
            }
            std::stable_sort(
                f.loops.begin(),
                f.loops.end(),
                [](const Loop& a, const Loop& b)
                {
                    return a.outer > b.outer;
                }
            );
            faces.push_back(std::move(f));
        }
        // Incidence is tested in material-boundary sense. Undo the preceding
        // surface-normal normalization with face.same_sense before comparing
        // the two uses: every closed-shell EC must occur oppositely twice.
        std::vector<std::vector<std::pair<Index, bool>>> incidence(curves.size());
        for (const auto& f : faces)
        {
            for (const auto& l : f.loops)
            {
                for (const auto& use : l.uses)
                {
                    incidence[use.edge].push_back({ f.record, use.forward == f.same });
                }
            }
        }
        for (Index i = 0; i < incidence.size(); ++i)
        {
            const auto& uses = incidence[i];
            Need(uses.size() == 2 && uses[0].second != uses[1].second, "GeneralSourceNonManifoldEdge", curves[i].record);
        }
        m_report.sourceShape = SourceShapeObservation{ "MaterialSolid", Id(Role("representation")), "SourceRepresentation",
                                                       Id(shape),       "MANIFOLD_SOLID_BREP",      Id(shell),
                                                       "CLOSED_SHELL" };
        std::int64_t num = 1, den = 1;
        for (int i = 0; i < std::abs(*power); ++i)
        {
            if (i >= 15)
            {
                Fail("GeneralUnitScaleRange", shape, Status::Indeterminate);
            }
            if (*power < 0)
            {
                den *= 10;
            }
            else
            {
                num *= 10;
            }
        }
        m_report.units = UnitNormalization{ Id(m_source->UnitContext().lengthUnit), Fraction(num, den), {} };
        if (supportingRepresentation)
        {
            // Physical content cannot be an interoperability exception. Reject
            // unsupported required items before allocating a native candidate.
            for (const auto geometry : Refs(*supportingRepresentation, "items"))
            {
                const auto basis = Is(geometry, "TRIMMED_CURVE") ? Ref(geometry, "basis_curve") : geometry;
                const bool curve = Is(geometry, "TRIMMED_CURVE") ?
                                       Is(basis, "LINE") || Is(basis, "CIRCLE") || Is(basis, "ELLIPSE") :
                                       Is(basis, "CIRCLE") || Is(basis, "ELLIPSE") || Is(basis, "B_SPLINE_CURVE_WITH_KNOTS");
                if (!Is(geometry, "CARTESIAN_POINT") && !Is(geometry, "AXIS2_PLACEMENT_3D") && !Is(geometry, "PLANE") && !curve)
                {
                    Fail("GeneralSupportingGeometryUnsupported", geometry, Status::Unsupported);
                }
            }
        }
        if (wireframeRepresentation)
        {
            for (const auto set : Refs(*wireframeRepresentation, "items"))
            {
                if (Is(set, "AXIS2_PLACEMENT_3D"))
                {
                    continue; // Retained source frame, not an inferred relation transform.
                }
                for (const auto geometry : Refs(set, "elements"))
                {
                    const auto basis = Is(geometry, "TRIMMED_CURVE") ? Ref(geometry, "basis_curve") : geometry;
                    const bool admitted = Is(geometry, "CARTESIAN_POINT") ||
                                          (Is(geometry, "TRIMMED_CURVE") ?
                                               Is(basis, "LINE") || Is(basis, "CIRCLE") || Is(basis, "ELLIPSE") :
                                               Is(basis, "CIRCLE") || Is(basis, "ELLIPSE") || Is(basis, "B_SPLINE_CURVE_WITH_KNOTS"));
                    if (!admitted)
                    {
                        Fail("GeneralWireframeGeometryUnsupported", geometry, Status::Unsupported);
                    }
                }
            }
        }
    }
    struct ConicTrim
    {
        double first, last, low, high;
        bool full;
        std::optional<double> angleScale;
    };
    ConicTrim TrimmedConic(Index wrapper, const Frame& frame, double rx, double ry, double& measuredConsistency)
    {
        constexpr double tau = 2 * std::numbers::pi;
        const auto evaluate = [&](double t)
        {
            return Add(frame.o, Add(Mul(frame.x, rx * std::cos(t)), Mul(frame.y, ry * std::sin(t))));
        };
        const auto angle = [&](V p)
        {
            const auto d = Sub(p, frame.o);
            return std::atan2(Dot(d, frame.y) / ry, Dot(d, frame.x) / rx);
        };
        std::optional<double> angleScale;
        const auto preferred = F(wrapper, "master_representation").text;
        const auto cut = [&](std::string_view field)
        {
            std::optional<double> parameter, pointAngle;
            std::optional<V> point;
            for (const auto& value : F(wrapper, field).children)
            {
                if (value.kind == step_source::ValueKind::Reference)
                {
                    point = Position(m_source->ReferenceTarget(value));
                    pointAngle = angle(*point);
                    const auto gap = Norm(Sub(evaluate(*pointAngle), *point));
                    measuredConsistency = std::max(measuredConsistency, gap);
                    Need(std::isfinite(gap) && gap <= Q(m_options.quality.consistencyMm), "GeneralTrimmedPointOffBasis", wrapper);
                }
                else
                {
                    if (!angleScale)
                    {
                        for (const auto& unit : m_source->UnitContext().siDefinitions)
                        {
                            if (unit.recordIndex == m_source->UnitContext().planeAngleUnit && unit.siName == "RADIAN")
                            {
                                angleScale = std::pow(10., unit.canonicalPower10);
                            }
                        }
                        if (!angleScale || !std::isfinite(*angleScale) || !(*angleScale > 0))
                        {
                            Fail("GeneralTrimmedAngleUnitUnestablished", wrapper, Status::Unsupported);
                        }
                    }
                    parameter = Number(value.children.at(0)) * *angleScale;
                    // Limit argument reduction to a small, explicit native
                    // development range. This is not an EXPRESS restriction.
                    if (!std::isfinite(*parameter) || std::abs(*parameter) > 2 * tau)
                    {
                        Fail("GeneralTrimmedAngleRangeUnestablished", wrapper, Status::Unsupported);
                    }
                }
            }
            if (parameter && point)
            {
                // Master preference chooses the defining cut but does not
                // excuse a conflicting second representation (4.4.40 IP1/IP2).
                const auto gap = Norm(Sub(evaluate(*parameter), *point));
                measuredConsistency = std::max(measuredConsistency, gap);
                Need(std::isfinite(gap) && gap <= Q(m_options.quality.consistencyMm), "GeneralTrimmedRepresentationsConflict", wrapper);
            }
            // With UNSPECIFIED and two consistent forms, deterministically
            // retain the supplied parameter. With one form, use that form.
            return preferred == "CARTESIAN" && pointAngle ? *pointAngle : parameter ? *parameter : pointAngle.value();
        };
        double first = cut("trim_1"), last = cut("trim_2");
        const bool same = B(wrapper, "sense_agreement");
        double extent = same ? last - first : first - last;
        if (extent < 0 && extent > -tau)
        {
            if (same)
            {
                last += tau;
            }
            else
            {
                first += tau;
            }
            extent += tau;
        }
        // Only binary64 conversion roundoff can identify one period here;
        // consistencyMm is never used to turn a short arc into a closed circle.
        const double roundoff = 32 * std::numeric_limits<double>::epsilon() * tau;
        if (!(extent > 0) || extent > tau + roundoff)
        {
            Fail("GeneralTrimmedConicExtentUnestablished", wrapper, Status::Unsupported);
        }
        return { first, last, std::min(first, last), std::max(first, last), std::abs(extent - tau) <= roundoff, angleScale };
    }
    std::unique_ptr<SmCurve> MakeConic(Index geometry, const Frame& frame, double rx, double ry, const SmExtent1d& domain, bool checked)
    {
        const bool circle = Is(geometry, "CIRCLE");
        std::unique_ptr<SmCurve> result;
        Step(geometry);
        if (checked)
        {
            SmStatus status;
            if (circle)
            {
                SmCircle* curve = nullptr;
                status = SmCircle::CreateCanonical(*m_model->context, frame.Native(), rx, curve, &domain);
                result.reset(curve);
            }
            else
            {
                SmEllipse* curve = nullptr;
                status = SmEllipse::CreateCanonical(*m_model->context, frame.Native(), rx, ry, curve, &domain);
                result.reset(curve);
            }
            Good(status); // Own even a nonnull failure result before propagating.
        }
        else if (circle)
        {
            result.reset(new (*m_model->context) SmCircle(P(frame.o), D(frame.x), D(frame.y), domain, rx, 3, m_model->context.get()));
        }
        else
        {
            result.reset(new (*m_model->context) SmEllipse(P(frame.o), D(frame.x), D(frame.y), domain, rx, ry, 3, m_model->context.get()));
        }
        return result;
    }
    std::unique_ptr<SmCurve> MakeSplineCurve(Index geometry, Index use)
    {
        SmTArray<SmPoint3d> cp;
        for (const auto control : Refs(geometry, "control_points_list"))
        {
            cp.Add(P(Position(control)));
        }
        auto mult = Multiplicities(geometry, "knot_multiplicities");
        auto knots = Numbers(geometry, "knots");
        auto weights = SplineWeights(geometry, false, cp);
        SmBSplineCurve* raw = nullptr;
        Step(geometry);
        const auto status = SmBSplineCurve::CreateCanonical(
            *m_model->context,
            3,
            Degree(geometry, "degree"),
            cp,
            SM_CF_UNSPECIFIED,
            mult,
            knots,
            SM_KT_UNSPECIFIED,
            weights.GetSize() ? &weights : nullptr,
            nullptr,
            raw
        );
        std::unique_ptr<SmCurve> result(raw);
        Good(status);
        if (weights.GetSize())
        {
            Need(bool(raw), "GeneralCurveAllocation", geometry);
            ULONG count = 0;
            double* net = nullptr;
            Good(raw->GetControlPointsPointer(count, net));
            VerifyRationalNet(geometry, use, cp, weights, net, count, raw->IsRational());
        }
        return result;
    }
    WireframeCurveInfo AddWireCurve(Index record, double& sourceConsistency, double& definitionEndpoints, bool supporting = false)
    {
        const bool trimmed = Is(record, "TRIMMED_CURVE");
        const auto basis = trimmed ? Ref(record, "basis_curve") : record;
        WireframeCurveInfo info{ Id(record), Id(basis), m_model->wireCurves.size(), {}, !trimmed || B(record, "sense_agreement") };
        std::unique_ptr<SmCurve> curve;
        ParameterMapping mapping;
        mapping.sourceGeometryId = Id(record);
        mapping.sourceBasisGeometryId = Id(basis);
        mapping.sourceUseId = Id(record);
        mapping.targetCurveIndex = info.nativeIndex;
        std::optional<std::array<V, 2>> expectedEnds;
        if (Is(basis, "LINE"))
        {
            // ISO 10303-42:2003 4.4.40 trim parameters follow the basis LINE:
            // P(t)=origin+t*magnitude*normalized_direction. t is dimensionless;
            // scale the VECTOR length once, never scale t by the length unit.
            const auto origin = Position(Ref(basis, "pnt"));
            const auto vector = Ref(basis, "dir");
            const auto direction = Direction(Ref(vector, "orientation"));
            const auto magnitude = N(vector, "magnitude") * scale;
            Need(std::isfinite(magnitude) && magnitude > 0, "GeneralWireLineMagnitude", record);
            const auto evaluate = [&](double t)
            {
                return Add(origin, Mul(direction, magnitude * t));
            };
            const auto preferred = F(record, "master_representation").text;
            const auto cut = [&](std::string_view field)
            {
                std::optional<double> parameter, pointParameter;
                std::optional<V> point;
                for (const auto& value : F(record, field).children)
                {
                    if (value.kind == step_source::ValueKind::Reference)
                    {
                        point = Position(m_source->ReferenceTarget(value));
                        pointParameter = Dot(Sub(*point, origin), direction) / magnitude;
                        const auto gap = Norm(Sub(evaluate(*pointParameter), *point));
                        sourceConsistency = std::max(sourceConsistency, gap);
                        Need(std::isfinite(gap) && gap <= Q(m_options.quality.consistencyMm), "GeneralWireTrimPointOffBasis", record);
                    }
                    else
                    {
                        parameter = Number(value.children.at(0));
                    }
                }
                if (parameter && point)
                {
                    const auto gap = Norm(Sub(evaluate(*parameter), *point));
                    sourceConsistency = std::max(sourceConsistency, gap);
                    Need(std::isfinite(gap) && gap <= Q(m_options.quality.consistencyMm), "GeneralTrimmedRepresentationsConflict", record);
                }
                return preferred == "CARTESIAN" && pointParameter ? *pointParameter : parameter ? *parameter : pointParameter.value();
            };
            const auto first = cut("trim_1"), last = cut("trim_2");
            Need(info.sourceForwardInNative ? last > first : first > last, "GeneralWireLineTrimOrder", record);
            const auto low = std::min(first, last), high = std::max(first, last);
            expectedEnds = std::array{ evaluate(low), evaluate(high) };
            for (const auto& point : *expectedEnds)
            {
                Need(
                    std::all_of(
                        point.begin(),
                        point.end(),
                        [](double v)
                        {
                            return std::isfinite(v);
                        }
                    ),
                    "GeneralWireLineRange",
                    record
                );
            }
            SmLine* raw = nullptr;
            Step(record);
            const auto status = SmLine::CreateLineSegment(*m_model->context, 3, P((*expectedEnds)[0]), P((*expectedEnds)[1]), raw);
            curve.reset(raw);
            Good(status);
            mapping.kind = supporting ? "SupportingTrimmedLine" : "WireframeTrimmedLine";
            mapping.equation = "basis_t=lerp(basis_range,(native_t-native_begin)/(native_end-native_begin)); source_s=sign*(basis_t-trim_1)";
            mapping.parameters = { { "basis_trim_parameters", { first, last } },
                                   { "basis_range", { low, high } },
                                   { "vector_magnitude_mm", { magnitude } } };
        }
        else if (Is(basis, "CIRCLE") || Is(basis, "ELLIPSE"))
        {
            const auto frame = Placement(Ref(basis, "position"));
            const bool circle = Is(basis, "CIRCLE");
            const auto rx = N(basis, circle ? "radius" : "semi_axis_1") * scale;
            const auto ry = circle ? rx : N(basis, "semi_axis_2") * scale;
            Need(std::isfinite(rx) && std::isfinite(ry) && rx > SM_EFF_ZERO && ry > SM_EFF_ZERO, "GeneralWireConicRadiusRange", record);
            constexpr double tau = 2 * std::numbers::pi;
            const auto trim = trimmed ? TrimmedConic(record, frame, rx, ry, sourceConsistency) : ConicTrim{ 0, tau, 0, tau, true, {} };
            double low = trim.low, high = trim.high;
            const auto shift = tau * std::floor(low / tau);
            low -= shift;
            high -= shift;
            if (high > tau)
            {
                low -= tau;
                high -= tau;
            }
            const auto degrees = trim.full ? FullPeriodDegrees(low) :
                                             std::array<double, 2>{ low * 180 / std::numbers::pi, high * 180 / std::numbers::pi };
            Need(degrees[1] > degrees[0] && degrees[0] >= -360 && degrees[1] <= 360, "GeneralWireConicAngleRange", record);
            curve = MakeConic(basis, frame, rx, ry, SmExtent1d(degrees[0], degrees[1]), true);
            const auto evaluate = [&](double angle)
            {
                return Add(frame.o, Add(Mul(frame.x, rx * std::cos(angle)), Mul(frame.y, ry * std::sin(angle))));
            };
            expectedEnds = std::array{ evaluate(trim.low), evaluate(trim.high) };
            mapping.kind = supporting ? "SupportingConicRationalParameter" : "WireframeConicRationalParameter";
            mapping.equation =
                "basis_angle_rad=atan2(dot(P(native_t)-origin,y)/ry,dot(P(native_t)-origin,x)/rx),unwrapped_to_basis_range_rad; native_t_is_not_angle";
            mapping.parameters = { { "basis_trim_rad", { trim.first, trim.last } },     { "basis_range_rad", { trim.low, trim.high } },
                                   { "native_chart_range_rad", { low, high } },         { "radii_mm", { rx, ry } },
                                   { "origin_mm", { frame.o.begin(), frame.o.end() } }, { "x", { frame.x.begin(), frame.x.end() } },
                                   { "y", { frame.y.begin(), frame.y.end() } } };
            if (trim.angleScale)
            {
                mapping.parameters.push_back({ "angle_parameter_to_rad", { *trim.angleScale } });
            }
        }
        else
        {
            curve = MakeSplineCurve(basis, record);
            mapping.kind = supporting ? "SupportingSplineIdentity" : "WireframeSplineIdentity";
            mapping.equation = "source_t=native_t; entire_source_natural_interval; defining_coefficients_unchanged";
        }
        Need(bool(curve) && curve->GetDim() == 3, "GeneralWireCurveAllocation", record);
        const auto domain = curve->GetNaturalInterval();
        info.parameterDomain = { domain.GetMin(), domain.GetMax() };
        Need(std::isfinite(domain.GetMin()) && std::isfinite(domain.GetMax()) && domain.GetMax() > domain.GetMin(), "GeneralWireNativeDomain", record);
        if (expectedEnds)
        {
            for (Index i = 0; i < 2; ++i)
            {
                SmPoint3d actual;
                Step(record);
                Good(curve->EvaluatePoint(info.parameterDomain[i], actual));
                const auto gap = Norm(Sub(P(actual), (*expectedEnds)[i]));
                definitionEndpoints = std::max(definitionEndpoints, gap);
                Need(std::isfinite(gap) && gap <= Q(m_options.quality.definitionMm), "GeneralWireDefinitionEndpointGap", record);
            }
        }
        mapping.parameters.push_back({ "native_natural_interval", { domain.GetMin(), domain.GetMax() } });
        mapping.parameters.push_back({ "source_forward_in_native", { info.sourceForwardInNative ? 1. : -1. } });
        m_report.parameterMappings.push_back(std::move(mapping));
        m_model->wireCurves.push_back(std::move(curve));
        return info;
    }
    void BuildWireframes()
    {
        if (!wireframeRepresentation)
        {
            return;
        }
        auto& curveInfo = detachedCurveInfo;
        auto& pointInfo = detachedPointInfo;
        double sourceConsistency = 0, definitionEndpoints = 0;
        for (const auto set : Refs(*wireframeRepresentation, "items"))
        {
            if (!Is(set, "GEOMETRIC_CURVE_SET"))
            {
                continue;
            }
            WireframeInfo group;
            group.representationId = Id(*wireframeRepresentation);
            group.relationshipId = Id(Role("wireframe_shape_relationship"));
            group.setId = Id(set);
            bool first = true;
            const auto include = [&](const Bounds& bounds)
            {
                for (Index axis = 0; axis < 3; ++axis)
                {
                    Need(
                        std::isfinite(bounds[0][axis]) && std::isfinite(bounds[1][axis]) && bounds[0][axis] <= bounds[1][axis],
                        "GeneralWireFiniteBounds",
                        set
                    );
                    group.boundsMm[0][axis] = first ? bounds[0][axis] : std::min(group.boundsMm[0][axis], bounds[0][axis]);
                    group.boundsMm[1][axis] = first ? bounds[1][axis] : std::max(group.boundsMm[1][axis], bounds[1][axis]);
                }
                first = false;
            };
            for (const auto geometry : Refs(set, "elements"))
            {
                m_execution.Check(Phase::BeforeNativeCall);
                if (Is(geometry, "CARTESIAN_POINT"))
                {
                    if (!pointInfo.contains(geometry))
                    {
                        WireframePointInfo point{ Id(geometry), m_model->wirePoints.size(), Position(geometry) };
                        m_model->wirePoints.push_back(point);
                        pointInfo.emplace(geometry, std::move(point));
                    }
                    const auto& point = pointInfo.at(geometry);
                    group.points.push_back(point);
                    include({ point.pointMm, point.pointMm });
                }
                else
                {
                    if (!curveInfo.contains(geometry))
                    {
                        curveInfo.emplace(geometry, AddWireCurve(geometry, sourceConsistency, definitionEndpoints));
                    }
                    const auto& info = curveInfo.at(geometry);
                    group.curves.push_back(info);
                    SmExtent3d box;
                    Step(geometry);
                    Good(m_model->wireCurves.at(info.nativeIndex)
                             ->CalculateTightBoundingBox(SmExtent1d(info.parameterDomain[0], info.parameterDomain[1]), &box));
                    include({ P(box.GetMin()), P(box.GetMax()) });
                }
            }
            Need(!first, "GeneralWireEmptySet", set);
            m_model->wireframes.push_back(std::move(group));
        }
        m_report.wireframes = m_model->wireframes;
        m_report.tolerances.push_back({ "Postcheck", "MeasuredWireframeSourceTrimResidual", Id(*wireframeRepresentation), sourceConsistency });
        m_report.tolerances.push_back({ "Postcheck", "MeasuredWireframeDefinitionEndpointError", Id(*wireframeRepresentation), definitionEndpoints });
    }
    void BuildSupportingGeometry()
    {
        if (!supportingRepresentation)
        {
            return;
        }
        SupportingGeometryInfo group;
        group.representationId = Id(*supportingRepresentation);
        group.relationshipId = Id(Role("construction_relationship"));
        double sourceConsistency = 0, definitionEndpoints = 0;
        for (const auto geometry : Refs(*supportingRepresentation, "items"))
        {
            m_execution.Check(Phase::BeforeNativeCall);
            if (Is(geometry, "CARTESIAN_POINT"))
            {
                if (!detachedPointInfo.contains(geometry))
                {
                    WireframePointInfo point{ Id(geometry), m_model->wirePoints.size(), Position(geometry) };
                    m_model->wirePoints.push_back(point);
                    detachedPointInfo.emplace(geometry, std::move(point));
                }
                group.points.push_back(detachedPointInfo.at(geometry));
            }
            else if (Is(geometry, "AXIS2_PLACEMENT_3D") || Is(geometry, "PLANE"))
            {
                const bool plane = Is(geometry, "PLANE");
                const auto position = plane ? Ref(geometry, "position") : geometry;
                const auto frame = Placement(position);
                // STEP PLANE is unbounded. CreateCanonical retains its analytic
                // axes and native sentinel domain, without a finite source trim.
                // For a direct placement this same native storage is explicitly
                // an archive frame adapter, not an added surface or locus.
                SmPlane* raw = nullptr;
                Step(geometry);
                const auto status = SmPlane::CreateCanonical(*m_model->context, frame.Native(), raw);
                std::unique_ptr<SmPlane> owned(raw);
                Good(status);
                Need(bool(owned), "GeneralSupportingPlaneAllocation", geometry);
                const auto& actual = owned->GetPosition();
                const auto axesEqual = [&](const auto& value, const V& expected)
                {
                    return value.x == expected[0] && value.y == expected[1] && value.z == expected[2];
                };
                Need(
                    axesEqual(actual.GetOriginRef(), frame.o) && axesEqual(actual.GetXAxisRef(), frame.x) && axesEqual(actual.GetYAxisRef(), frame.y),
                    "GeneralSupportingFrameChanged",
                    geometry
                );
                const CoordinateFrame coordinates{ frame.o, frame.x, frame.y, frame.z };
                const auto index = m_model->supportingSurfaces.size();
                if (plane)
                {
                    const auto domain = owned->GetNaturalUVDomain();
                    group.planes.push_back(
                        { Id(geometry),
                          Id(position),
                          index,
                          coordinates,
                          { domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y } }
                    );
                }
                else
                {
                    group.frames.push_back({ Id(geometry), index, coordinates });
                }
                m_model->supportingSurfaces.push_back(std::move(owned));
            }
            else
            {
                if (!detachedCurveInfo.contains(geometry))
                {
                    detachedCurveInfo.emplace(geometry, AddWireCurve(geometry, sourceConsistency, definitionEndpoints, true));
                }
                group.curves.push_back(detachedCurveInfo.at(geometry));
            }
        }
        m_model->supportingGeometry.push_back(std::move(group));
        m_report.supportingGeometry = m_model->supportingGeometry;
        m_report.tolerances.push_back({ "Postcheck", "MeasuredSupportingSourceTrimResidual", Id(*supportingRepresentation), sourceConsistency });
        m_report.tolerances.push_back(
            { "Postcheck", "MeasuredSupportingDefinitionEndpointError", Id(*supportingRepresentation), definitionEndpoints }
        );
    }
    void MakeCurve(Curve& c)
    {
        const bool trimmed = Is(c.geometry, "TRIMMED_CURVE");
        const auto g = trimmed ? Ref(c.geometry, "basis_curve") : c.geometry;
        if (trimmed && !Is(g, "CIRCLE") && !Is(g, "ELLIPSE"))
        {
            Fail("GeneralTrimmedBasisUnsupported", c.geometry, Status::Unsupported);
        }
        const auto a = points[c.v0], b = points[c.v1];
        ParameterMapping mapping;
        mapping.sourceGeometryId = Id(g);
        mapping.sourceUseId = Id(c.record);
        mapping.targetEdgeIndex = ei.at(c.record);
        if (Is(g, "LINE"))
        {
            const auto origin = Position(Ref(g, "pnt"));
            const auto vector = Ref(g, "dir");
            Need(N(vector, "magnitude") > 0, "GeneralDegenerateLine", g);
            const auto d = Direction(Ref(vector, "orientation"));
            const auto t0 = Dot(Sub(a, origin), d), t1 = Dot(Sub(b, origin), d);
            Need(t1 > t0, "GeneralLineParameterOrder", c.record);
            mapping.kind = "LineAffineParameter";
            mapping.equation = "source_t=lerp(source_parameter_at_native_ends,(native_t-native_begin)/(native_end-native_begin))";
            mapping.parameters.push_back(
                { "source_parameter_at_native_ends", { t0 / (N(vector, "magnitude") * scale), t1 / (N(vector, "magnitude") * scale) } }
            );
            SmLine* line = nullptr;
            Step(g);
            Good(SmLine::CreateLineSegment(*m_model->context, 3, P(Add(origin, Mul(d, t0))), P(Add(origin, Mul(d, t1))), line));
            c.native.reset(line);
        }
        else if (Is(g, "CIRCLE") || Is(g, "ELLIPSE"))
        {
            const auto frame = Placement(Ref(g, "position"));
            const bool circle = Is(g, "CIRCLE");
            const auto rx = N(g, circle ? "radius" : "semi_axis_1") * scale;
            const auto ry = circle ? rx : N(g, "semi_axis_2") * scale;
            if (trimmed && (!(rx > SM_EFF_ZERO) || !(ry > SM_EFF_ZERO) || !std::isfinite(rx) || !std::isfinite(ry)))
            {
                Fail("GeneralTrimmedConicRadiusRange", g, Status::Unsupported);
            }
            const auto angle = [&](V p)
            {
                const auto delta = Sub(p, frame.o);
                return std::atan2(Dot(delta, frame.y) / ry, Dot(delta, frame.x) / rx);
            };
            double t0 = angle(a), t1 = angle(b);
            std::optional<Index> trimMapping;
            if (c.v0 == c.v1)
            {
                t1 = t0 + 2 * std::numbers::pi;
            }
            else
            {
                while (t1 <= t0)
                {
                    t1 += 2 * std::numbers::pi;
                }
            }
            if (trimmed)
            {
                constexpr double tau = 2 * std::numbers::pi;
                const auto trim = TrimmedConic(c.geometry, frame, rx, ry, consistency);
                if (c.v0 == c.v1)
                {
                    Need(trim.full, "GeneralTrimmedClosedEdgeRequiresFullConic", c.record);
                }
                else if (!trim.full)
                {
                    // Resolve each EC endpoint inside the source trim's
                    // increasing BASIS interval. The topological edge may use
                    // a subset; it cannot extend or ignore the wrapper limits.
                    const auto inside = [&](double t)
                    {
                        // atan2 and source decimal conversion can straddle a
                        // cut by a few ulps. Select the adjacent periodic branch
                        // before bounding that inversion to the ORIGINAL cut.
                        // This roundoff envelope is independent of quality tol;
                        // the usual native endpoint/vertex gap check below must
                        // still pass in mm, including for very large radii.
                        const double roundoff = 32 * std::numeric_limits<double>::epsilon() *
                                                std::max({ 1., std::abs(trim.low), std::abs(trim.high) });
                        double candidate = t + tau * std::ceil((trim.low - t - roundoff) / tau);
                        if (candidate < trim.low && candidate >= trim.low - roundoff)
                        {
                            candidate = trim.low;
                        }
                        if (candidate > trim.high && candidate <= trim.high + roundoff)
                        {
                            candidate = trim.high;
                        }
                        return candidate;
                    };
                    t0 = inside(angle(a));
                    t1 = inside(angle(b));
                    Need(t0 >= trim.low && t1 <= trim.high && t1 > t0, "GeneralTrimmedEdgeOutsideSourceDomain", c.record);
                }
                ParameterMapping trace;
                trace.kind = "TrimmedConicSourceDomain";
                trace.sourceGeometryId = Id(c.geometry);
                trace.sourceBasisGeometryId = Id(g);
                trace.sourceUseId = Id(c.record);
                trace.targetEdgeIndex = ei.at(c.record);
                trace.equation =
                    "canonical_trim_s_rad=sign*(basis_angle_rad-trim_1_rad); periodic_branch_when_full; "
                    "source_s=canonical_trim_s_rad/angle_parameter_to_rad_when_established; native_t_is_rational_not_angle; preferred=" +
                    F(c.geometry, "master_representation").text;
                trace.parameters = { { "basis_trim_rad", { trim.first, trim.last } },
                                     { "canonical_trim_parameter_range_rad", { 0, trim.high - trim.low } },
                                     { "source_sense", { B(c.geometry, "sense_agreement") ? 1. : -1. } },
                                     { "edge_basis_angle_branch_rad", { t0, t1 } } };
                if (trim.angleScale)
                {
                    trace.parameters.push_back({ "angle_parameter_to_rad", { *trim.angleScale } });
                }
                trimMapping = m_report.parameterMappings.size();
                m_report.parameterMappings.push_back(std::move(trace));
            }
            // A whole-period shift preserves the curve; kernel analytic angles
            // are degrees in [-360,360], while evaluators use a NURBS parameter.
            const auto sourceBranchBegin = t0;
            if (trimmed)
            {
                // Trimming can lift either endpoint several periods in either
                // direction. Bring the beginning into [0,2pi) before choosing
                // the kernel chart; the original source branch stays recorded.
                const double shift = 2 * std::numbers::pi * std::floor(t0 / (2 * std::numbers::pi));
                t0 -= shift;
                t1 -= shift;
            }
            if (t1 > 2 * std::numbers::pi)
            {
                t0 -= 2 * std::numbers::pi;
                t1 -= 2 * std::numbers::pi;
            }
            if (trimMapping)
            {
                auto& trace = m_report.parameterMappings[*trimMapping];
                trace.parameters.push_back({ "native_chart_period_shift_rad", { sourceBranchBegin - t0 } });
                trace.parameters.push_back({ "native_chart_angle_range_rad", { t0, t1 } });
            }
            mapping.kind = "ConicRationalParameter";
            mapping.equation =
                "source_angle=atan2(dot(P(native_t)-origin,y)/ry,dot(P(native_t)-origin,x)/rx),unwrapped_to_angle_range_rad; native_t_is_not_angle";
            mapping.parameters = { { "angle_range_rad", { t0, t1 } },
                                   { "radii_mm", { rx, ry } },
                                   { "origin_mm", { frame.o.begin(), frame.o.end() } },
                                   { "x", { frame.x.begin(), frame.x.end() } },
                                   { "y", { frame.y.begin(), frame.y.end() } } };
            const auto degrees = c.v0 == c.v1 ? FullPeriodDegrees(t0) :
                                                std::array<double, 2>{ t0 * 180 / std::numbers::pi, t1 * 180 / std::numbers::pi };
            const SmExtent1d domain(degrees[0], degrees[1]);
            if (trimmed && (!(domain.GetMax() > domain.GetMin()) || domain.GetMin() < -360 || domain.GetMax() > 360))
            {
                Fail("GeneralTrimmedNativeAngleRangeUnestablished", c.geometry, Status::Unsupported);
            }
            c.native = MakeConic(g, frame, rx, ry, domain, trimmed);
        }
        else if (Is(g, "B_SPLINE_CURVE_WITH_KNOTS"))
        {
            c.native = MakeSplineCurve(g, c.record);
        }
        else
        {
            Fail("GeneralUnsupportedCurve", g, Status::Unsupported);
        }
        Need(bool(c.native), "GeneralCurveAllocation", g);
        const auto domain = c.native->GetNaturalInterval();
        c.t0 = domain.GetMin();
        c.t1 = domain.GetMax();
        if (Is(g, "B_SPLINE_CURVE_WITH_KNOTS"))
        {
            // Source topology trims the defining spline. Inversion chooses
            // parameters only; the original spline coefficients are unchanged.
            mapping.kind = "SplineIdentityWithInvertedTrim";
            mapping.equation = "source_t=native_t; endpoint_inversion_only; defining_coefficients_unchanged";
            c.t0 = CurveParameter(*c.native, a, c.t0, g);
            c.t1 = CurveParameter(*c.native, b, c.t1, g);
            Need(c.t1 > c.t0, "GeneralSplineParameterOrder", c.record);
        }
        mapping.parameters.push_back({ "native_natural_interval", { domain.GetMin(), domain.GetMax() } });
        mapping.parameters.push_back({ "native_edge_interval", { c.t0, c.t1 } });
        m_report.parameterMappings.push_back(std::move(mapping));
        for (const auto& endpoint : { std::pair{ c.t0, a }, std::pair{ c.t1, b } })
        {
            SmPoint3d actual;
            Step(c.record);
            Good(c.native->EvaluatePoint(endpoint.first, actual));
            if (endpoint.first == c.t0)
            {
                c.p0 = P(actual);
            }
            else
            {
                c.p1 = P(actual);
            }
            const auto gap = Norm(Sub(P(actual), endpoint.second));
            consistency = std::max(consistency, gap);
            Need(std::isfinite(gap) && gap <= Q(m_options.quality.consistencyMm), "GeneralEdgeVertexGap", c.record);
        }
        SmExtent3d box;
        Step(g);
        Good(c.native->CalculateBoundingBox(SmExtent1d(c.t0, c.t1), &box));
        c.bounds = { P(box.GetMin()), P(box.GetMax()) };
    }
    double CurveParameter(SmCurve& curve, V point, double endpoint, Index record)
    {
        SmPoint3d actual;
        Step(record);
        Good(curve.EvaluatePoint(endpoint, actual));
        if (Norm(Sub(P(actual), point)) <= Q(m_options.quality.consistencyMm))
        {
            return endpoint;
        }
        SmBoolean success = FALSE;
        double t = 0, distance = 0;
        Step(record);
        Good(curve.DropPoint(curve.GetNaturalInterval(), P(point), nullptr, m_model->queryTolerance, nullptr, success, t, distance));
        Need(success && distance <= Q(m_options.quality.consistencyMm), "GeneralSplineEndpointInversion", record);
        return t;
    }
    ULONG Degree(Index a, std::string_view name) const
    {
        const double n = N(a, name);
        Need(n >= 1 && n <= 32 && std::floor(n) == n, "GeneralSplineDegree", a);
        return static_cast<ULONG>(n);
    }
    SmTArray<ULONG> Multiplicities(Index a, std::string_view name) const
    {
        SmTArray<ULONG> result;
        for (const auto& value : F(a, name).children)
        {
            const auto n = Number(value);
            Need(n >= 1 && n <= 33 && std::floor(n) == n, "GeneralKnotMultiplicity", a);
            result.Add(static_cast<ULONG>(n));
        }
        return result;
    }
    SmTArray<double> Numbers(Index a, std::string_view name) const
    {
        SmTArray<double> result;
        for (const auto& value : F(a, name).children)
        {
            result.Add(Number(value));
        }
        return result;
    }
    SmTArray<double> SplineWeights(Index geometry, bool surface, const SmTArray<SmPoint3d>& control) const
    {
        SmTArray<double> result;
        if (!Is(geometry, surface ? "RATIONAL_B_SPLINE_SURFACE" : "RATIONAL_B_SPLINE_CURVE"))
        {
            return result;
        }
        const auto& values = F(geometry, "weights_data");
        const auto append = [&](const step_source::Value& value)
        {
            const double weight = Number(value);
            Need(weight > 0, "GeneralWeightRangeUnestablished", geometry);
            result.Add(weight);
        };
        // STEP Cartesian controls are Euclidean, weights dimensionless. Preserve
        // source U rows / V entries and pass weights separately: CreateCanonical
        // performs the one conversion to homogeneous coordinates itself.
        for (const auto& value : values.children)
        {
            if (surface)
            {
                for (const auto& weight : value.children)
                {
                    append(weight);
                }
            }
            else
            {
                append(value);
            }
        }
        Need(result.GetSize() == control.GetSize(), "GeneralWeightNetShape", geometry);
        for (ULONG i = 0; i < result.GetSize(); ++i)
        {
            for (const auto coordinate : P(control[i]))
            {
                const double homogeneous = coordinate * result[i];
                Need(std::isfinite(homogeneous) && (coordinate == 0 || homogeneous != 0), "GeneralHomogeneousRangeUnestablished", geometry);
            }
        }
        return result;
    }
    void VerifyRationalNet(
        Index geometry,
        Index sourceUse,
        const SmTArray<SmPoint3d>& control,
        const SmTArray<double>& weights,
        const double* net,
        ULONG count,
        bool rational
    ) const
    {
        Need(net && count == control.GetSize(), "GeneralRationalNetShapeChanged", geometry);
        // Only an identical positive source weight at EVERY control can be
        // cancelled to one without changing geometry or parameterization.
        // Require literal equality as a sufficient exact source proof; binary64
        // equality alone could hide distinct source decimals rounding together.
        const auto& sourceWeights = F(geometry, "weights_data");
        const bool surface = Is(geometry, "RATIONAL_B_SPLINE_SURFACE");
        const auto& first = surface ? sourceWeights.children.front().children.front() : sourceWeights.children.front();
        bool uniformSource = true;
        for (const auto& value : sourceWeights.children)
        {
            if (surface)
            {
                for (const auto& child : value.children)
                {
                    uniformSource = uniformSource && child.text == first.text;
                }
            }
            else
            {
                uniformSource = uniformSource && value.text == first.text;
            }
        }
        bool cancelled = false;
        // SMLib may also discard near-unity NONuniform surface weights. Reject
        // that unqualified normalization; raw source weights remain unchanged.
        // Stored nonrational W is a sentinel, so interpret it as exactly one.
        for (ULONG i = 0; i < count; ++i)
        {
            const double actualWeight = rational ? net[4 * i + 3] : 1.;
            if (actualWeight != weights[i])
            {
                Need(uniformSource && !rational && actualWeight == 1., "GeneralSourceWeightsChanged", geometry);
                cancelled = true;
            }
            V actual{};
            for (Index axis = 0; axis < 3; ++axis)
            {
                actual[axis] = net[4 * i + axis] / actualWeight;
                Need(std::isfinite(actual[axis]), "GeneralHomogeneousRangeUnestablished", geometry);
            }
            Need(Norm(Sub(actual, P(control[i]))) <= Q(m_options.quality.definitionMm), "GeneralControlRoundtripUnestablished", geometry);
        }
        if (cancelled)
        {
            ParameterMapping trace;
            trace.kind = "ExactUniformWeightCancellation";
            trace.sourceGeometryId = Id(geometry);
            trace.sourceUseId = Id(sourceUse);
            trace.equation = "identical positive source weights cancel from numerator/denominator; source parameters and Euclidean controls unchanged";
            trace.parameters = { { "source_uniform_weight", { weights[0] } }, { "native_uniform_weight", { 1. } } };
            m_report.parameterMappings.push_back(std::move(trace));
        }
    }
    std::optional<SeamPlan> MissingSeam(const Face& f) const
    {
        if (!Is(f.surface, "CYLINDRICAL_SURFACE") || f.loops.size() != 2)
        {
            return {};
        }
        for (const auto& loop : f.loops)
        {
            if (loop.uses.size() != 1)
            {
                return {};
            }
            const auto& c = curves[loop.uses[0].edge];
            if (!Is(c.geometry, "CIRCLE") || c.v0 != c.v1)
            {
                return {};
            }
        }
        const auto a = curves[f.loops[0].uses[0].edge].v0, b = curves[f.loops[1].uses[0].edge].v0;
        const auto frame = Placement(Ref(f.surface, "position"));
        const auto radius = N(f.surface, "radius") * scale;
        const auto da = Sub(points[a], frame.o), db = Sub(points[b], frame.o);
        const auto z0 = Dot(da, frame.z), z1 = Dot(db, frame.z);
        const auto radial = Unit(Sub(da, Mul(frame.z, z0)));
        const auto pa = Add(Add(frame.o, Mul(frame.z, z0)), Mul(radial, radius));
        const auto pb = Add(Add(frame.o, Mul(frame.z, z1)), Mul(radial, radius));
        // A target chart cut is an exact isoparametric line on the defining
        // cylinder. It reuses source ring vertices without moving them. Rings
        // with incompatible seam vertices need a separately verified split.
        if (Norm(Sub(pa, points[a])) > Q(m_options.quality.consistencyMm) || Norm(Sub(pb, points[b])) > Q(m_options.quality.consistencyMm))
        {
            Fail("GeneralImplicitSeamRequiresRingSplit", f.record, Status::Unsupported);
        }
        Need(std::abs(z1 - z0) > Tol(), "GeneralImplicitSeamHeight", f.record);
        return SeamPlan{ a, b, pa, pb, std::atan2(Dot(radial, frame.y), Dot(radial, frame.x)) };
    }
    std::array<double, 2> AngularWindow(const Face& f, const Frame& frame)
    {
        constexpr double tau = 2 * std::numbers::pi;
        if (const auto seam = MissingSeam(f))
        {
            const double start = seam->angle > 0 ? seam->angle - tau : seam->angle;
            return FullPeriodDegrees(start);
        }
        // STEP may already represent a periodic seam by two opposite uses of
        // one generatrix. Align the analytic domain to that exact source edge;
        // introducing an unrelated zero-angle seam would invalidate the trim.
        std::map<Index, Index> useCounts;
        for (const auto& loop : f.loops)
        {
            for (const auto& use : loop.uses)
            {
                ++useCounts[use.edge];
            }
        }
        for (const auto& [edge, count] : useCounts)
        {
            const auto& c = curves[edge];
            if (count == 2 && Is(c.geometry, "LINE"))
            {
                const auto a = Sub(c.p0, frame.o), b = Sub(c.p1, frame.o);
                const double t0 = std::atan2(Dot(a, frame.y), Dot(a, frame.x));
                const double t1 = std::atan2(Dot(b, frame.y), Dot(b, frame.x));
                if (std::abs(std::remainder(t0 - t1, tau)) > 1e-10)
                {
                    continue;
                }
                const double start = t0 > 0 ? t0 - tau : t0;
                return FullPeriodDegrees(start);
            }
        }
        bool splineBoundary = false;
        std::vector<double> meridianAngles;
        double maximumRadius = 0;
        for (const auto& [edge, count] : useCounts)
        {
            (void)count;
            const auto& c = curves[edge];
            splineBoundary = splineBoundary || Is(c.geometry, "B_SPLINE_CURVE_WITH_KNOTS");
            if (!Is(c.geometry, "LINE"))
            {
                continue;
            }
            for (const auto vertex : { c.v0, c.v1 })
            {
                const auto p = Sub(points[vertex], frame.o);
                const double x = Dot(p, frame.x), y = Dot(p, frame.y);
                meridianAngles.push_back(std::atan2(y, x));
                maximumRadius = std::max(maximumRadius, std::hypot(x, y));
            }
        }
        if (splineBoundary && meridianAngles.size() >= 4)
        {
            double x = 0, y = 0;
            for (const auto angle : meridianAngles)
            {
                x += std::cos(angle);
                y += std::sin(angle);
            }
            const double center = std::atan2(y, x);
            double spread = 0;
            for (const auto angle : meridianAngles)
            {
                spread = std::max(spread, std::abs(std::remainder(angle - center, tau)));
            }
            // A tolerant source may encode opposite seam sides as distinct
            // generatrices near one meridian. Align only the target chart to a
            // candidate inside their bounded angular spread. The 3D definitions
            // stay unchanged; UV deviation and the native seam checks still gate
            // publication. This does not weld the distinct source edges.
            if (maximumRadius > 0 && spread * maximumRadius <= Q(m_options.quality.derivedUvMm) / 4)
            {
                const double start = center > 0 ? center - tau : center;
                return FullPeriodDegrees(start);
            }
        }
        std::vector<std::pair<double, double>> cover;
        const auto normalized = [](double t)
        {
            double result = std::fmod(t, tau);
            return result < 0 ? result + tau : result;
        };
        for (const auto& loop : f.loops)
        {
            for (const auto& use : loop.uses)
            {
                const auto& c = curves[use.edge];
                if (Is(c.geometry, "CIRCLE"))
                {
                    const auto cr = Placement(Ref(c.geometry, "position"));
                    const auto dc = Sub(cr.o, frame.o);
                    const double parallel = Dot(cr.z, frame.z);
                    // Closed-form angular coverage applies only to coaxial circular
                    // arcs. Other intersections retain a full period and require a
                    // separate seam-normalization proof; sampling is not a proof.
                    if (std::abs(std::abs(parallel) - 1) > 1e-12 ||
                        Norm(Sub(dc, Mul(frame.z, Dot(dc, frame.z)))) > Q(m_options.quality.consistencyMm))
                    {
                        return { 0, 360 };
                    }
                    const auto a = Sub(points[c.v0], cr.o), b = Sub(points[c.v1], cr.o);
                    double t0 = std::atan2(Dot(a, cr.y), Dot(a, cr.x));
                    double t1 = std::atan2(Dot(b, cr.y), Dot(b, cr.x));
                    if (c.v0 == c.v1)
                    {
                        return { 0, 360 };
                    }
                    while (t1 <= t0)
                    {
                        t1 += tau;
                    }
                    const double offset = std::atan2(Dot(cr.x, frame.y), Dot(cr.x, frame.x));
                    const double start = normalized(offset + (parallel > 0 ? t0 : -t1)), length = t1 - t0;
                    if (start + length <= tau)
                    {
                        cover.push_back({ start, start + length });
                    }
                    else
                    {
                        cover.push_back({ start, tau });
                        cover.push_back({ 0, start + length - tau });
                    }
                }
                else if (Is(c.geometry, "LINE"))
                {
                    const auto a = Sub(points[c.v0], frame.o), b = Sub(points[c.v1], frame.o);
                    const double t0 = normalized(std::atan2(Dot(a, frame.y), Dot(a, frame.x)));
                    const double t1 = normalized(std::atan2(Dot(b, frame.y), Dot(b, frame.x)));
                    const double lower = std::min(t0, t1), upper = std::max(t0, t1);
                    const double start = upper - lower <= std::numbers::pi ? lower : upper;
                    const double span = std::min(upper - lower, tau - (upper - lower));
                    // A projected line lies in the convex angular cone spanned by
                    // its endpoints when that cone is narrower than pi. A tolerant
                    // source generatrix need not have identical endpoint angles.
                    if (span >= std::numbers::pi - 1e-10)
                    {
                        return { 0, 360 };
                    }
                    if (start + span <= tau)
                    {
                        cover.push_back({ start, start + span });
                    }
                    else
                    {
                        cover.push_back({ start, tau });
                        cover.push_back({ 0, start + span - tau });
                    }
                }
                else if (Is(c.geometry, "B_SPLINE_CURVE_WITH_KNOTS"))
                {
                    const auto curve = c.native ? c.native.get() : m_model->edges[use.edge]->GetCurve();
                    const auto bsp = dynamic_cast<const SmBSplineCurve*>(curve);
                    Need(bsp != nullptr, "GeneralSplineRepresentation", c.geometry);
                    const auto knots = Numbers(c.geometry, "knots");
                    for (ULONG k = 1; k < knots.GetSize(); ++k)
                    {
                        const double lo = knots[k - 1], hi = knots[k];
                        // Query strictly inside one knot span: GetPolygon returns
                        // its complete contributing control hull, not sampled curve
                        // points. A cone of width <pi is convex, hence bounds every
                        // point of this nonrational span including its endpoints.
                        const double a = lo + (hi - lo) * 0.25, b = lo + (hi - lo) * 0.75;
                        Need(a > lo && b < hi && b > a, "GeneralAngularHullPrecision", c.geometry);
                        SmTArray<SmPoint3d> polygon;
                        Step(c.geometry);
                        Good(bsp->GetPolygon(SmExtent1d(a, b), polygon));
                        std::vector<double> angles;
                        for (ULONG n = 0; n < polygon.GetSize(); ++n)
                        {
                            const auto p = Sub(P(polygon[n]), frame.o);
                            const double x = Dot(p, frame.x), y = Dot(p, frame.y);
                            if (std::hypot(x, y) <= Q(m_options.quality.definitionMm))
                            {
                                return { 0, 360 };
                            }
                            angles.push_back(normalized(std::atan2(y, x)));
                        }
                        std::sort(angles.begin(), angles.end());
                        if (angles.empty())
                        {
                            return { 0, 360 };
                        }
                        double gap = -1, start = 0;
                        for (Index n = 0; n < angles.size(); ++n)
                        {
                            const auto next = angles[(n + 1) % angles.size()] + (n + 1 == angles.size() ? tau : 0);
                            if (next - angles[n] > gap)
                            {
                                gap = next - angles[n];
                                start = normalized(next);
                            }
                        }
                        const double span = tau - gap;
                        if (span >= std::numbers::pi - 1e-10)
                        {
                            return { 0, 360 };
                        }
                        if (start + span <= tau)
                        {
                            cover.push_back({ start, start + span });
                        }
                        else
                        {
                            cover.push_back({ start, tau });
                            cover.push_back({ 0, start + span - tau });
                        }
                    }
                }
                else
                {
                    return { 0, 360 };
                }
            }
        }
        if (cover.empty())
        {
            return { 0, 360 };
        }
        std::sort(cover.begin(), cover.end());
        std::vector<std::pair<double, double>> merged;
        for (const auto& arc : cover)
        {
            if (merged.empty() || arc.first > merged.back().second + 1e-12)
            {
                merged.push_back(arc);
            }
            else
            {
                merged.back().second = std::max(merged.back().second, arc.second);
            }
        }
        double gap = -1, start = 0;
        for (Index i = 0; i < merged.size(); ++i)
        {
            const double next = merged[(i + 1) % merged.size()].first + (i + 1 == merged.size() ? tau : 0);
            const double length = next - merged[i].second;
            if (length > gap)
            {
                gap = length;
                start = normalized(next);
            }
        }
        if (gap <= 1e-10)
        {
            return { 0, 360 };
        }
        // Padding expands a computational window only. The source trims remain
        // unchanged; it adds neither a cap nor material to the imported body.
        start -= 1e-12;
        double end = start + tau - gap + 2e-12;
        if (end > tau)
        {
            start -= tau;
            end -= tau;
        }
        return { start * 180 / std::numbers::pi, end * 180 / std::numbers::pi };
    }
    std::unique_ptr<SmSurface> HelicalCylinderChart(const Face& f, const Frame& frame)
    {
        if (f.loops.size() != 1 || f.loops[0].uses.empty())
        {
            return {};
        }
        Index longEdge = curves.size();
        double pitch = 0, originAngle = 0, reference = 0, guideSweep = 0;
        constexpr double tau = 2 * std::numbers::pi;
        const auto unwrapped = [&](const SmTArray<SmPoint3d>& cp)
        {
            std::vector<double> angles;
            for (ULONG i = 0; i < cp.GetSize(); ++i)
            {
                const auto p = Sub(P(cp[i]), frame.o);
                double a = std::atan2(Dot(p, frame.y), Dot(p, frame.x));
                if (!angles.empty())
                {
                    a = angles.back() + std::remainder(a - angles.back(), tau);
                }
                angles.push_back(a);
            }
            return angles;
        };
        // A chart gauge is not a fitted defining helix. It only changes which
        // cylinder points are covered by a finite computational rectangle.
        for (const auto& use : f.loops[0].uses)
        {
            const auto& c = curves[use.edge];
            if (!Is(c.geometry, "B_SPLINE_CURVE_WITH_KNOTS") && !Is(c.geometry, "LINE") && !Is(c.geometry, "CIRCLE"))
            {
                return {};
            }
            if (!Is(c.geometry, "B_SPLINE_CURVE_WITH_KNOTS"))
            {
                continue;
            }
            const auto native = c.native ? c.native.get() : m_model->edges[use.edge]->GetCurve();
            const auto bsp = dynamic_cast<const SmBSplineCurve*>(native);
            if (!bsp)
            {
                return {};
            }
            SmTArray<SmPoint3d> cp;
            SmTArray<double> weights;
            Good(bsp->GetControlPolygon(cp, weights));
            if (cp.GetSize() < 2)
            {
                continue;
            }
            const auto angles = unwrapped(cp);
            const double turn = angles.back() - angles.front();
            if (std::abs(turn) < 1.5 * std::numbers::pi)
            {
                continue;
            }
            const double dz = Dot(Sub(P(cp[cp.GetSize() - 1]), P(cp[0])), frame.z);
            const double candidate = dz * tau / turn;
            if (std::abs(candidate) <= 2 * Tol())
            {
                continue;
            }
            if (std::abs(turn) <= guideSweep)
            {
                continue;
            }
            guideSweep = std::abs(turn);
            longEdge = use.edge;
            pitch = candidate;
            originAngle = *std::min_element(angles.begin(), angles.end()) - std::numbers::pi / 18;
        }
        if (longEdge == curves.size())
        {
            return {};
        }
        const auto toU = [&](double angle)
        {
            const double a = angle - originAngle, q = std::floor(a / (std::numbers::pi / 2));
            const double part = a - q * (std::numbers::pi / 2);
            const double left = std::sin(part / 2), right = std::sin((std::numbers::pi / 2 - part) / 2);
            return (q + left / (left + right)) / 4;
        };
        const auto& guide = curves[longEdge];
        const auto guideCurve = dynamic_cast<const SmBSplineCurve*>(guide.native ? guide.native.get() : m_model->edges[longEdge]->GetCurve());
        SmTArray<SmPoint3d> guideCp;
        SmTArray<double> guideWeights;
        Good(guideCurve->GetControlPolygon(guideCp, guideWeights));
        const auto guideAngles = unwrapped(guideCp);
        reference = 0.5 * (Dot(Sub(P(guideCp[0]), frame.o), frame.z) + Dot(Sub(P(guideCp[guideCp.GetSize() - 1]), frame.o), frame.z) -
                           pitch * (toU(guideAngles.front()) + toU(guideAngles.back())));
        double lower = INFINITY, upper = -INFINITY;
        std::array<double, 5> upperByQuarter;
        upperByQuarter.fill(-INFINITY);
        for (const auto& use : f.loops[0].uses)
        {
            const auto& c = curves[use.edge];
            const auto native = c.native ? c.native.get() : m_model->edges[use.edge]->GetCurve();
            const auto curve = dynamic_cast<const SmBSplineCurve*>(native);
            if (!curve)
            {
                return {};
            }
            SmTArray<SmPoint3d> cp;
            SmTArray<double> weights, knots;
            Good(curve->GetControlPolygon(cp, weights));
            Good(curve->GetKnotsAll(knots));
            if (cp.GetSize() == 0)
            {
                return {};
            }
            auto angles = unwrapped(cp);
            double best = INFINITY, shift = 0;
            const auto [amin, amax] = std::minmax_element(angles.begin(), angles.end());
            double za = INFINITY, zb = -INFINITY;
            for (ULONG i = 0; i < cp.GetSize(); ++i)
            {
                const auto z = Dot(Sub(P(cp[i]), frame.o), frame.z);
                za = std::min(za, z);
                zb = std::max(zb, z);
            }
            for (int n = -3; n <= 3; ++n)
            {
                const double offset = n * tau;
                if (*amin + offset < originAngle || *amax + offset > originAngle + 2.5 * std::numbers::pi)
                {
                    continue;
                }
                const double score = std::abs(0.5 * (za + zb - pitch * (toU(*amin + offset) + toU(*amax + offset))) - reference);
                if (score < best)
                {
                    best = score;
                    shift = offset;
                }
            }
            if (!std::isfinite(best))
            {
                return {};
            }
            for (auto& angle : angles)
            {
                angle += shift;
            }
            double expectedAngle = angles.front();
            for (ULONG k = 1; k < knots.GetSize(); ++k)
            {
                const double a = std::max(knots[k - 1], c.t0), b = std::min(knots[k], c.t1);
                if (!(b > a))
                {
                    continue;
                }
                // Exact knot insertion on a private copy extracts one Bezier
                // span. The broad original basis hull can cover nearly half a
                // turn even when its actual span covers only a small arc.
                SmCurve* rawCopy = nullptr;
                Step(c.geometry);
                Good(curve->Copy(*m_model->context, rawCopy));
                std::unique_ptr<SmCurve> copy(rawCopy);
                Need(bool(copy), "GeneralCurveCopyAllocation", c.geometry);
                SmExtent1d interval(a, b);
                Step(c.geometry);
                Good(copy->Trim(interval));
                const auto bezier = dynamic_cast<const SmBSplineCurve*>(copy.get());
                if (!bezier)
                {
                    return {};
                }
                SmTArray<SmPoint3d> hull;
                SmTArray<double> hullWeights;
                Good(bezier->GetControlPolygon(hull, hullWeights));
                auto hullAngles = unwrapped(hull);
                if (hullAngles.empty())
                {
                    return {};
                }
                const double turns = std::nearbyint((expectedAngle - hullAngles.front()) / tau);
                for (auto& angle : hullAngles)
                {
                    angle += turns * tau;
                }
                expectedAngle = hullAngles.back();
                double z0 = INFINITY, z1 = -INFINITY, t0 = INFINITY, t1 = -INFINITY;
                for (ULONG j = 0; j < hull.GetSize(); ++j)
                {
                    const double z = Dot(Sub(P(hull[j]), frame.o), frame.z);
                    z0 = std::min(z0, z);
                    z1 = std::max(z1, z);
                    t0 = std::min(t0, hullAngles[j]);
                    t1 = std::max(t1, hullAngles[j]);
                }
                // Positive-basis control hulls enclose Z and a convex angular
                // cone. Their product interval encloses the complete boundary,
                // not a set of sampled points. Reject an ambiguous angular cone.
                if (t1 - t0 >= std::numbers::pi - 1e-10)
                {
                    return {};
                }
                const double p0 = pitch * toU(t0), p1 = pitch * toU(t1);
                lower = std::min(lower, z0 - std::max(p0, p1));
                upper = std::max(upper, z1 - std::min(p0, p1));
                const double u0 = toU(t0), u1 = toU(t1);
                for (unsigned q = 0; q < 5; ++q)
                {
                    if (u1 >= double(q) / 4 && u0 <= double(q + 1) / 4)
                    {
                        upperByQuarter[q] = std::max(upperByQuarter[q], z1 - std::min(p0, p1));
                    }
                }
            }
        }
        const double padding = 4 * Tol();
        lower -= padding;
        upper += padding;
        const double height = upper - lower;
        if (!(height > 0))
        {
            return {};
        }
        std::array<double, 6> heights;
        heights.fill(height);
        if (height >= std::abs(pitch) - 2 * Tol())
        {
            if (pitch <= 0)
            {
                return {};
            }
            std::array<double, 5> required;
            for (unsigned q = 0; q < 5; ++q)
            {
                required[q] = std::max(8 * Tol(), std::isfinite(upperByQuarter[q]) ? upperByQuarter[q] + padding - lower : 8 * Tol());
            }
            heights[0] = required[0];
            heights[5] = required[4];
            for (unsigned q = 1; q < 5; ++q)
            {
                heights[q] = std::max(required[q - 1], required[q]);
            }
            // Only the first and fifth quarters repeat an angle. With positive
            // pitch, their axial intervals cannot overlap if the first quarter's
            // V width is smaller than one pitch. Widening the last quarter can
            // cover a thread termination without creating a duplicate 3D image.
            if (std::max(heights[0], heights[1]) >= pitch - 2 * Tol())
            {
                return {};
            }
        }
        const auto net = MakeCylinderGaugeNet(
            { frame.o, frame.x, frame.y, frame.z, N(f.surface, "radius") * scale, originAngle, pitch, lower, heights }
        );
        SmTArray<SmPoint3d> control;
        SmTArray<double> weights;
        for (const auto& point : net.pointsMm)
        {
            control.Add(P(point));
        }
        for (const auto weight : net.weights)
        {
            weights.Add(weight);
        }
        SmTArray<ULONG> um, vm;
        SmTArray<double> uk, vk;
        for (unsigned k = 0; k < 6; ++k)
        {
            um.Add(k == 0 || k == 5 ? 4 : 3);
            uk.Add(double(k) / 4);
        }
        vm.Add(2);
        vm.Add(2);
        vk.Add(0);
        vk.Add(1);
        SmBSplineSurface* raw = nullptr;
        Step(f.surface);
        Good(
            SmBSplineSurface::
                CreateCanonical(*m_model->context, 3, 1, control, SM_SF_UNSPECIFIED, um, vm, uk, vk, SM_KT_UNSPECIFIED, &weights, nullptr, raw)
        );
        // Persist enough data to reconstruct the exact gauge and associate it
        // with its source support and target face. No hidden study parameters.
        ParameterMapping mapping;
        mapping.kind = "ExactCylinderHelicalGaugeChart";
        mapping.sourceGeometryId = Id(f.surface);
        mapping.sourceUseId = Id(f.record);
        mapping.targetFaceIndex = m_model->faces.size();
        mapping.equation =
            "native_u=(quarter+t)/4; angle=origin_angle+quarter*pi/2+atan2(2*w*t*(1-t)+t*t,(1-t)*(1-t)+2*w*t*(1-t)),w=sqrt(0.5); z=lower+pitch*native_u+lerp(width[quarter],width[quarter+1],t)*native_v";
        mapping.parameters = { { "origin_angle_rad", { originAngle } },
                               { "pitch_mm_per_native_u", { pitch } },
                               { "lower_mm", { lower } },
                               { "widths_mm", { heights.begin(), heights.end() } },
                               { "origin_mm", { frame.o.begin(), frame.o.end() } },
                               { "x", { frame.x.begin(), frame.x.end() } },
                               { "y", { frame.y.begin(), frame.y.end() } },
                               { "z", { frame.z.begin(), frame.z.end() } },
                               { "radius_mm", { N(f.surface, "radius") * scale } },
                               { "native_domain", { 0, 0, 1.25, 1 } } };
        m_report.parameterMappings.push_back(std::move(mapping));
        RepresentationNormalization normalization;
        normalization.kind = "ExactCylinderHelicalGaugeChart";
        normalization.sourceFaceId = Id(f.record);
        for (const auto& loop : f.loops)
        {
            normalization.sourceBoundIds.push_back(Id(loop.bound));
        }
        m_report.normalizations.push_back(std::move(normalization));
        return std::unique_ptr<SmSurface>(raw);
    }
    std::unique_ptr<SmSurface> Surface(const Face& f)
    {
        const auto g = f.surface;
        if (Is(g, "B_SPLINE_SURFACE_WITH_KNOTS"))
        {
            SmTArray<SmPoint3d> cp;
            // Source rows are U, entries V. SMLib uses the same U-major layout.
            for (const auto& row : F(g, "control_points_list").children)
            {
                for (const auto& ref : row.children)
                {
                    cp.Add(P(Position(m_source->ReferenceTarget(ref))));
                }
            }
            auto um = Multiplicities(g, "u_multiplicities"), vm = Multiplicities(g, "v_multiplicities");
            auto uk = Numbers(g, "u_knots"), vk = Numbers(g, "v_knots");
            auto weights = SplineWeights(g, true, cp);
            SmBSplineSurface* surface = nullptr;
            Step(g);
            Good(
                SmBSplineSurface::CreateCanonical(
                    *m_model->context,
                    Degree(g, "u_degree"),
                    Degree(g, "v_degree"),
                    cp,
                    SM_SF_UNSPECIFIED,
                    um,
                    vm,
                    uk,
                    vk,
                    SM_KT_UNSPECIFIED,
                    weights.GetSize() ? &weights : nullptr,
                    nullptr,
                    surface
                )
            );
            std::unique_ptr<SmSurface> owned(surface);
            if (weights.GetSize())
            {
                Need(bool(surface), "GeneralSurfaceAllocation", g);
                ULONG u = 0, v = 0;
                double* net = nullptr;
                Good(surface->GetControlPointsPointer(u, v, net));
                Need(
                    u == F(g, "control_points_list").children.size() && v == F(g, "control_points_list").children[0].children.size(),
                    "GeneralRationalNetShapeChanged",
                    g
                );
                VerifyRationalNet(g, f.record, cp, weights, net, u * v, surface->IsRational());
            }
            return owned;
        }
        const auto frame = Placement(Ref(g, "position"));
        // Finite computational windows must cover the entire defining boundary,
        // not just vertices (closed circles may have only one source vertex).
        // Project conservative curve boxes; this changes no trim or geometry.
        Bounds local{ V{ INFINITY, INFINITY, INFINITY }, V{ -INFINITY, -INFINITY, -INFINITY } };
        for (const auto& loop : f.loops)
        {
            for (const auto& use : loop.uses)
            {
                const auto& box = curves[use.edge].bounds;
                for (unsigned mask = 0; mask < 8; ++mask)
                {
                    V corner;
                    for (Index k = 0; k < 3; ++k)
                    {
                        corner[k] = box[(mask >> k) & 1U][k];
                    }
                    const auto d = Sub(corner, frame.o);
                    const V p{ Dot(d, frame.x), Dot(d, frame.y), Dot(d, frame.z) };
                    for (Index k = 0; k < 3; ++k)
                    {
                        local[0][k] = std::min(local[0][k], p[k]);
                        local[1][k] = std::max(local[1][k], p[k]);
                    }
                }
            }
        }
        const auto padding = std::max(
            Q(m_options.quality.definitionMm),
            64 * std::numeric_limits<double>::epsilon() * (1 + Norm(local[0]) + Norm(local[1]))
        );
        for (Index k = 0; k < 3; ++k)
        {
            local[0][k] -= padding;
            local[1][k] += padding;
        }
        if (Is(g, "PLANE"))
        {
            Step(g);
            return std::unique_ptr<SmSurface>(new (*m_model->context) SmPlane(
                P(frame.o),
                D(frame.x),
                D(frame.y),
                SmVector2d(1, 1),
                SmExtent2d(local[0][0], local[0][1], local[1][0], local[1][1]),
                m_model->context.get()
            ));
        }
        if (Is(g, "CYLINDRICAL_SURFACE"))
        {
            if (auto chart = HelicalCylinderChart(f, frame))
            {
                return chart;
            }
            const auto angles = AngularWindow(f, frame);
            Step(g);
            return std::unique_ptr<SmSurface>(new (*m_model->context) SmCylinder(
                P(Add(frame.o, Mul(frame.z, local[0][2]))),
                D(frame.x),
                D(frame.y),
                N(g, "radius") * scale,
                angles[0],
                angles[1],
                local[1][2] - local[0][2],
                FALSE,
                FALSE,
                FALSE,
                m_model->context.get()
            ));
        }
        if (Is(g, "CONICAL_SURFACE"))
        {
            int anglePower = 0;
            bool found = false;
            for (const auto& unit : m_source->UnitContext().siDefinitions)
            {
                if (unit.recordIndex == m_source->UnitContext().planeAngleUnit)
                {
                    found = unit.siName == "RADIAN";
                    anglePower = unit.canonicalPower10;
                }
            }
            Need(found, "GeneralConeAngleUnitUnestablished", g);
            const double angle = N(g, "semi_angle") * std::pow(10., anglePower);
            // The kernel cone's V is axial height, STEP's V is generatrix
            // distance. Physical construction uses radius/angle/frame directly;
            // no identity V mapping is asserted in the source report.
            Step(g);
            const double radius = N(g, "radius") * scale, slope = std::tan(angle);
            if (slope > 0)
            {
                local[0][2] = std::max(local[0][2], -radius / slope);
            }
            if (slope < 0)
            {
                local[1][2] = std::min(local[1][2], -radius / slope);
            }
            auto shifted = frame;
            shifted.o = Add(frame.o, Mul(frame.z, local[0][2]));
            const auto angles = AngularWindow(f, frame);
            SmCone* raw = nullptr;
            Good(
                SmCone::CreateCanonical(
                    *m_model->context,
                    shifted.Native(),
                    std::max(0., radius + slope * local[0][2]),
                    std::max(0., radius + slope * local[1][2]),
                    local[1][2] - local[0][2],
                    raw
                )
            );
            std::unique_ptr<SmCone> cone(raw);
            Need(bool(cone), "GeneralConeAllocation", g);
            const auto domain = cone->GetSTEPUVDomain();
            Step(g);
            Good(cone->AdjustSTEPUVDomain(SmExtent2d(angles[0], domain.GetMin().y, angles[1], domain.GetMax().y)));
            return cone;
        }
        if (Is(g, "TOROIDAL_SURFACE"))
        {
            const double major = N(g, "major_radius") * scale, minor = N(g, "minor_radius") * scale;
            // This native slice handles finite nonsingular ring tori only.
            // Horn/spindle surfaces are valid source TOROIDAL_SURFACE values;
            // refusing their native construction is not an EXPRESS violation.
            if (!(std::isfinite(major) && std::isfinite(minor) && minor > 0 && major > minor))
            {
                Fail("GeneralToroidalRingRequired", g, Status::Unsupported);
            }
            // The checked factory rejects radii at/below SM_EFF_ZERO. Its
            // direct constructor assumes a successful generating-circle
            // allocation and cannot safely handle that source-positive range.
            if (minor <= SM_EFF_ZERO || major <= SM_EFF_ZERO)
            {
                Fail("GeneralToroidalKernelRadiusRange", g, Status::Unsupported);
            }
            // SmTorus's analytic chart uses degrees; its EvaluatePoint takes
            // the nonlinearly related NURBS UV. No source parameter is changed
            // or claimed identical to target UV. Existing source seams/edges
            // remain defining geometry; installed UV must pass the same gates.
            const SmExtent2d domain(0, 0, 360, 360);
            SmTorus* raw = nullptr;
            Step(g);
            const auto status = SmTorus::CreateCanonical(*m_model->context, frame.Native(), major, minor, raw, FALSE, FALSE, nullptr, &domain);
            std::unique_ptr<SmTorus> torus(raw);
            Good(status);
            Need(torus && torus->GetMajorRadius() == major && torus->GetMinorRadius() == minor, "GeneralToroidalDefinitionChanged", g);
            return torus;
        }
        Fail("GeneralUnsupportedSurface", g, Status::Unsupported);
    }
    void AppendSourceUse(SmBrepConstructor& c, const Use& u, LoopMapping& mapping)
    {
        auto& edge = curves[u.edge];
        const auto existing = m_model->edges[u.edge];
        Step(edge.record);
        auto native = c.StartEdge(u.forward ? SM_OT_SAME : SM_OT_OPPOSITE, existing, m_model->vertices[edge.v0], m_model->vertices[edge.v1]);
        Need(native != nullptr && (!existing || existing == native), "GeneralSharedEdgeIdentity", edge.record);
        if (!existing)
        {
            native->SetTolerance(Tol(), FALSE);
            m_model->edges[u.edge] = native;
            Step(edge.record);
            const auto adoptedCurve = c.SetEdgeCurve(edge.native.get(), FALSE);
            const bool curveOwned = edge.native->GetOwner() == native;
            if (curveOwned)
            {
                edge.native.release();
            }
            Good(adoptedCurve);
            Need(curveOwned, "GeneralCurveOwnership", edge.record);
            Step(edge.record);
            Good(c.SetLimits(SmVertexNode(edge.t0), SmVertexNode(edge.t1)));
        }
        mapping.uses.push_back(
            { Id(u.source),
              Id(edge.record),
              Id(vertices[u.forward ? edge.v0 : edge.v1]),
              Id(vertices[u.forward ? edge.v1 : edge.v0]),
              u.forward,
              false,
              {},
              {} }
        );
        Step(edge.record);
        Good(c.EndEdge());
    }
    SmEdge* AppendSeamUse(SmBrepConstructor& c, const SeamPlan& seam, Index face, SmEdge* existing, bool forward)
    {
        Step(face);
        auto edge = c.StartEdge(forward ? SM_OT_SAME : SM_OT_OPPOSITE, existing, m_model->vertices[seam.v0], m_model->vertices[seam.v1]);
        Need(edge != nullptr && (!existing || edge == existing), "GeneralTargetSeamIdentity", face);
        if (!existing)
        {
            edge->SetTolerance(Tol(), FALSE);
            SmLine* raw = nullptr;
            Step(face);
            Good(SmLine::CreateLineSegment(*m_model->context, 3, P(seam.a), P(seam.b), raw));
            std::unique_ptr<SmLine> curve(raw);
            Step(face);
            const auto status = c.SetEdgeCurve(curve.get(), FALSE);
            const bool adopted = curve->GetOwner() == edge;
            if (adopted)
            {
                curve.release();
            }
            Good(status);
            Need(adopted, "GeneralTargetSeamOwnership", face);
            Step(face);
            Good(c.SetLimits(SmVertexNode(0.), SmVertexNode(1.)));
            m_model->edges.push_back(edge);
            derivedEdgeFaces.push_back(face);
        }
        Step(face);
        Good(c.EndEdge());
        return edge;
    }
    void BuildFace(SmBrepConstructor& c, Face& f)
    {
        if (std::none_of(
                f.loops.begin(),
                f.loops.end(),
                [](const Loop& loop)
                {
                    return loop.outer;
                }
            ))
        {
            // Bootstrap the kernel's first-loop representation with a candidate
            // envelope. This is not a region decision: ClassifyRegion must prove
            // unique containment and orientation before any model can publish.
            const auto envelope = [&](const Loop& loop)
            {
                Bounds box{ V{ INFINITY, INFINITY, INFINITY }, V{ -INFINITY, -INFINITY, -INFINITY } };
                for (const auto& use : loop.uses)
                {
                    for (Index k = 0; k < 3; ++k)
                    {
                        box[0][k] = std::min(box[0][k], curves[use.edge].bounds[0][k]);
                        box[1][k] = std::max(box[1][k], curves[use.edge].bounds[1][k]);
                    }
                }
                return Norm(Sub(box[1], box[0]));
            };
            const auto candidate = std::max_element(
                f.loops.begin(),
                f.loops.end(),
                [&](const Loop& a, const Loop& b)
                {
                    return envelope(a) < envelope(b);
                }
            );
            std::rotate(f.loops.begin(), candidate, candidate + 1);
        }
        NativeFaceData nf;
        nf.faceId = Id(f.record);
        Step(f.record);
        nf.face = c.StartFace(f.same ? SM_OT_SAME : SM_OT_OPPOSITE);
        Need(nf.face != nullptr, "GeneralFaceAllocation", f.record);
        nf.face->SetTolerance(Tol(), FALSE);
        auto surface = Surface(f);
        Need(bool(surface), "GeneralSurfaceAllocation", f.surface);
        const auto domain = surface->GetNaturalUVDomain();
        Step(f.surface);
        const auto adoptedSurface = c.SetFaceSurface(surface.get(), domain);
        const bool surfaceOwned = surface->GetOwner() == nf.face;
        if (surfaceOwned)
        {
            surface.release();
        }
        Good(adoptedSurface);
        Need(surfaceOwned, "GeneralSurfaceOwnership", f.record);
        if (const auto seam = MissingSeam(f))
        {
            // Cutting a cylindrical annulus opens its chart into one loop. Both
            // source rings remain defining boundaries; the one derived seam is
            // used twice in opposite directions and contributes no material.
            Step(f.record);
            auto loop = c.StartLoop(SM_OT_SAME);
            Need(loop != nullptr, "GeneralTargetSeamLoop", f.record);
            nf.loops.push_back(loop);
            LoopMapping mapping;
            AppendSourceUse(c, f.loops[0].uses[0], mapping);
            const auto seamIndex = m_model->edges.size();
            auto edge = AppendSeamUse(c, *seam, f.record, nullptr, true);
            AppendSourceUse(c, f.loops[1].uses[0], mapping);
            AppendSeamUse(c, *seam, f.record, edge, false);
            Step(f.record);
            Good(c.EndLoop());
            m_report.normalizations.push_back(
                { "PeriodicCutFromTwoSourceRings",
                  Id(f.record),
                  { Id(f.loops[0].bound), Id(f.loops[1].bound) },
                  { Id(vertices[seam->v0]), Id(vertices[seam->v1]) },
                  seamIndex }
            );
        }
        else
        {
            for (const auto& l : f.loops)
            {
                Step(l.bound);
                auto loop = c.StartLoop(SM_OT_SAME);
                Need(loop != nullptr, "GeneralLoopAllocation", l.bound);
                nf.loops.push_back(loop);
                LoopMapping mapping{ Id(f.record), Id(l.bound), Id(l.record), l.outer, l.orientation, {} };
                for (const auto& u : l.uses)
                {
                    AppendSourceUse(c, u, mapping);
                }
                m_report.loops.push_back(std::move(mapping));
                Step(l.bound);
                Good(c.EndLoop());
            }
        }
        Step(f.record);
        Good(c.EndFace());
        m_model->faces.push_back(std::move(nf));
    }
    void InstallUv()
    {
        for (Index i = 0; i < faces.size(); ++i)
        {
            const auto& f = faces[i];
            auto& native = m_model->faces[i];
            double mean = 0, max = 0, meanVertex = 0, maxVertex = 0, meanParam = 0, maxParam = 0;
            Step(f.record);
            Good(native.face->CreateUVTrimCurves(FALSE, nullptr, nullptr, mean, max, meanVertex, maxVertex, meanParam, maxParam));
            // FALSE forbids tolerance adjustment. Report kernel observations as
            // measurements, never as the planar implementation's interval proof.
            // A failed budget still retains the finite observation; absence of
            // data must remain distinguishable from a measured excess.
            if (std::isfinite(max) && max >= 0)
            {
                m_report.tolerances.push_back({ "UvProjection", "MeasuredCurveSurfaceGap", Id(f.record), max });
            }
            Need(std::isfinite(max) && max >= 0 && max <= Q(m_options.quality.derivedUvMm), "GeneralDerivedUvGap", f.record);
            Need(std::isfinite(maxVertex) && maxVertex >= 0 && maxVertex <= Q(m_options.quality.consistencyMm), "GeneralDerivedVertexGap", f.record);
            uvGap = std::max(uvGap, max);
            // CreateUVTrimCurves also fixes UV endpoints/reparameterization after
            // collecting its projection statistic. Measure the final installed
            // data separately; neither that old statistic nor LEVEL2 covers it.
            SmTArray<SmEdgeuse*> uses;
            native.face->GetUpwardEdgeuses(uses);
            double finalGap = 0;
            for (ULONG k = 0; k < uses.GetSize(); ++k)
            {
                const auto edge = uses[k]->GetEdge();
                const auto uv = uses[k]->GetUVTrimCurve();
                Need(edge && edge->GetCurve() && uv, "GeneralFinalTrimMissing", f.record);
                const auto interval = edge->GetInterval();
                Step(f.record);
                const auto measured = MeasureFinalTrim(*edge->GetCurve(), *uv, *native.face->GetSurface(), interval.GetMin(), interval.GetMax());
                Need(measured.maximumMm <= Q(m_options.quality.derivedUvMm), "GeneralFinalTrimGap", f.record);
                finalGap = std::max(finalGap, measured.maximumMm);
            }
            uvGap = std::max(uvGap, finalGap);
            m_report.tolerances.push_back({ "FinalInstalledUv", "KnotSpanPairedParameterSampleMaximum", Id(f.record), finalGap });
            bool explicitOuter = false;
            for (const auto& l : f.loops)
            {
                explicitOuter = explicitOuter || l.outer;
            }
            if (!explicitOuter)
            {
                ClassifyRegion(f, native);
            }
        }
    }
    void ClassifyRegion(const Face& f, NativeFaceData& native)
    {
        // Source-unmarked planes, cylinders and cones use curve-intersection
        // containment after installed-UV validation. An envelope/SET order is
        // only construction bootstrapping; it cannot identify the outer region.
        const bool cylinder = Is(f.surface, "CYLINDRICAL_SURFACE");
        const bool cone = Is(f.surface, "CONICAL_SURFACE");
        const bool periodicSupport = cylinder || cone;
        if (!Is(f.surface, "PLANE") && !periodicSupport)
        {
            Fail("GeneralRegionClassificationPending", f.record, Status::Deferred);
        }
        SmFaceProps properties(native.face, 0, m_model->context.get());
        Step(f.record);
        // The property collector uses a healer-stage enum as a readiness gate.
        // Tolerances and UV/seam prerequisites were established directly above;
        // no healer or geometry mutation is invoked.
        Good(properties.SetProps(SM_PROPSTAGE_3, native.face, 0, SM_HO_FIX_SPLITEDGE_ATSEAM));
        if (periodicSupport)
        {
            Need(
                properties.m_eBadMissingSeam == SM_SP_NEITHER && properties.m_eBadCrossedSeam == SM_SP_NEITHER &&
                    properties.m_eBadNearMissSeam == SM_SP_NEITHER && properties.m_bBadUnpairedNoArea_Loop == FALSE &&
                    properties.m_lBadMissingPoles == 0,
                "GeneralRegionSeamUnestablished",
                f.record
            );
        }
        Need(
            properties.m_lOuterLoopCnt == 1 && properties.m_lBadOrient_LoopCnt == 0 && properties.m_lBadNested_LoopCnt == 0 &&
                properties.m_lBadNoArea_LoopCnt == 0 && properties.m_lBadClosed3d_LoopCnt == 0 && properties.m_lBadClosedPtr_LoopCnt == 0,
            "GeneralRegionContainmentUnestablished",
            f.record
        );
        SmLoop* outer = nullptr;
        for (ULONG k = 0; k < properties.m_sLoopProps.GetSize(); ++k)
        {
            const auto& loop = properties.m_sLoopProps[k];
            Need(
                loop.m_eContainmentType == SM_CMT_OUTERLOOP || loop.m_eContainmentType == SM_CMT_INNERLOOP,
                "GeneralRegionContainmentUnestablished",
                f.record
            );
            if (loop.m_eContainmentType == SM_CMT_OUTERLOOP)
            {
                outer = const_cast<SmLoop*>(loop.m_cpLoop);
            }
        }
        const auto it = std::find(native.loops.begin(), native.loops.end(), outer);
        Need(it != native.loops.end(), "GeneralClassifiedLoopIdentity", f.record);
        const auto index = static_cast<Index>(it - native.loops.begin());
        if (periodicSupport)
        {
            // A cut annulus is derived from BOTH rings; neither source ring is
            // an outer-bound marker. Preserve all origins and keep the planar
            // one-bound classification report separate from this result.
            RepresentationNormalization classification;
            classification.kind = cylinder ? "NativeCylindricalCurveIntersectionContainment" : "NativeConicalCurveIntersectionContainment";
            classification.sourceFaceId = Id(f.record);
            for (const auto& loop : f.loops)
            {
                classification.sourceBoundIds.push_back(Id(loop.bound));
            }
            m_report.normalizations.push_back(std::move(classification));
        }
        else
        {
            m_report.classifiedOuterBounds.push_back({ Id(f.record), Id(f.loops[index].bound) });
        }
        if (it != native.loops.begin())
        {
            // Kernel constructor uses first loop as outer. Reorder intact loops
            // with complete ownership APIs; all source uses/directions survive.
            std::rotate(native.loops.begin(), it, it + 1);
            for (auto loop : native.loops)
            {
                Step(f.record);
                Good(native.face->RemoveLoop(loop));
            }
            for (auto loop : native.loops)
            {
                Step(f.record);
                Good(native.face->InsertLoop(loop));
            }
        }
    }
    void Validate()
    {
        SmAssertArray pointers;
        Step(shape);
        const auto status = m_model->brep->ValidatePointers(&pointers);
        m_report.pointerReports = pointers.GetSize();
        if (status != SM_SUCCESS || pointers.GetSize() != 0)
        {
            AssertFailure(pointers, "GeneralPointerValidation");
        }
        SmAssertArray deep;
        Step(shape);
        const bool valid = m_model->brep->AssertValid(&deep, SM_LEVEL_2, SM_WALK) != FALSE;
        m_report.deepReports = deep.GetSize();
        if (!valid || deep.GetSize() != 0)
        {
            AssertFailure(deep, "GeneralLevel2Validation");
        }
        for (Index i = 0; i < vertices.size(); ++i)
        {
            Need(Norm(Sub(P(m_model->vertices[i]->GetPoint()), points[i])) == 0, "GeneralSourceVertexMoved", vertices[i]);
        }
        for (Index i = 0; i < curves.size(); ++i)
        {
            SmTArray<SmEdgeuse*> uses;
            Good(m_model->edges[i]->GetEdgeuses(uses));
            Need(uses.GetSize() == 4, "GeneralNativeEdgeIncidence", curves[i].record);
        }
    }
    [[noreturn]] void AssertFailure(const SmAssertArray& reports, const char* key) const
    {
        // Capture a bounded description before discarding native objects. IDs
        // and messages remain useful after the private context is destroyed.
        std::string message;
        for (ULONG i = 0; i < std::min<ULONG>(reports.GetSize(), 128); ++i)
        {
            const auto problem = reports[i];
            for (const auto& face : m_model->faces)
            {
                if (problem->GetOwner() == face.face)
                {
                    message += "face=" + face.faceId + " ";
                }
            }
            for (const auto& face : m_model->faces)
            {
                for (const auto loop : face.loops)
                {
                    if (problem->GetOwner() == loop)
                    {
                        message += "face=" + face.faceId + " ";
                    }
                }
            }
            message += "[ownerType=" + std::to_string(problem->GetOwnerType()) + ",test=" + std::to_string(problem->GetTestIndex()) + "] ";
            TCHAR buffer[1024]{};
            problem->FormatLogMessage(buffer, 1024);
            for (const auto ch : buffer)
            {
                if (!ch)
                {
                    break;
                }
                message += ch >= 0 && ch < 128 ? static_cast<char>(ch) : '?';
            }
            message += "\n";
        }
        throw Failure{ Status::PostcheckFailure, key, message, shape, "GeneralBrep" };
    }
    void ObserveTolerances()
    {
        const auto observe = [&](const char* kind, const std::string& id, const SmObject* object)
        {
            const double tolerance = SmTol::GetZoneTol3d(object);
            Need(
                std::isfinite(tolerance) && tolerance > 0 && tolerance <= Q(m_options.quality.maximumWorkToleranceMm) &&
                    tolerance <= Tol() + Q(m_options.quality.toleranceGrowthMm),
                "GeneralUnexpectedToleranceGrowth",
                shape
            );
            m_report.tolerances.push_back({ "Postchecked", kind, id, tolerance });
        };
        observe("BrepWorkTolerance", Id(shape), m_model->brep.get());
        for (const auto& f : m_model->faces)
        {
            observe("FaceWorkTolerance", f.faceId, f.face);
        }
        for (Index i = 0; i < m_model->edges.size(); ++i)
        {
            observe("EdgeWorkTolerance", i < curves.size() ? Id(curves[i].record) : "derived-seam", m_model->edges[i]);
        }
        for (Index i = 0; i < m_model->vertices.size(); ++i)
        {
            observe("VertexWorkTolerance", Id(vertices[i]), m_model->vertices[i]);
        }
    }
    void Measure()
    {
        Statistics info;
        info.faces = faces.size();
        info.edges = m_model->edges.size();
        info.vertices = vertices.size();
        info.shapeRole = "MaterialSolid";
        // This path has already checked a closed all-face material boundary:
        // there are no free curves/vertices outside these faces. Each required
        // tight face box includes its vertices, edges AND trimmed interior
        // surface extrema. Their union therefore bounds the whole model without
        // repeating the expensive whole-BRep tight-bound query. Keep the same
        // boxes for FaceView::Info; edge-only or ordinary boxes are insufficient.
        bool firstFace = true;
        for (Index i = 0; i < m_model->faces.size(); ++i)
        {
            auto& f = m_model->faces[i];
            SmPoint3d faceMin, faceMax;
            Step(faces[i].record);
            Good(SmApiFaceBoundingBox(f.face, TRUE, faceMin, faceMax));
            f.info.boundsMm = { P(faceMin), P(faceMax) };
            for (Index a = 0; a < 3; ++a)
            {
                Need(
                    std::isfinite(f.info.boundsMm[0][a]) && std::isfinite(f.info.boundsMm[1][a]) && f.info.boundsMm[1][a] >= f.info.boundsMm[0][a],
                    "GeneralFiniteBounds",
                    faces[i].record
                );
                info.boundsMm[0][a] = firstFace ? f.info.boundsMm[0][a] : std::min(info.boundsMm[0][a], f.info.boundsMm[0][a]);
                info.boundsMm[1][a] = firstFace ? f.info.boundsMm[1][a] : std::max(info.boundsMm[1][a], f.info.boundsMm[1][a]);
            }
            firstFace = false;
        }
        Need(!firstFace, "GeneralFiniteBounds", shape);
        for (Index a = 0; a < 3; ++a)
        {
            Need(
                std::isfinite(info.boundsMm[0][a]) && std::isfinite(info.boundsMm[1][a]) && info.boundsMm[1][a] > info.boundsMm[0][a],
                "GeneralFiniteBounds",
                shape
            );
        }
        Step(shape);
        // Finite-accuracy mass integration also accumulates moment terms.
        // Center their reference frame on the bounds to reduce cancellation;
        // a corner reference failed the rotated analytic-torus control.
        // This changes no geometry or requested accuracy. midpoint avoids
        // overflow when finite bounds have large coordinates of the same sign.
        Point origin;
        for (Index axis = 0; axis < 3; ++axis)
        {
            origin[axis] = std::midpoint(info.boundsMm[0][axis], info.boundsMm[1][axis]);
        }
        GeneralSolidIntegrals integrals;
        Good(IntegrateGeneralSolid(
            *m_model,
            origin,
            [&](Index face)
            {
                Step(faces[face].record);
            },
            integrals
        ));
        info.areaMm2 = integrals.areaMm2;
        const double volume = integrals.volumeMm3;
        Need(std::isfinite(volume) && volume > 0 && std::isfinite(info.areaMm2) && info.areaMm2 > 0, "GeneralSolidMeasures", shape);
        info.materialVolumeMm3 = volume;
        info.centroidMm = integrals.centroidMm;
        for (const auto value : info.centroidMm)
        {
            Need(std::isfinite(value), "GeneralFiniteCentroid", shape);
        }
        for (Index i = 0; i < m_model->edges.size(); ++i)
        {
            double length = 0;
            Step(i < curves.size() ? curves[i].record : derivedEdgeFaces.at(i - curves.size()));
            Good(SmApiEdgeComputeLength(m_model->edges[i], m_model->queryTolerance, length));
            Need(
                std::isfinite(length) && length > 0,
                "GeneralEdgeLength",
                i < curves.size() ? curves[i].record : derivedEdgeFaces.at(i - curves.size())
            );
            info.boundaryMm += length;
        }
        for (Index i = 0; i < m_model->faces.size(); ++i)
        {
            auto& f = m_model->faces[i];
            f.info.faces = 1;
            f.info.shapeRole = "MaterialSolidBoundary";
            // The face's charged/cancellable query ran in the fused traversal.
            // Finish its cached result here, retaining the original report order.
            const auto& integral = integrals.faces[i];
            Good(std::abs(integral.areaMm2) >= SM_EFF_ZERO ? SM_SUCCESS : SM_ERR_INVALID_INPUT);
            f.info.areaMm2 = std::abs(integral.areaMm2);
            for (Index axis = 0; axis < 3; ++axis)
            {
                f.info.centroidMm[axis] = integral.areaOriginMm[axis] + integral.areaFirstMomentMm3[axis] / integral.areaMm2;
            }
            std::set<Index> faceEdges, faceVertices;
            for (const auto& loop : faces[i].loops)
            {
                for (const auto& use : loop.uses)
                {
                    faceEdges.insert(use.edge);
                    faceVertices.insert(curves[use.edge].v0);
                    faceVertices.insert(curves[use.edge].v1);
                }
            }
            f.info.edges = faceEdges.size() + (MissingSeam(faces[i]) ? 1 : 0);
            f.info.vertices = faceVertices.size();
            m_report.faces.push_back({ f.faceId, Id(faces[i].surface), faces[i].same, f.info });
        }
        m_model->info = info;
        m_report.native = info;
        m_report.solidRegions = 1;
        m_report.voidCavities = 0;
        // Measurements are intentionally not written into *_bound_mm fields,
        // which are reserved for the existing planar conservative proof.
        m_report.tolerances.push_back({ "Postcheck", "MeasuredEdgeVertexGap", "", consistency });
        m_report.tolerances.push_back({ "Postcheck", "MeasuredDerivedUvGap", "", uvGap });
    }
};
} // namespace
std::shared_ptr<ModelData> BuildGeneral(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const Options& options,
    Report& report,
    Execution& execution
)
{
    std::lock_guard lock(NativeMutex());
    try
    {
        return GeneralBuilder(std::move(source), options, report, execution).Run();
    }
    catch (...)
    {
        report.candidateDiscarded = report.nativeCalls > 0;
        throw;
    }
}
} // namespace step_import::detail
