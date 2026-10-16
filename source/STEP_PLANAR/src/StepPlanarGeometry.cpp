// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepPlanarInternal.h"

#include <algorithm>
#include <set>

namespace step_planar
{
namespace
{
using Point2 = std::array<Rational, 2>;
Rational Orient(Point2 a, Point2 b, Point2 c)
{
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}
bool OnSegment(Point2 p, Point2 a, Point2 b)
{
    return Orient(a, b, p) == Rational() && std::min(a[0], b[0]) <= p[0] && p[0] <= std::max(a[0], b[0]) && std::min(a[1], b[1]) <= p[1] &&
           p[1] <= std::max(a[1], b[1]);
}
int Sign(Rational a)
{
    return a == Rational() ? 0 : a > Rational() ? 1 : -1;
}
bool Intersects(Point2 a, Point2 b, Point2 c, Point2 d)
{
    const auto x = Orient(a, b, c), y = Orient(a, b, d), z = Orient(c, d, a), w = Orient(c, d, b);
    return (Sign(x) * Sign(y) < 0 && Sign(z) * Sign(w) < 0) || OnSegment(c, a, b) || OnSegment(d, a, b) || OnSegment(a, c, d) || OnSegment(b, c, d);
}
// Strict interior: boundary is explicitly separate from winding zero (outside).
int Contains(const std::vector<Point2>& polygon, Point2 p)
{
    int winding = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i)
    {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        if (OnSegment(p, a, b))
        {
            return 0;
        }
        if (a[1] <= p[1] && p[1] < b[1] && Orient(a, b, p) > Rational())
        {
            ++winding;
        }
        if (b[1] <= p[1] && p[1] < a[1] && Orient(a, b, p) < Rational())
        {
            --winding;
        }
    }
    return winding == 0 ? -1 : 1;
}
Rational Power(int power)
{
    return Rational::Decimal("1E" + std::to_string(power));
}
} // namespace

void Validator::Edge(std::size_t r, Rational scale)
{
    Tick(r);
    const auto line = Ref(r, "edge_geometry"), vector = Ref(line, "dir"), direction = Ref(vector, "orientation");
    const auto magnitude = Rational::Decimal(Field(vector, "magnitude").text);
    if (magnitude == Rational())
    {
        Fail("NonzeroDefiningLineRequired", "D4", vector, "magnitude", "VECTOR.WR1 permits zero; this LINE cannot define a finite edge direction");
    }
    const auto a = Point(Ref(line, "pnt")), d = Direction(direction);
    const auto start = Ref(r, "edge_start"), end = Ref(r, "edge_end");
    const auto p = Point(Ref(start, "vertex_geometry")), q = Point(Ref(end, "vertex_geometry"));
    const auto d2 = Squared(d), scale2 = scale * scale;
    EdgeEvidence edge;
    edge.edge = r;
    edge.line = line;
    edge.start = start;
    edge.end = end;
    edge.sameSense = Sense(r, "same_sense");
    edge.directionNormSquared = d2;
    edge.tStartCoefficient = Dot(Subtract(p, a), d) / magnitude;
    edge.tEndCoefficient = Dot(Subtract(q, a), d) / magnitude;
    edge.lengthSquaredMm = Squared(Subtract(q, p)) * scale2;
    out.evidence.edges.push_back(edge);
    const auto threshold2 = out.quality.consistencyMm * out.quality.consistencyMm;
    const auto r0 = Squared(Cross(Subtract(p, a), d)) / d2 * scale2;
    const auto r1 = Squared(Cross(Subtract(q, a), d)) / d2 * scale2;
    out.evidence.metrics.push_back(
        { "endpoint_curve",
          r,
          { line, start, end },
          { { "start_distance_squared_mm", r0 }, { "end_distance_squared_mm", r1 }, { "threshold_squared_mm", threshold2 } } }
    );
    if (r0 > threshold2 || r1 > threshold2)
    {
        Fail("EndpointCurveMismatch", "D4", line, "pnt", "Source VP endpoints do not lie on defining LINE within the explicit consistency threshold");
    }
    if (edge.lengthSquaredMm == Rational())
    {
        Fail("NonzeroEdgeRequired", "D4", r, "edge_end", "Distinct topological endpoints must define a nonzero geometric segment");
    }
    const bool increasing = edge.tEndCoefficient > edge.tStartCoefficient;
    if (edge.tEndCoefficient == edge.tStartCoefficient || increasing != edge.sameSense)
    {
        Fail(
            "EdgeCurveSenseMismatch",
            "D4",
            r,
            "same_sense",
            "Source EC.same_sense disagrees with the signed LINE parameter interval; endpoints are not swapped to repair it"
        );
    }
}

