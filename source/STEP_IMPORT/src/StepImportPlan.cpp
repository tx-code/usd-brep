// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPlan.h"

#include <algorithm>
#include <map>
#include <set>

namespace step_import::detail
{
namespace
{
ExactPoint PointAt(const step_semantics::Snapshot& source, std::size_t vertex)
{
    const auto& values = source.Field(vertex, "coordinates").Value().children;
    return { Fraction::Decimal(values.at(0).text), Fraction::Decimal(values.at(1).text), Fraction::Decimal(values.at(2).text) };
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
} // namespace
Plan MakePlan(const step_planar::Outcome& checked)
{
    const auto& evidence = checked.evidence;
    if (checked.status != step_planar::Status::Checked || !evidence.source)
    {
        throw std::logic_error("Native plan requires complete source qualification");
    }
    const auto& source = *evidence.source;
    // Preserve these declarations in the source graph, but do not grant their target qualification before ticket 10.
    for (const auto type : { "UNCERTAINTY_MEASURE_WITH_UNIT",
                             "PRODUCT_RELATED_PRODUCT_CATEGORY",
                             "MECHANICAL_CONTEXT",
                             "DESIGN_CONTEXT",
                             "PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE" })
    {
        const auto* population = source.Instances(type);
        if (!population || !population->empty())
        {
            throw Failure{ Status::Unsupported,
                           "UnsupportedNativeOptionalDeclarationScope",
                           "Optional target declarations require ticket 10 qualification",
                           {} };
        }
    }
    if (source.UnitContext().solidAngleUnit)
    {
        throw Failure{ Status::Unsupported,
                       "UnsupportedNativeOptionalDeclarationScope",
                       "Optional solid-angle declarations require ticket 10 qualification",
                       {} };
    }
    if (source.PartDefinition().shapeRole != "OpenSurfaceModel" || evidence.faces.size() != 1 ||
        std::count_if(
            evidence.faces[0].loops.begin(),
            evidence.faces[0].loops.end(),
            [](const auto& loop)
            {
                return loop.outer;
            }
        ) != 1)
    {
        throw Failure{ Status::Unsupported,
                       "UnsupportedNativeShapeScope",
                       "Native scope requires one open LINE/PLANE face with one outer loop and optional holes",
                       {} };
    }
    if (source.UnitContext().lengthPower10Mm != std::optional<int>(0))
    {
        throw Failure{ Status::Unsupported,
                       "UnsupportedNativeUnitScope",
                       "Target SI unit restatements and conversion qualification belong to ticket 09",
                       {} };
    }
    const auto& face = evidence.faces.front();
    Plan plan{};
    plan.source = evidence.source;
    plan.face = face.face;
    plan.plane = face.plane;
    plan.faceSameSense = face.sameSense;
    plan.originMm = face.originMm;
    plan.x = face.x;
    plan.y = face.y;
    plan.xNormSquared = face.xNormSquared;
    plan.yNormSquared = face.yNormSquared;
    plan.sourceAreaSquared = face.areaSquaredMm;
    plan.sourceCentroid = face.centroidMm;
    plan.sourceMinimum = face.minimumMm;
    plan.sourceMaximum = face.maximumMm;
    std::map<std::size_t, std::size_t> vertexIndices, edgeIndices;
    const auto id = [&](std::size_t record)
    {
        return source.Source().Records().at(record).id;
    };
    for (const auto& loop : face.loops)
    {
        for (const auto vertex : loop.vertices)
        {
            if (vertexIndices.emplace(vertex, plan.vertices.size()).second)
            {
                plan.vertices.push_back({ vertex, PointAt(source, source.Field(vertex, "vertex_geometry").ReferenceTarget()) });
            }
        }
    }
    for (const auto& loop : face.loops)
    {
        for (const auto record : loop.edges)
        {
            if (edgeIndices.contains(record))
            {
                throw Failure{ Status::Unsupported,
                               "UnsupportedRepeatedNativeEdgeUse",
                               "Repeated same-face edge uses need a separate seam contract",
                               record };
            }
            const auto found = std::find_if(
                evidence.edges.begin(),
                evidence.edges.end(),
                [&](const auto& edge)
                {
                    return edge.edge == record;
                }
            );
            if (found == evidence.edges.end())
            {
                throw std::logic_error("Qualified edge evidence missing");
            }
            const auto& edge = *found;
            const auto line = source.Field(record, "edge_geometry").ReferenceTarget();
            const auto vector = source.Field(line, "dir").ReferenceTarget();
            const auto direction = source.Field(vector, "orientation").ReferenceTarget();
            const auto a = PointAt(source, source.Field(line, "pnt").ReferenceTarget());
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
    }
    if (plan.edges.size() != evidence.edges.size())
    {
        throw Failure{ Status::Unsupported, "UnmappedNativeRequiredEdge", "A required source edge cannot be silently omitted", face.face };
    }
    for (const auto& loop : face.loops)
    {
        LoopPlan target{ loop.bound, loop.loop, loop.outer, loop.orientation, {}, {} };
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
        append(target.primaryUses, loop.orientation == face.sameSense);
        append(target.selectedUses, loop.orientation);
        plan.loops.push_back(std::move(target));
    }
    std::stable_sort(
        plan.loops.begin(),
        plan.loops.end(),
        [](const auto& a, const auto& b)
        {
            return a.outer && !b.outer;
        }
    );
    return plan;
}
} // namespace step_import::detail
