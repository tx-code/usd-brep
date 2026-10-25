// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <SmPoly.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string_view>
#include <tl/expected.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace step_view
{
using TriangleResult = tl::expected<std::size_t, std::string_view>;
// Consumes only request-private derived mesh state, never the defining BRep.
// SmPolyFace::TriangulateNonPlanar supports regular, single-loop polygons; its
// success is not a continuous source-surface or tessellation-error certificate.
// Failure leaves an unpublished work mesh for the caller to discard. Allocation
// exceptions retain their meaning; no partial scene, repair or retry is implied.
inline TriangleResult TriangulateDisplayMesh(SmPolyBrep& mesh, std::size_t maximumTriangles)
{
    SmTArray<SmPolyFace*> faces;
    mesh.GetPolyFaces(faces);
    if (!faces.GetSize() || faces.GetSize() > maximumTriangles)
    {
        return tl::unexpected("DisplayMeshLimit");
    }
    struct Polygon
    {
        SmPolyFace* face;
        std::size_t edges;
    };
    std::vector<Polygon> polygons;
    std::size_t planned = 0;
    for (auto* face : faces)
    {
        SmTArray<SmPolyLoop*> loops;
        face->GetPolyLoops(loops);
        // Never export only the outer ring of a cell with a hole. A valid
        // source BRep does not make an unsupported display cell admissible.
        if (loops.GetSize() != 1 || !loops[0])
        {
            return tl::unexpected("DisplayPolygonLoopsUnsupported");
        }
        SmTArray<SmPolyEdge*> edges;
        loops[0]->GetPolyEdges(edges);
        if (edges.GetSize() < 3)
        {
            return tl::unexpected("DisplayPolygonBoundaryUnsupported");
        }
        const auto triangles = std::size_t(edges.GetSize()) - 2;
        if (triangles > maximumTriangles - planned)
        {
            return tl::unexpected("DisplayMeshLimit");
        }
        planned += triangles;
        if (edges.GetSize() > 3)
        {
            SmTArray<SmPolyEdge*> nondegenerate;
            face->GetNonDegeneratePolyEdges(nondegenerate);
            if (nondegenerate.GetSize() != edges.GetSize())
            {
                return tl::unexpected("DisplayPolygonSingularBoundaryUnsupported");
            }
            std::unordered_set<SmPolyVertex*> ring;
            for (auto* edge : edges)
            {
                if (!edge->GetStartPolyVertex() || !ring.insert(edge->GetStartPolyVertex()).second)
                {
                    return tl::unexpected("DisplayPolygonRepeatedVertexUnsupported");
                }
            }
            polygons.push_back({ face, edges.GetSize() });
        }
    }
    // Keep ordinary triangle order/payload; that path needs no point snapshot.
    if (polygons.empty())
    {
        return planned;
    }
    using Bits = std::array<std::uint64_t, 3>;
    struct Vertex
    {
        std::size_t index;
        Bits bits;
    };
    const auto bits = [](const SmPoint3d& p)
    {
        return Bits{ std::bit_cast<std::uint64_t>(p.x), std::bit_cast<std::uint64_t>(p.y), std::bit_cast<std::uint64_t>(p.z) };
    };
    SmTArray<SmPolyVertex*> vertices;
    mesh.GetPolyVertices(vertices);
    if (vertices.GetSize() > std::size_t(std::numeric_limits<std::int32_t>::max()))
    {
        return tl::unexpected("DisplayMeshLimit");
    }
    std::unordered_map<SmPolyVertex*, Vertex> original;
    original.reserve(vertices.GetSize());
    for (auto* vertex : vertices)
    {
        const auto& p = vertex->GetPoint();
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        {
            return tl::unexpected("NonfiniteMeshCoordinate");
        }
        original.emplace(vertex, Vertex{ original.size(), bits(p) });
    }
    using Boundary = std::map<std::pair<std::size_t, std::size_t>, std::int64_t>;
    const auto accumulate = [&](SmPolyFace& face, Boundary& boundary, int sign, std::size_t expected) -> bool
    {
        SmTArray<SmPolyLoop*> loops;
        face.GetPolyLoops(loops);
        if (loops.GetSize() != 1)
        {
            return false;
        }
        SmTArray<SmPolyEdge*> edges;
        loops[0]->GetPolyEdges(edges);
        if (edges.GetSize() != expected)
        {
            return false;
        }
        for (auto* edge : edges)
        {
            const auto a = original.find(edge->GetStartPolyVertex());
            const auto b = original.find(edge->GetEndPolyVertex());
            if (a == original.end() || b == original.end() || a->second.index == b->second.index)
            {
                return false;
            }
            const auto u = a->second.index, v = b->second.index;
            boundary[{ std::min(u, v), std::max(u, v) }] += u < v ? sign : -sign;
        }
        return true;
    };
    for (const auto& polygon : polygons)
    {
        Boundary boundary;
        if (!accumulate(*polygon.face, boundary, 1, polygon.edges))
        {
            return tl::unexpected("DisplayPolygonBoundaryUnestablished");
        }
        // ConvertToPolyBrep clears general back-pointer validity. OriginalFace
        // is ONLY an opaque label resolved against the still-live display BRep;
        // do not dereference it or manufacture a source STEP face identity.
        auto* label = polygon.face->GetOriginalFace();
        SmTArray<SmPolyFace*> children;
        if (polygon.face->TriangulateNonPlanar(children) != SM_SUCCESS || children.GetSize() != polygon.edges - 3)
        {
            return tl::unexpected("DisplayPolygonTriangulationFailed");
        }
        polygon.face->SetOriginalFace(label, FALSE);
        if (!accumulate(*polygon.face, boundary, -1, 3))
        {
            return tl::unexpected("DisplayPolygonTriangulationUnestablished");
        }
        std::unordered_set<SmPolyFace*> seen{ polygon.face };
        for (auto* child : children)
        {
            if (!child || child->GetPolyBrep() != &mesh || !seen.insert(child).second)
            {
                return tl::unexpected("DisplayPolygonTriangulationUnestablished");
            }
            child->SetOriginalFace(label, FALSE);
            if (child->GetOriginalFace() != label || !accumulate(*child, boundary, -1, 3))
            {
                return tl::unexpected("DisplayPolygonTriangulationUnestablished");
            }
        }
        // Internal diagonals cancel; every original directed boundary edge
        // must survive with exactly its original orientation/multiplicity.
        for (const auto& entry : boundary)
        {
            if (entry.second != 0)
            {
                return tl::unexpected("DisplayPolygonBoundaryChanged");
            }
        }
    }
    mesh.GetPolyVertices(vertices);
    if (vertices.GetSize() != original.size())
    {
        return tl::unexpected("DisplayPolygonVerticesChanged");
    }
    for (auto* vertex : vertices)
    {
        const auto found = original.find(vertex);
        if (found == original.end() || found->second.bits != bits(vertex->GetPoint()))
        {
            return tl::unexpected("DisplayPolygonVerticesChanged");
        }
    }
    mesh.GetPolyFaces(faces);
    if (faces.GetSize() != planned)
    {
        return tl::unexpected("DisplayPolygonTriangleCountChanged");
    }
    for (auto* face : faces)
    {
        SmTArray<SmPolyLoop*> loops;
        face->GetPolyLoops(loops);
        if (loops.GetSize() != 1)
        {
            return tl::unexpected("DisplayPolygonTriangulationUnestablished");
        }
        SmTArray<SmPolyEdge*> edges;
        loops[0]->GetPolyEdges(edges);
        if (edges.GetSize() != 3)
        {
            return tl::unexpected("DisplayPolygonTriangulationUnestablished");
        }
    }
    return planned;
}
} // namespace step_view
