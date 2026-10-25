// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepViewMesh.h"

#include <SmApiGeneral.h>
#include <SmApiPrimitives.h>
#include <SmBrep.h>
#include <SmContext.h>
#include <SmFace.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
using Bits = std::array<std::uint64_t, 3>;
using PointMap = std::map<SmPolyVertex*, Bits>;
PointMap Capture(SmPolyBrep& mesh)
{
    SmTArray<SmPolyVertex*> vertices;
    mesh.GetPolyVertices(vertices);
    PointMap result;
    for (auto* vertex : vertices)
    {
        const auto& p = vertex->GetPoint();
        result.emplace(vertex, Bits{ std::bit_cast<std::uint64_t>(p.x), std::bit_cast<std::uint64_t>(p.y), std::bit_cast<std::uint64_t>(p.z) });
    }
    return result;
}
struct Fixture
{
    // This primitive supplies real, live opaque face labels only. The authored
    // polygons below are transport controls, not an approximation claim about
    // these primitive faces or a source-import qualification.
    std::unique_ptr<SmBrep> labels;
    std::unique_ptr<SmPolyBrep> mesh;
    SmTArray<SmFace*> original;
    Fixture()
    {
        SmBrep* raw = nullptr;
        Require(SmApiCreateBox(SmVector3d(0, 0, 0), 1, 1, 1, raw) == SM_SUCCESS && raw, "public label primitive");
        labels.reset(raw);
        labels->GetFaces(original);
        Require(original.GetSize() == 6, "public label inventory");
        mesh.reset(new (*labels->GetContext()) SmPolyBrep(1e-9));
    }
    SmPolyFace* Add(const std::vector<SmPoint3d>& points, ULONG label = 0)
    {
        SmTArray<SmPoint3d> native;
        for (const auto& point : points)
        {
            native.Add(point);
        }
        SmPolyFace* face = nullptr;
        Require(mesh->CreatePoly(nullptr, native, face) == SM_SUCCESS && face, "public polygon creation");
        face->SetOriginalFace(original[label], FALSE);
        return face;
    }
    void LabelsUnchanged()
    {
        SmTArray<SmFace*> after;
        labels->GetFaces(after);
        Require(after.GetSize() == original.GetSize(), "derived conversion leaves native face inventory unchanged");
        for (ULONG i = 0; i < after.GetSize(); ++i)
        {
            Require(after[i] == original[i], "derived conversion leaves native face identity unchanged");
        }
    }
};
std::map<SmFace*, double> Areas(SmPolyBrep& mesh, bool positive = true)
{
    SmTArray<SmPolyFace*> faces;
    mesh.GetPolyFaces(faces);
    std::map<SmFace*, double> result;
    for (auto* face : faces)
    {
        SmTArray<SmPolyLoop*> loops;
        face->GetPolyLoops(loops);
        Require(loops.GetSize() == 1, "each output cell keeps one loop");
        SmTArray<SmPolyEdge*> edges;
        face->GetOuterPolyLoop()->GetPolyEdges(edges);
        Require(edges.GetSize() == 3, "output cell has exactly three edges");
        const auto& a = edges[0]->GetStartPolyVertex()->GetPoint();
        const auto& b = edges[1]->GetStartPolyVertex()->GetPoint();
        const auto& c = edges[2]->GetStartPolyVertex()->GetPoint();
        const double area = ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) / 2;
        Require(positive ? area > 0 : area < 0, "triangle winding preserves the authored XY orientation");
        result[face->GetOriginalFace()] += area;
    }
    return result;
}
void Polygons()
{
    const std::vector<SmPoint3d> quad{ { 0, 0, 0 }, { 2, 0, 0 }, { 2, 1, 0 }, { 0, 1, 0 } };
    const std::vector<SmPoint3d> concave{ { 0, 0, 0 }, { 2, 0, 0 }, { 2, 1, 0 }, { 1, 1, 0 }, { 1, 2, 0 }, { 0, 2, 0 } };
    auto warped = quad;
    warped[2].z = .125;
    for (const auto& points : { quad, concave, warped })
    {
        for (bool reverse : { false, true })
        {
            Fixture fixture;
            auto ring = points;
            if (reverse)
            {
                std::reverse(ring.begin(), ring.end());
            }
            fixture.Add(ring);
            const auto before = Capture(*fixture.mesh);
            const auto made = step_view::TriangulateDisplayMesh(*fixture.mesh, ring.size() - 2);
            Require(made && *made == ring.size() - 2, "regular polygon becomes exactly n-2 triangles");
            Require(Capture(*fixture.mesh) == before, "whole vertex set and XYZ bits stay unchanged");
            const auto areas = Areas(*fixture.mesh, !reverse);
            const double expected = points.size() == 6 ? 3 : 2;
            Require(
                areas.size() == 1 && std::abs(areas.at(fixture.original[0]) - (reverse ? -expected : expected)) < 1e-12,
                "independent analytic projected area and source label are preserved"
            );
            fixture.LabelsUnchanged();
        }
    }
}
void MixedAndLimits()
{
    Fixture fixture;
    const auto* triangle = fixture.Add({ { 4, 0, 0 }, { 5, 0, 0 }, { 4, 1, 0 } }, 1);
    fixture.Add({ { 0, 0, 0 }, { 2, 0, 0 }, { 2, 1, 0 }, { 0, 1, 0 } });
    const auto before = Capture(*fixture.mesh);
    auto refused = step_view::TriangulateDisplayMesh(*fixture.mesh, 2);
    Require(!refused && refused.error() == "DisplayMeshLimit", "projected N-1 refuses before any split");
    Require(Capture(*fixture.mesh) == before, "capacity refusal keeps original points");
    SmTArray<SmPolyFace*> unsplit;
    fixture.mesh->GetPolyFaces(unsplit);
    Require(unsplit.GetSize() == 2, "capacity refusal leaves every input polygon unchanged");
    const auto made = step_view::TriangulateDisplayMesh(*fixture.mesh, 3);
    Require(made && *made == 3, "exact projected N succeeds");
    const auto area = Areas(*fixture.mesh);
    Require(area.at(fixture.original[0]) == 2 && area.at(fixture.original[1]) == .5, "mixed labels keep their independent areas");
    SmTArray<SmPolyFace*> after;
    fixture.mesh->GetPolyFaces(after);
    bool retained = false;
    for (auto* face : after)
    {
        retained = retained || face == triangle;
    }
    Require(retained, "existing triangle is not replaced");
    Require(Capture(*fixture.mesh) == before, "mixed conversion leaves points unchanged");
    Require(bool(step_view::TriangulateDisplayMesh(*fixture.mesh, 3)), "already triangular path is reusable");
    fixture.LabelsUnchanged();
}
void UnsupportedLoops()
{
    Fixture fixture;
    auto* face = fixture.Add({ { 0, 0, 0 }, { 2, 0, 0 }, { 2, 1, 0 }, { 0, 1, 0 } });
    // An intermediate extra loop must never be silently discarded. It is an
    // unsupported display input, not evidence of a valid imported source face.
    (void)new (fixture.mesh.get()) SmPolyLoop(face);
    const auto before = Capture(*fixture.mesh);
    const auto refused = step_view::TriangulateDisplayMesh(*fixture.mesh, 100);
    Require(!refused && refused.error() == "DisplayPolygonLoopsUnsupported", "extra loops cannot disappear during display conversion");
    Require(Capture(*fixture.mesh) == before, "unsupported input is not moved");
}
void WholeMeshPreflight()
{
    Fixture fixture;
    fixture.Add({ { 0, 0, 0 }, { 2, 0, 0 }, { 2, 1, 0 }, { 0, 1, 0 } });
    fixture.Add({ { 4, 0, 0 }, { 6, 0, 0 }, { 6, 1, 0 }, { 4, 1, 0 } }, 1);
    const auto before = Capture(*fixture.mesh);
    const auto refused = step_view::TriangulateDisplayMesh(*fixture.mesh, 3);
    Require(!refused && refused.error() == "DisplayMeshLimit", "later polygon overflow refuses the whole planned mesh");
    SmTArray<SmPolyFace*> faces;
    fixture.mesh->GetPolyFaces(faces);
    Require(faces.GetSize() == 2, "later overflow does not split an earlier polygon");
    for (auto* face : faces)
    {
        SmTArray<SmPolyEdge*> edges;
        face->GetOuterPolyLoop()->GetPolyEdges(edges);
        Require(edges.GetSize() == 4, "every original ring remains intact on preflight refusal");
    }
    Require(Capture(*fixture.mesh) == before, "preflight preserves all point bits");
    const auto made = step_view::TriangulateDisplayMesh(*fixture.mesh, 4);
    Require(made && *made == 4, "exact whole-mesh triangle capacity succeeds");
}
} // namespace
int main()
{
    try
    {
        SmApiCreateContext();
        Polygons();
        MixedAndLimits();
        WholeMeshPreflight();
        UnsupportedLoops();
        std::cout << "Display polygon triangulation contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