std::pair<Vector, Vector> Validator::Axes(std::size_t placement, Vector& normal)
{
    Tick(placement);
    const auto& axis = Field(placement, "axis");
    const auto& reference = Field(placement, "ref_direction");
    normal = axis.kind == step_source::ValueKind::Unset ? Vector{ 0, 0, 1 } : Direction(Target(axis));
    Vector x;
    if (reference.kind == step_source::ValueKind::Unset)
    {
        x = normal[1] == Rational() && normal[2] == Rational() ? Vector{ 0, 1, 0 } : Vector{ 1, 0, 0 };
    }
    else
    {
        x = Direction(Target(reference));
    }
    // Positive normalisations cancel in Gram-Schmidt. Keep an exact orthogonal basis, with metric
    // factors explicit: no approximate fitting of the source PLANE or hidden UV reparameterisation.
    x = Subtract(x, Times(normal, Dot(x, normal) / Squared(normal)));
    if (Squared(x) == Rational())
    {
        Fail(
            "DerivedPlacementAxesIndeterminate",
            "D4",
            placement,
            "ref_direction",
            "first_proj_axis has no defined result; optional-axis WHEREs do not invent a fallback axis",
            Status::Indeterminate
        );
    }
    return { x, Cross(normal, x) };
}
FaceEvidence Validator::Face(std::size_t r, Rational scale)
{
    Tick(r);
    FaceEvidence face;
    face.face = r;
    face.plane = Ref(r, "face_geometry");
    face.placement = Ref(face.plane, "position");
    face.sameSense = Sense(r, "same_sense");
    face.sourceLengthScaleMm = scale;
    face.originMm = Times(Point(Ref(face.placement, "location")), scale);
    const auto basis = Axes(face.placement, face.normal);
    face.x = basis.first;
    face.y = basis.second;
    face.xNormSquared = Squared(face.x);
    face.yNormSquared = Squared(face.y);
    face.metricSquared = face.xNormSquared * face.yNormSquared;
    const auto threshold2 = out.quality.consistencyMm * out.quality.consistencyMm;
    const auto planeResidual = [&](Vector p)
    {
        const auto distance = Dot(Subtract(p, face.originMm), face.normal);
        return distance * distance / Squared(face.normal);
    };
    for (const auto bound : Refs(r, "bounds"))
    {
        LoopEvidence loop;
        loop.bound = bound;
        loop.loop = Ref(bound, "bound");
        loop.outer = Is(bound, "FACE_OUTER_BOUND");
        loop.orientation = Sense(bound, "orientation");
        loop.orientedEdges = Refs(loop.loop, "edge_list");
        for (const auto oe : loop.orientedEdges)
        {
            Tick(oe);
            const auto ends = Ends(oe);
            const auto ec = Ref(oe, "edge_element"), line = Ref(ec, "edge_geometry");
            loop.edges.push_back(ec);
            loop.vertices.push_back(ends.first);
            loop.edgeOrientations.push_back(Sense(oe, "orientation"));
            const auto p = Times(Point(Ref(ends.first, "vertex_geometry")), scale);
            const auto q = Times(Point(Ref(ends.second, "vertex_geometry")), scale);
            const auto a = Times(Point(Ref(line, "pnt")), scale), d = Direction(Ref(Ref(line, "dir"), "orientation"));
            const auto projectedCurve = [&](Vector v)
            {
                return Add(a, Times(d, Dot(Subtract(v, a), d) / Squared(d)));
            };
            const auto pResidual = planeResidual(p), qResidual = planeResidual(q);
            const auto c0 = planeResidual(projectedCurve(p)), c1 = planeResidual(projectedCurve(q));
            out.evidence.metrics.push_back(
                { "edge_surface",
                  r,
                  { ec, oe, face.plane },
                  { { "vertex_start_squared_mm", pResidual },
                    { "vertex_end_squared_mm", qResidual },
                    { "line_start_squared_mm", c0 },
                    { "line_end_squared_mm", c1 },
                    { "threshold_squared_mm", threshold2 } } }
            );
            if (std::max({ pResidual, qResidual, c0, c1 }) > threshold2)
            {
                Fail(
                    "EdgeSurfaceMismatch",
                    "D4",
                    face.plane,
                    "position",
                    "Both source vertices and the finite LINE segment must agree with the source PLANE; maximum endpoint residual exceeds Q"
                );
            }
            const auto delta = Subtract(p, face.originMm);
            loop.projected.push_back({ Dot(delta, face.x) / face.xNormSquared, Dot(delta, face.y) / face.yNormSquared });
        }
        const auto& polygon = loop.projected;
        const auto count = polygon.size();
        if (count < 3)
        {
            Fail("DegeneratePlanarBoundary", "D4", loop.loop, "edge_list", "A simple finite planar region requires at least three sides");
        }
        std::set<std::size_t> identities;
        Rational twiceArea;
        for (std::size_t i = 0; i < count; ++i)
        {
            Tick(loop.loop);
            const auto a = polygon[i], b = polygon[(i + 1) % count], c = polygon[(i + 2) % count];
            if (!identities.insert(loop.vertices[i]).second || a == b)
            {
                Fail(
                    "DegeneratePlanarBoundary",
                    "D4",
                    loop.loop,
                    "edge_list",
                    "Repeated vertex identity or coincident consecutive projected vertices"
                );
            }
            twiceArea = twiceArea + a[0] * b[1] - b[0] * a[1];
            const auto forward = (b[0] - a[0]) * (c[0] - b[0]) + (b[1] - a[1]) * (c[1] - b[1]);
            if (Orient(a, b, c) == Rational() && !(forward > Rational()))
            {
                Fail(
                    "PlanarBoundaryIntersection",
                    "D4",
                    loop.loop,
                    "edge_list",
                    "Adjacent boundary segments overlap or reverse at their common vertex"
                );
            }
            for (std::size_t j = i + 1; j < count; ++j)
            {
                Tick(loop.loop);
                if (j == i + 1 || (i == 0 && j + 1 == count))
                {
                    continue;
                }
                if (Intersects(a, b, polygon[j], polygon[(j + 1) % count]))
                {
                    Fail(
                        "PlanarBoundaryIntersection",
                        "D4",
                        loop.loop,
                        "edge_list",
                        "Nonadjacent boundary segments intersect or touch; no automatic splitting"
                    );
                }
            }
        }
        loop.signedAreaCoefficient = twiceArea / Rational(2) * (loop.orientation ? Rational(1) : Rational(-1));
        if (loop.signedAreaCoefficient == Rational())
        {
            Fail("DegeneratePlanarRegion", "D4", loop.loop, "edge_list", "Source projected boundary has zero area");
        }
        const auto relative = loop.signedAreaCoefficient * (face.sameSense ? Rational(1) : Rational(-1));
        out.evidence.metrics.push_back(
            { "bound_orientation",
              bound,
              { r, loop.loop },
              { { "signed_area_coefficient", loop.signedAreaCoefficient },
                { "metric_squared", face.metricSquared },
                { "face_same_sense", face.sameSense ? Rational(1) : Rational(-1) } } }
        );
        if (loop.outer ? !(relative > Rational()) : !(relative < Rational()))
        {
            Fail(
                loop.outer ? "FaceBoundaryOrShellOrientationMismatch" : "InnerBoundaryOrientationMismatch",
                "D4",
                loop.outer ? r : bound,
                loop.outer ? "same_sense" : "orientation",
                "Effective bound winding (OE then FACE_BOUND) must agree with FACE_SURFACE.same_sense for outer, and oppose it for inner boundaries"
            );
        }
        face.loops.push_back(std::move(loop));
    }
    auto outer = std::find_if(
        face.loops.begin(),
        face.loops.end(),
        [](const auto& loop)
        {
            return loop.outer;
        }
    );
    if (outer == face.loops.end())
    {
        throw std::logic_error("Explicit outer-bound guard was not established");
    }
    for (std::size_t i = 0; i < face.loops.size(); ++i)
    {
        const auto& a = face.loops[i];
        if (!a.outer)
        {
            for (std::size_t v = 0; v < outer->projected.size(); ++v)
            {
                Tick(a.loop);
            }
            if (Contains(outer->projected, a.projected.front()) != 1)
            {
                Fail("InnerBoundaryOutsideOuter", "D4", a.bound, "bound", "Every inner boundary must lie strictly inside the explicit outer boundary");
            }
        }
        for (std::size_t j = i + 1; j < face.loops.size(); ++j)
        {
            const auto& b = face.loops[j];
            for (std::size_t u = 0; u < a.projected.size(); ++u)
            {
                for (std::size_t v = 0; v < b.projected.size(); ++v)
                {
                    Tick(a.loop);
                    if (Intersects(
                            a.projected[u],
                            a.projected[(u + 1) % a.projected.size()],
                            b.projected[v],
                            b.projected[(v + 1) % b.projected.size()]
                        ))
                    {
                        Fail(
                            "PlanarBoundaryIntersection",
                            "D4",
                            b.bound,
                            "bound",
                            "Different bounds intersect or touch; containment alone is insufficient"
                        );
                    }
                }
            }
            if (!a.outer && !b.outer)
            {
                for (std::size_t v = 0; v < a.projected.size() + b.projected.size(); ++v)
                {
                    Tick(a.loop);
                }
                if (Contains(a.projected, b.projected.front()) == 1 || Contains(b.projected, a.projected.front()) == 1)
                {
                    Fail(
                        "AmbiguousNestedInnerBoundary",
                        "D4",
                        b.bound,
                        "bound",
                        "Nested holes require an explicit supported region interpretation; no silent parity rewrite"
                    );
                }
            }
        }
    }
    Rational totalTwice;
    Vector moment{};
    bool first = true;
    for (const auto& loop : face.loops)
    {
        const auto& polygon = loop.projected;
        for (std::size_t i = 0; i < polygon.size(); ++i)
        {
            Tick(loop.loop);
            const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
            const auto pa = Add(face.originMm, Add(Times(face.x, a[0]), Times(face.y, a[1])));
            const auto pb = Add(face.originMm, Add(Times(face.x, b[0]), Times(face.y, b[1])));
            const auto cross = (a[0] * b[1] - b[0] * a[1]) * (loop.orientation ? Rational(1) : Rational(-1));
            totalTwice = totalTwice + cross;
            moment = Add(moment, Times(Add(Subtract(pa, face.originMm), Subtract(pb, face.originMm)), cross));
            if (first)
            {
                face.minimumMm = pa;
                face.maximumMm = pa;
                first = false;
            }
            for (std::size_t k = 0; k < 3; ++k)
            {
                face.minimumMm[k] = std::min(face.minimumMm[k], pa[k]);
                face.maximumMm[k] = std::max(face.maximumMm[k], pa[k]);
            }
        }
    }
    if (totalTwice == Rational())
    {
        Fail("DegeneratePlanarRegion", "D4", r, "bounds", "Outer area minus inner areas must be nonzero");
    }
    face.areaSquaredMm = totalTwice * totalTwice / Rational(4) * face.metricSquared;
    face.centroidMm = Add(face.originMm, Times(moment, Rational(1) / (Rational(3) * totalTwice)));
    return face;
}

