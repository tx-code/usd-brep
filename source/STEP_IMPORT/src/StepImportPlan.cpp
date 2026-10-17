// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPlan.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>

namespace step_import::detail
{
namespace
{
ExactPoint PointAt(const step_semantics::Snapshot& source, std::size_t vertex, Fraction scale)
{
    const auto& values = source.Field(vertex, "coordinates").Value().children;
    return { Fraction::Decimal(values.at(0).text) * scale,
             Fraction::Decimal(values.at(1).text) * scale,
             Fraction::Decimal(values.at(2).text) * scale };
}
ExactPoint Add(ExactPoint a, ExactPoint b)
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        a[i] = a[i] + b[i];
    }
    return a;
}
ExactPoint Subtract(ExactPoint a, ExactPoint b)
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        a[i] = a[i] - b[i];
    }
    return a;
}
ExactPoint Times(ExactPoint a, Fraction scale)
{
    for (auto& value : a)
    {
        value = value * scale;
    }
    return a;
}
Fraction Dot(ExactPoint a, ExactPoint b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
ExactPoint Cross(ExactPoint a, ExactPoint b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
BoxProof ProveBox(const Plan& plan)
{
    const auto need = [&](bool condition)
    {
        if (!condition)
        {
            throw Failure{
                Status::Unsupported,
                plan.materialSolid ? "UnsupportedNativeMaterialBoxScope" : "UnsupportedNativeClosedSurfaceBoxScope",
                "Closed boundary qualification requires eight exact corners, twelve orthogonal box edges and six complete outward rectangles",
                plan.record
            };
        }
    };
    need(plan.vertices.size() == 8 && plan.edges.size() == 12 && plan.faces.size() == 6);
    BoxProof proof{};
    proof.origin = plan.vertices[0].pointMm;
    std::vector<ExactPoint> directions;
    for (const auto& edge : plan.edges)
    {
        need(edge.minimumMm == plan.vertices[edge.minimumVertex].pointMm && edge.maximumMm == plan.vertices[edge.maximumVertex].pointMm);
        if (edge.minimumVertex == 0)
        {
            directions.push_back(Subtract(edge.maximumMm, proof.origin));
        }
        else if (edge.maximumVertex == 0)
        {
            directions.push_back(Subtract(edge.minimumMm, proof.origin));
        }
    }
    need(directions.size() == 3);
    for (std::size_t i = 0; i < 3; ++i)
    {
        proof.axes[i] = directions[i];
        need(Dot(directions[i], directions[i]) > Fraction());
    }
    need(
        Dot(directions[0], directions[1]) == Fraction() && Dot(directions[0], directions[2]) == Fraction() &&
        Dot(directions[1], directions[2]) == Fraction()
    );
    proof.volume = Dot(directions[0], Cross(directions[1], directions[2]));
    if (proof.volume < Fraction())
    {
        proof.volume = -proof.volume;
    }
    need(proof.volume > Fraction());
    std::set<std::size_t> covered;
    for (std::size_t mask = 0; mask < 8; ++mask)
    {
        auto expected = proof.origin;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            if (mask & (std::size_t(1) << axis))
            {
                expected = Add(expected, directions[axis]);
            }
        }
        const auto found = std::find_if(
            plan.vertices.begin(),
            plan.vertices.end(),
            [&](const auto& vertex)
            {
                return vertex.pointMm == expected;
            }
        );
        need(found != plan.vertices.end());
        const auto index = static_cast<std::size_t>(found - plan.vertices.begin());
        need(covered.insert(index).second);
        proof.corners[mask] = index;
    }
    std::set<std::pair<std::size_t, std::size_t>> pairs;
    for (const auto& edge : plan.edges)
    {
        const auto a = static_cast<std::size_t>(std::find(proof.corners.begin(), proof.corners.end(), edge.minimumVertex) - proof.corners.begin());
        const auto b = static_cast<std::size_t>(std::find(proof.corners.begin(), proof.corners.end(), edge.maximumVertex) - proof.corners.begin());
        const auto difference = a ^ b;
        need(difference == 1 || difference == 2 || difference == 4);
        need(pairs.emplace(std::min(a, b), std::max(a, b)).second);
    }
    proof.center = proof.origin;
    for (const auto& axis : proof.axes)
    {
        proof.center = Add(proof.center, Times(axis, Fraction(1, 2)));
    }
    proof.minimum = proof.maximum = proof.origin;
    for (const auto& vertex : plan.vertices)
    {
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            proof.minimum[axis] = std::min(proof.minimum[axis], vertex.pointMm[axis]);
            proof.maximum[axis] = std::max(proof.maximum[axis], vertex.pointMm[axis]);
        }
    }
    std::set<std::pair<std::size_t, bool>> sides;
    for (const auto& face : plan.faces)
    {
        need(face.loops.size() == 1 && face.loops[0].outer && face.vertices.size() == 4 && face.edges.size() == 4);
        std::vector<std::size_t> masks;
        for (const auto vertex : face.vertices)
        {
            masks.push_back(static_cast<std::size_t>(std::find(proof.corners.begin(), proof.corners.end(), vertex) - proof.corners.begin()));
            need(Dot(Cross(face.x, face.y), Subtract(plan.vertices[vertex].pointMm, face.originMm)) == Fraction());
        }
        bool found = false;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const bool bit = (masks[0] & (std::size_t(1) << axis)) != 0;
            if (std::all_of(
                    masks.begin(),
                    masks.end(),
                    [&](auto mask)
                    {
                        return ((mask & (std::size_t(1) << axis)) != 0) == bit;
                    }
                ))
            {
                need(sides.emplace(axis, bit).second);
                found = true;
            }
        }
        need(found);
        const auto outward = Times(Cross(face.x, face.y), face.faceSameSense ? Fraction(1) : Fraction(-1));
        need(Dot(outward, Subtract(face.sourceCentroid, proof.center)) > Fraction());
    }
    need(sides.size() == 6);
    return proof;
}
OpenSheetProof ProveOpenSheet(const Plan& plan)
{
    const auto need = [&](bool condition)
    {
        if (!condition)
        {
            throw Failure{ Status::Unsupported,
                           "UnsupportedNativeOpenSheetScope",
                           "Shared open-sheet qualification requires two exact simple polygons meeting only on one complete shared supporting edge",
                           plan.record };
        }
    };
    need(plan.faces.size() == 2 && plan.vertices.size() >= 4 && plan.edges.size() >= 5);
    OpenSheetProof proof{};
    std::vector<std::size_t> edgeFaces(plan.edges.size()), vertexFaces(plan.vertices.size());
    for (const auto& face : plan.faces)
    {
        for (const auto e : face.edges)
        {
            ++edgeFaces.at(e);
        }
        for (const auto v : face.vertices)
        {
            ++vertexFaces.at(v);
        }
    }
    std::size_t shared = 0;
    for (std::size_t e = 0; e < plan.edges.size(); ++e)
    {
        const auto& edge = plan.edges[e];
        need(edge.minimumMm == plan.vertices[edge.minimumVertex].pointMm && edge.maximumMm == plan.vertices[edge.maximumVertex].pointMm);
        const auto count = edgeFaces[e];
        need(count == 1 || count == 2);
        if (count == 2)
        {
            proof.sharedEdge = e;
            ++shared;
        }
    }
    need(shared == 1);
    const auto& common = plan.edges[proof.sharedEdge];
    std::array<ExactPoint, 2> normals;
    for (std::size_t f = 0; f < plan.faces.size(); ++f)
    {
        const auto& face = plan.faces[f];
        need(face.loops.size() == 1 && face.loops[0].outer && face.vertices.size() >= 3 && face.edges.size() == face.vertices.size());
        const auto& uses = face.loops[0].selectedUses;
        need(uses.size() == face.vertices.size());
        std::vector<ExactPoint> points(uses.size());
        normals[f] = Cross(face.x, face.y);
        for (std::size_t i = 0; i < uses.size(); ++i)
        {
            const auto& edge = plan.edges[uses[i].edge];
            points[i] = plan.vertices[uses[i].forward ? edge.minimumVertex : edge.maximumVertex].pointMm;
            need(Dot(normals[f], Subtract(points[i], face.originMm)) == Fraction());
        }
        const auto a = Subtract(points[1], points[0]), b = Subtract(points.back(), points[0]);
        if (points.size() == 4 && Dot(a, b) == Fraction() && points[2] == Add(Add(points[0], a), b))
        {
            // Retain the established rectangle arithmetic and capacity path.
            need(Dot(a, a) > Fraction() && Dot(b, b) > Fraction());
            need(face.sourceAreaSquared == Dot(a, a) * Dot(b, b));
            need(face.sourceCentroid == Add(points[0], Times(Add(a, b), Fraction(1, 2))));
        }
        else
        {
            // Source Checked has already proved a simple finite LINE loop.
            // Keep the established strict-convexity arithmetic/capacity path.
            // For a simple polygon, these turns prove that every side supports it.
            bool strictlyConvex = true;
            std::optional<bool> positive;
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                const auto j = (i + 1) % points.size(), k = (i + 2) % points.size();
                const auto turn = Dot(normals[f], Cross(Subtract(points[j], points[i]), Subtract(points[k], points[j])));
                const bool current = turn > Fraction();
                if (turn == Fraction() || (positive && current != *positive))
                {
                    strictlyConvex = false;
                    break;
                }
                positive = current;
            }
            if (!strictlyConvex)
            {
                // Concavities and forward collinear vertices are harmless away
                // from the shared line if every other vertex is strictly on one
                // side. Source Checked already excludes self-crossing/touching;
                // its simple filled polygon therefore meets the line only on
                // the shared edge. No convex hull, vertex removal or O(n^2) walk.
                const auto direction = Subtract(common.maximumMm, common.minimumMm);
                std::optional<bool> sidePositive;
                for (const auto& use : uses)
                {
                    const auto& edge = plan.edges[use.edge];
                    const auto vertex = use.forward ? edge.minimumVertex : edge.maximumVertex;
                    if (vertex == common.minimumVertex || vertex == common.maximumVertex)
                    {
                        continue;
                    }
                    const auto side = Dot(normals[f], Cross(direction, Subtract(plan.vertices[vertex].pointMm, common.minimumMm)));
                    need(side != Fraction());
                    const bool current = side > Fraction();
                    need(!sidePositive || current == *sidePositive);
                    sidePositive = current;
                }
                need(sidePositive.has_value());
            }
        }
    }
    need(plan.vertices.size() == plan.faces[0].vertices.size() + plan.faces[1].vertices.size() - 2);
    need(plan.edges.size() == plan.faces[0].edges.size() + plan.faces[1].edges.size() - 1);
    std::size_t commonVertices = 0;
    for (std::size_t v = 0; v < vertexFaces.size(); ++v)
    {
        need(vertexFaces[v] == 1 || vertexFaces[v] == 2);
        if (vertexFaces[v] == 2)
        {
            need(v == common.minimumVertex || v == common.maximumVertex);
            ++commonVertices;
        }
    }
    need(commonVertices == 2);
    const auto axis = Cross(normals[0], normals[1]);
    if (Dot(axis, axis) == Fraction())
    {
        need(Dot(normals[0], Subtract(plan.faces[1].originMm, plan.faces[0].originMm)) == Fraction());
        const auto direction = Subtract(common.maximumMm, common.minimumMm);
        const auto side = [&](const FacePlan& face)
        {
            return Dot(normals[0], Cross(direction, Subtract(face.sourceCentroid, common.minimumMm)));
        };
        const auto a = side(plan.faces[0]), b = side(plan.faces[1]);
        need((a > Fraction() && b < Fraction()) || (a < Fraction() && b > Fraction()));
        // The proofs above put each filled polygon strictly on one side away
        // from the shared edge, so its area centroid selects that half-plane
        // even for a concave polygon. Opposite signs exclude area overlap.
    }
    // For distinct planes their intersection line contains the shared side.
    // Each qualified polygon meets it only on that complete side. Either
    // admitted case has exactly the declared finite intersection, independently
    // of native validator success.
    proof.minimum = proof.maximum = plan.vertices.front().pointMm;
    for (const auto& vertex : plan.vertices)
    {
        for (std::size_t i = 0; i < 3; ++i)
        {
            proof.minimum[i] = std::min(proof.minimum[i], vertex.pointMm[i]);
            proof.maximum[i] = std::max(proof.maximum[i], vertex.pointMm[i]);
        }
    }
    return proof;
}
} // namespace
SourceDeclarations DescribeSource(const step_planar::Outcome& checked, const Quality& quality)
{
    if (checked.status != step_planar::Status::Checked || !checked.evidence.source)
    {
        throw std::logic_error("Source declarations require complete source qualification");
    }
    SourceDeclarations result;
    result.semantic = checked.evidence.source;
    const auto& source = *result.semantic;
    const auto& raw = source.Source();
    std::vector<bool> selected(raw.Records().size());
    std::vector<std::size_t> pending;
    const auto add = [&](std::size_t r)
    {
        if (!selected.at(r))
        {
            selected[r] = true;
            pending.push_back(r);
        }
    };
    for (const auto type : { "APPLICATION_CONTEXT",
                             "APPLICATION_PROTOCOL_DEFINITION",
                             "PRODUCT_CONTEXT",
                             "PRODUCT_DEFINITION_CONTEXT",
                             "PRODUCT",
                             "PRODUCT_DEFINITION_FORMATION",
                             "PRODUCT_DEFINITION",
                             "PRODUCT_RELATED_PRODUCT_CATEGORY",
                             "REPRESENTATION_CONTEXT",
                             "NAMED_UNIT",
                             "UNCERTAINTY_MEASURE_WITH_UNIT" })
    {
        const auto* population = source.Instances(type);
        if (!population)
        {
            throw std::logic_error("Unregistered source declaration population");
        }
        for (const auto r : *population)
        {
            add(r);
        }
    }
    std::vector<std::vector<std::size_t>> dependencies(raw.Records().size());
    for (const auto& reference : raw.References())
    {
        dependencies[reference.recordIndex].push_back(reference.targetRecordIndex);
    }
    for (std::size_t i = 0; i < pending.size(); ++i)
    {
        for (const auto dependency : dependencies[pending[i]])
        {
            add(dependency);
        }
    }
    for (std::size_t r = 0; r < selected.size(); ++r)
    {
        if (selected[r])
        {
            result.records.push_back(r);
        }
    }
    const auto scale = [&](std::size_t unit)
    {
        for (const auto& conversion : checked.evidence.conversions)
        {
            if (conversion.unit == unit)
            {
                return conversion.scaleMm;
            }
        }
        for (const auto& si : source.UnitContext().siDefinitions)
        {
            if (si.recordIndex == unit && si.quantity == "Length")
            {
                Fraction value(1);
                for (int i = 0; i < std::abs(si.canonicalPower10); ++i)
                {
                    value = si.canonicalPower10 < 0 ? value / Fraction(10) : value * Fraction(10);
                }
                return value;
            }
        }
        throw std::logic_error("Qualified uncertainty requires a checked length unit");
    };
    for (const auto r : *source.Instances("UNCERTAINTY_MEASURE_WITH_UNIT"))
    {
        const auto& value = source.Field(r, "value_component").Value();
        SourceUncertainty uncertainty;
        uncertainty.record = r;
        uncertainty.unit = source.Field(r, "unit_component").ReferenceTarget();
        uncertainty.valueType = value.text;
        uncertainty.value = Fraction::Decimal(value.children.at(0).text);
        uncertainty.consistencyComparison = "Unestablished";
        try
        {
            uncertainty.unitScaleMm = scale(uncertainty.unit);
            uncertainty.valueMm = uncertainty.value * *uncertainty.unitScaleMm;
            uncertainty.consistencyComparison = *uncertainty.valueMm <= quality.consistencyMm ? "Within" : "Exceeds";
        }
        catch (const std::overflow_error&)
        {
            // Optional source precision cannot enlarge Q or fail otherwise qualified geometry.
            // Exact source values and units remain available even when this advisory comparison is unestablished.
        }
        result.uncertainties.push_back(std::move(uncertainty));
    }
    return result;
}
Plan MakePlan(const step_planar::Outcome& checked)
{
    const auto& evidence = checked.evidence;
    if (checked.status != step_planar::Status::Checked || !evidence.source)
    {
        throw std::logic_error("Native plan requires complete source qualification");
    }
    const auto& source = *evidence.source;
    const auto role = source.PartDefinition().shapeRole;
    if (role != "MaterialSolid" && role != "ClosedSurfaceModel" &&
        (role != "OpenSurfaceModel" || (evidence.faces.size() != 1 && evidence.faces.size() != 2)))
    {
        throw Failure{ Status::Unsupported,
                       "UnsupportedNativeShapeScope",
                       "Native scope is one open planar face, two polygons with a shared supporting edge, or a qualified rectangular box boundary",
                       {} };
    }
    Plan plan{};
    plan.source = evidence.source;
    plan.units.lengthToMm = evidence.faces.at(0).sourceLengthScaleMm;
    plan.units.sourceLengthUnitId = source.Source().Records().at(source.UnitContext().lengthUnit).id;
    plan.units.definitions = evidence.conversions;
    if (!(plan.units.lengthToMm > Fraction()))
    {
        throw std::logic_error("Native plan requires a proved positive canonical length scale");
    }
    plan.sourceShape.role = role;
    plan.materialSolid = role == "MaterialSolid";
    plan.closedBoundary = role == "MaterialSolid" || role == "ClosedSurfaceModel";
    for (const auto& item : source.PartDefinition().roles)
    {
        const auto& record = source.Source().Records().at(item.recordIndex);
        const auto& type = record.components.at(0).name;
        if (item.name == "representation")
        {
            plan.sourceShape.representationId = record.id;
            plan.sourceShape.representationType = type;
        }
        if (item.name == "shape")
        {
            plan.record = item.recordIndex;
            plan.sourceShape.shapeId = record.id;
            plan.sourceShape.shapeType = type;
        }
        if (item.name == "shell")
        {
            plan.shell = item.recordIndex;
            plan.sourceShape.shellId = record.id;
            plan.sourceShape.shellType = type;
        }
    }
    if (!plan.closedBoundary && evidence.faces.size() == 1)
    {
        plan.record = evidence.faces.front().face;
    }
    std::map<std::size_t, std::size_t> vertexIndices, edgeIndices;
    const auto id = [&](std::size_t record)
    {
        return source.Source().Records().at(record).id;
    };
    for (const auto& face : evidence.faces)
    {
        for (const auto& loop : face.loops)
        {
            for (const auto vertex : loop.vertices)
            {
                if (vertexIndices.emplace(vertex, plan.vertices.size()).second)
                {
                    plan.vertices.push_back(
                        { vertex, PointAt(source, source.Field(vertex, "vertex_geometry").ReferenceTarget(), plan.units.lengthToMm) }
                    );
                }
            }
        }
    }
    for (const auto& edge : evidence.edges)
    {
        const auto record = edge.edge;
        const auto line = source.Field(record, "edge_geometry").ReferenceTarget();
        const auto vector = source.Field(line, "dir").ReferenceTarget();
        const auto direction = source.Field(vector, "orientation").ReferenceTarget();
        const auto a = PointAt(source, source.Field(line, "pnt").ReferenceTarget(), plan.units.lengthToMm);
        const auto& ratios = source.Field(direction, "direction_ratios").Value().children;
        const ExactPoint d{ Fraction::Decimal(ratios.at(0).text), Fraction::Decimal(ratios.at(1).text), Fraction::Decimal(ratios.at(2).text) };
        const auto norm = Dot(d, d);
        const auto project = [&](std::size_t vertex)
        {
            const auto point = plan.vertices.at(vertexIndices.at(vertex)).pointMm;
            return Add(a, Times(d, Dot(Subtract(point, a), d) / norm));
        };
        const auto minimum = edge.sameSense ? edge.start : edge.end;
        const auto maximum = edge.sameSense ? edge.end : edge.start;
        EdgeMapping mapping{ id(record),  id(line),       id(edge.start),         id(edge.end),         id(minimum),
                             id(maximum), edge.sameSense, edge.tStartCoefficient, edge.tEndCoefficient, edge.directionNormSquared };
        edgeIndices.emplace(record, plan.edges.size());
        plan.edges.push_back(
            { record,
              vertexIndices.at(minimum),
              vertexIndices.at(maximum),
              project(minimum),
              project(maximum),
              edge.lengthSquaredMm,
              std::move(mapping) }
        );
    }
    std::set<std::size_t> mappedEdges;
    for (const auto& face : evidence.faces)
    {
        FacePlan target{};
        target.face = face.face;
        target.plane = face.plane;
        target.faceSameSense = face.sameSense;
        target.originMm = face.originMm;
        target.x = face.x;
        target.y = face.y;
        target.xNormSquared = face.xNormSquared;
        target.yNormSquared = face.yNormSquared;
        target.sourceAreaSquared = face.areaSquaredMm;
        target.sourceCentroid = face.centroidMm;
        target.sourceMinimum = face.minimumMm;
        target.sourceMaximum = face.maximumMm;
        std::set<std::size_t> faceEdges, faceVertices;
        for (const auto& loop : face.loops)
        {
            LoopPlan targetLoop{ loop.bound, loop.loop, loop.outer, loop.orientation, {}, {} };
            for (const auto record : loop.edges)
            {
                const auto index = edgeIndices.at(record);
                if (!faceEdges.insert(index).second)
                {
                    throw Failure{ Status::Unsupported,
                                   "UnsupportedRepeatedNativeEdgeUse",
                                   "Repeated same-face uses require a seam contract",
                                   record };
                }
                target.edges.push_back(index);
                mappedEdges.insert(index);
            }
            for (const auto vertex : loop.vertices)
            {
                const auto index = vertexIndices.at(vertex);
                if (faceVertices.insert(index).second)
                {
                    target.vertices.push_back(index);
                }
            }
            const auto append = [&](std::vector<UsePlan>& uses, bool forward)
            {
                for (std::size_t offset = 0; offset < loop.edges.size(); ++offset)
                {
                    const auto i = forward ? offset : loop.edges.size() - 1 - offset;
                    const auto index = edgeIndices.at(loop.edges[i]);
                    const bool alongMinimum = loop.edgeOrientations.at(i) == plan.edges.at(index).mapping.sourceSameSense;
                    uses.push_back({ index, loop.orientedEdges.at(i), forward ? alongMinimum : !alongMinimum });
                }
            };
            append(targetLoop.primaryUses, loop.orientation == face.sameSense);
            append(targetLoop.selectedUses, loop.orientation);
            target.loops.push_back(std::move(targetLoop));
        }
        std::stable_sort(
            target.loops.begin(),
            target.loops.end(),
            [](const auto& a, const auto& b)
            {
                return a.outer && !b.outer;
            }
        );
        plan.faces.push_back(std::move(target));
    }
    if (mappedEdges.size() != plan.edges.size())
    {
        throw Failure{ Status::Unsupported, "UnmappedNativeRequiredEdge", "Required source edges cannot be omitted", plan.record };
    }
    if (plan.closedBoundary)
    {
        plan.box = ProveBox(plan);
    }
    else if (plan.faces.size() == 2)
    {
        plan.openSheet = ProveOpenSheet(plan);
    }
    return plan;
}
} // namespace step_import::detail
