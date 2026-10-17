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
    if (role != "MaterialSolid" && role != "ClosedSurfaceModel" && (role != "OpenSurfaceModel" || evidence.faces.size() != 1))
    {
        throw Failure{ Status::Unsupported,
                       "UnsupportedNativeShapeScope",
                       "Native scope is one open planar face or a qualified rectangular box boundary with its declared role",
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
    if (!plan.closedBoundary)
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
    return plan;
}
} // namespace step_import::detail