void Validator::Shell()
{
    const auto shell = Role("shell");
    struct Use
    {
        std::size_t face;
        int direction;
    };
    std::map<std::size_t, std::vector<Use>> uses;
    for (const auto& face : out.evidence.faces)
    {
        for (const auto& loop : face.loops)
        {
            for (std::size_t i = 0; i < loop.edges.size(); ++i)
            {
                uses[loop.edges[i]].push_back({ face.face, (loop.edgeOrientations[i] ? 1 : -1) * (loop.orientation ? 1 : -1) });
            }
        }
    }
    std::size_t boundary = 0, manifold = 0;
    std::map<std::size_t, std::set<std::size_t>> neighbours;
    for (const auto& entry : uses)
    {
        Tick(shell);
        const auto& list = entry.second;
        if (list.size() == 1)
        {
            ++boundary;
        }
        else if (list.size() == 2 && list[0].face != list[1].face)
        {
            ++manifold;
            if (list[0].direction == list[1].direction)
            {
                Fail(
                    "FaceBoundaryOrShellOrientationMismatch",
                    "D4",
                    shell,
                    "cfs_faces",
                    "Shared source edge occurrences have the same effective direction"
                );
            }
            neighbours[list[0].face].insert(list[1].face);
            neighbours[list[1].face].insert(list[0].face);
        }
        else
        {
            Fail("NonManifoldSourceEdge", "D4", entry.first, "edge_start", "A source edge has unsupported repeated or non-manifold face use");
        }
    }
    out.evidence.metrics.push_back(
        { "shell_incidence",
          shell,
          Refs(shell, "cfs_faces"),
          { { "boundary_edges", Rational(static_cast<std::int64_t>(boundary)) }, { "paired_edges", Rational(static_cast<std::int64_t>(manifold)) } } }
    );
    if (Is(shell, "CLOSED_SHELL") && boundary)
    {
        Fail(
            "ClaimedClosedShellHasBoundary",
            "D4",
            shell,
            "cfs_faces",
            "CLOSED_SHELL claim has free source edges; no filling, stitching or type promotion"
        );
    }
    const auto faces = Refs(shell, "cfs_faces");
    std::set<std::size_t> reached;
    std::vector<std::size_t> pending{ faces.front() };
    while (!pending.empty())
    {
        const auto face = pending.back();
        pending.pop_back();
        if (!reached.insert(face).second)
        {
            continue;
        }
        Tick(face);
        for (const auto n : neighbours[face])
        {
            pending.push_back(n);
        }
    }
    if (reached.size() != faces.size())
    {
        Fail("DisconnectedSourceShell", "D4", shell, "cfs_faces", "CONNECTED_FACE_SET source claim is disconnected by shared edge identity");
    }
}
void Validator::Geometry()
{
    const auto power = sem.UnitContext().lengthPower10Mm;
    if (!power)
    {
        throw std::logic_error("Conversion length interpretation must not enter planar geometry");
    }
    Tick(sem.UnitContext().lengthUnit);
    const auto scale = Power(*power);
    for (const auto placement : Population("AXIS2_PLACEMENT_3D"))
    {
        Vector normal;
        (void)Axes(placement, normal);
    }
    for (const auto ec : Population("EDGE_CURVE"))
    {
        Edge(ec, scale);
    }
    for (const auto face : Population("ADVANCED_FACE"))
    {
        out.evidence.faces.push_back(Face(face, scale));
    }
    Shell();
}
} // namespace step_planar
