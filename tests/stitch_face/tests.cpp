// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmApiBrep.h>
#include <SmApiPrimitives.h>
#include <SmApiQueries.h>
#include <SmAssertArray.h>
#include <SmBrep.h>
#include <SmEdge.h>
#include <SmFace.h>
#include <SmVertex.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#    include <Windows.h>
#endif

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
    Require(status == SM_SUCCESS, "native fixture/query operation failed");
}
std::unique_ptr<SmBrep> Primitive(bool box)
{
    SmBrep* raw = nullptr;
    const auto status = box ? SmApiCreateBox(SmVector3d(0, 0, 0), 10, 20, 30, raw) : SmApiCreatePlane(SmVector3d(0, 0, 0), 10, 20, raw);
    std::unique_ptr<SmBrep> owner(raw);
    Good(status);
    Require(bool(owner), "public primitive produces a BRep");
    return owner;
}
void Validate(SmBrep& body)
{
    SmAssertArray reports;
    Good(body.ValidatePointers(&reports));
    Require(reports.GetSize() == 0, "pointer/geometry validation reports remain empty");
    Require(body.AssertValid(&reports, SM_LEVEL_2, SM_WALK) && reports.GetSize() == 0, "full topology/geometry validation passes");
}
void WithoutPartners(bool box)
{
    auto body = Primitive(box);
    SmTArray<SmFace*> faces;
    Good(SmApiGetFaces(body.get(), faces));
    Require(faces.GetSize() == (box ? 6u : 1u), "analytic primitive face inventory");
    const double tolerance = body->GetTolerance();
    // The closed box has no lamina edges; the isolated sheet has no partner
    // face. Both are valid uses of the public own-face overload and require its
    // single-BRep default-tolerance initialization before any topology work.
    Good(SmApiStitch(body.get(), faces[0]));
    // A mutating stitch can invalidate borrowed topology views: reacquire them.
    faces.SetSize(0);
    Good(SmApiGetFaces(body.get(), faces));
    SmTArray<SmEdge*> edges;
    SmTArray<SmVertex*> vertices;
    body->GetEdges(edges);
    body->GetVertices(vertices);
    Require(
        faces.GetSize() == (box ? 6u : 1u) && edges.GetSize() == (box ? 12u : 4u) && vertices.GetSize() == (box ? 8u : 4u),
        "a stitch without partner geometry preserves the analytic topology"
    );
    Require(body->GetTolerance() == tolerance, "an exact no-op does not enlarge working tolerance");
    double area = 0;
    Good(SmApiBrepComputeArea(body.get(), 1e-8, area));
    Require(std::abs(area - (box ? 2200. : 200.)) < 1e-8, "analytic area is unchanged");
    SmBoolean solid = FALSE;
    Good(SmApiBrepIsManifoldSolid(body.get(), solid));
    Require((solid != FALSE) == box, "stitching an isolated sheet does not create material-solid semantics");
    if (box)
    {
        double volume = 0;
        Good(SmApiBrepComputeVolume(body.get(), 1e-8, volume));
        Require(std::abs(volume - 6000.) < 1e-8, "analytic box volume is unchanged");
    }
    Validate(*body);
}
void AdjacentSheets()
{
    auto body = Primitive(false);
    SmTArray<SmFace*> faces;
    Good(SmApiGetFaces(body.get(), faces));
    const auto original = faces[0];
    SmBrep* raw = nullptr;
    const auto created = SmApiCreatePlane(SmVector3d(10, 0, 0), 10, 20, raw);
    std::unique_ptr<SmBrep> source(raw);
    Good(created);
    Require(bool(source), "second analytic sheet exists");
    // MergeBrep copies the defining geometry and disconnected topology. The
    // source stays owned independently; stitch the copied face in the target.
    Good(body->MergeBrep(*source));
    faces.SetSize(0);
    Good(SmApiGetFaces(body.get(), faces));
    Require(faces.GetSize() == 2, "two disconnected adjacent sheets before stitching");
    auto* added = faces[0] == original ? faces[1] : faces[0];
    Good(SmApiStitch(body.get(), added));
    faces.SetSize(0);
    Good(SmApiGetFaces(body.get(), faces));
    SmTArray<SmEdge*> edges;
    SmTArray<SmVertex*> vertices;
    body->GetEdges(edges);
    body->GetVertices(vertices);
    // Two 10x20 rectangles share one complete edge and its two endpoints.
    Require(faces.GetSize() == 2 && edges.GetSize() == 7 && vertices.GetSize() == 6, "one shared edge and its endpoints are actually stitched");
    double area = 0;
    Good(SmApiBrepComputeArea(body.get(), 1e-8, area));
    Require(std::abs(area - 400.) < 1e-8, "stitching preserves the exact two-sheet area");
    Validate(*body);
    Validate(*source);
}
void ForeignFace()
{
    auto target = Primitive(true);
    auto other = Primitive(false);
    SmTArray<SmFace*> foreign;
    Good(SmApiGetFaces(other.get(), foreign));
    Require(SmApiStitch(target.get(), foreign[0]) == SM_ERR_INVALID_INPUT, "a face outside the target is rejected");
    Require(
        SmApiStitch(nullptr, foreign[0]) == SM_ERR_INVALID_INPUT && SmApiStitch(target.get(), nullptr) == SM_ERR_INVALID_INPUT,
        "required null arguments remain invalid inputs"
    );
    Validate(*target);
    Validate(*other);
    SmTArray<SmFace*> faces;
    Good(SmApiGetFaces(target.get(), faces));
    Require(faces.GetSize() == 6, "foreign-face rejection leaves the target intact");
    faces.SetSize(0);
    Good(SmApiGetFaces(other.get(), faces));
    Require(faces.GetSize() == 1, "foreign-face rejection leaves its actual owner intact");
}
void TwoBrepBooleans()
{
    const std::array operations{ SM_BO_UNION, SM_BO_DIFFERENCE, SM_BO_INTERSECTION };
    for (const auto operation : operations)
    {
        auto first = Primitive(true);
        SmBrep* raw = nullptr;
        const auto created = SmApiCreateBox(SmVector3d(5, 0, 0), 10, 20, 30, raw);
        std::unique_ptr<SmBrep> second(raw);
        Good(created);
        Require(bool(second), "second Boolean operand exists");
        SmBrep* result = nullptr;
        const auto status = SmApiBoolean(first.get(), second.get(), operation, result);
        if (status == SM_SUCCESS)
        {
            second.release(); // Successful two-Brep Booleans consume the second operand.
        }
        Good(status);
        Require(result == first.get(), "Boolean keeps its documented primary-owner result");
        double volume = 0;
        Good(SmApiBrepComputeVolume(result, 1e-8, volume));
        // Each box has volume 6000; their analytic overlap is 5x20x30=3000.
        Require(std::abs(volume - (operation == SM_BO_UNION ? 9000. : 3000.)) < 1e-7, "two-operand default tolerance keeps Boolean volume");
        Validate(*result);
    }
}
} // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    try
    {
        Require(argc == 2, "stitch contract mode required");
        const std::string_view mode = argv[1];
        if (mode == "box" || mode == "plane")
        {
            WithoutPartners(mode == "box");
        }
        else if (mode == "adjacent")
        {
            AdjacentSheets();
        }
        else if (mode == "foreign")
        {
            ForeignFace();
        }
        else if (mode == "boolean")
        {
            TwoBrepBooleans();
        }
        else
        {
            Require(false, "unknown stitch contract mode");
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
