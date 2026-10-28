// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"
#include "StepImportLinearExtrusion.h"

#include <SmAssertArray.h>
#include <SmLine.h>
#include <SmNativeArchive.h>
#include <fstream>
#include <iostream>
#include <set>

namespace
{
using namespace step_import;
using step_import_test::Near;
using step_import_test::Require;
Options Settings()
{
    auto o = Options::PlanarFixture();
    o.generalBrep = true;
    o.quality = Quality::GeneralDevelopment();
    return o;
}
void Replace(std::string& text, const std::string& a, const std::string& b)
{
    const auto at = text.find(a);
    Require(at != std::string::npos, "public extrusion mutation token");
    text.replace(at, a.size(), b);
}
double IndependentArea()
{
    // The independent polynomial oracle is C(t)=(12t(1-t),12t^2(1-t),0).
    // A perpendicular height-five extrusion has area 5*integral |C'(t)| dt.
    // Simpson quadrature qualifies this area observation, not import geometry.
    constexpr unsigned n = 4096;
    double sum = 0;
    for (unsigned i = 0; i <= n; ++i)
    {
        const double t = double(i) / n;
        sum += (i == 0 || i == n ? 1 : i % 2 ? 4 : 2) * 60 * std::hypot(1 - 2 * t, 2 * t - 3 * t * t);
    }
    return sum / (3 * n);
}
void Native(SmBrep& native)
{
    SmAssertArray pointers, deep;
    Require(native.ValidatePointers(&pointers) == SM_SUCCESS && !pointers.GetSize(), "closed extrusion pointers");
    Require(native.AssertValid(&deep, SM_LEVEL_2, SM_WALK) && !deep.GetSize(), "closed extrusion LEVEL2");
    step_import_test::CheckNativeWorkBudget(native, .001);
    SmTArray<SmFace*> faces;
    SmTArray<SmEdge*> edges;
    SmTArray<SmVertex*> vertices;
    native.GetFaces(faces);
    native.GetEdges(edges);
    native.GetVertices(vertices);
    Require(faces.GetSize() == 1 && edges.GetSize() == 5 && vertices.GetSize() == 4, "four ring children and one shared cut");
    unsigned seams = 0;
    for (auto edge : edges)
    {
        if (dynamic_cast<const SmLine*>(edge->GetCurve()))
        {
            ++seams;
            const auto a = edge->GetStartVertex()->GetPoint(), b = edge->GetEndVertex()->GetPoint();
            Require(a.x == 0 && a.y == 0 && b.x == 0 && b.y == 0 && std::abs(a.z - b.z) == 5, "cut at original natural U boundary");
        }
        else
        {
            const auto* spline = dynamic_cast<const SmBSplineCurve*>(edge->GetCurve());
            Require(spline && spline->GetDegree() == 3, "original boundary basis retained");
            ULONG count = 0;
            double* cp = nullptr;
            Require(spline->GetControlPointsPointer(count, cp) == SM_SUCCESS && count == 4, "complete source boundary net");
            SmTArray<double> knots;
            Require(spline->GetKnotsAll(knots) == SM_SUCCESS && knots.GetSize() == 8, "complete source knot vector");
            for (unsigned i = 0; i < 8; ++i)
            {
                Require(knots[i] == (i < 4 ? 0 : 1), "no rephasing or parameter normalization");
            }
        }
    }
    Require(seams == 1, "one seam with two opposite uses");
    SmTArray<SmEdgeuse*> uses;
    faces[0]->GetUpwardEdgeuses(uses);
    unsigned forward = 0, backward = 0;
    for (auto use : uses)
    {
        if (dynamic_cast<const SmLine*>(use->GetEdge()->GetCurve()))
        {
            use->GetOrientation() == SM_OT_SAME ? ++forward : ++backward;
        }
    }
    Require(forward == 1 && backward == 1, "seam incidence remains paired");
    using step_import::detail::construction::BoundLinearExtrusionSeam;
    const auto ok = []() -> step_import::detail::Result<void>
    {
        return {};
    };
    const auto work = [](std::size_t) -> step_import::detail::Result<void>
    {
        return {};
    };
    const std::array<Point, 2> endpoints{ Point{ 0, 0, 0 }, Point{ 0, 0, 5 } };
    const auto bound = BoundLinearExtrusionSeam(*faces[0]->GetSurface(), endpoints, Fraction(1, 1000000000), ok, work);
    Require(bound && *bound && **bound < 1e-9, "whole stored-cut bound");
    auto shifted = endpoints;
    shifted[1][0] = .1;
    const auto bad = BoundLinearExtrusionSeam(*faces[0]->GetSurface(), shifted, Fraction(1, 1000000000), ok, work);
    Require(bad && !*bad, "both endpoints and both U sides must meet the original bound");
    auto outside = endpoints;
    outside[1][2] = 1000;
    const auto extrapolated = BoundLinearExtrusionSeam(*faces[0]->GetSurface(), outside, Fraction(1, 1000000000), ok, work);
    Require(extrapolated && !*extrapolated, "no V extrapolation for the cut");
    const auto stopped = BoundLinearExtrusionSeam(
        *faces[0]->GetSurface(),
        endpoints,
        Fraction(1, 1000000000),
        []() -> step_import::detail::Result<void>
        {
            return tl::unexpected(step_import::detail::Failure{ Status::Cancelled, "native-sentinel", "sentinel" });
        },
        work
    );
    Require(!stopped && stopped.error().key == "native-sentinel", "native callback identity preserved");
    const auto exhausted = BoundLinearExtrusionSeam(
        *faces[0]->GetSurface(),
        endpoints,
        Fraction(1, 1000000000),
        ok,
        [](std::size_t) -> step_import::detail::Result<void>
        {
            return tl::unexpected(step_import::detail::Failure{ Status::ResourceLimit, "work-sentinel", "sentinel" });
        }
    );
    Require(!exhausted && exhausted.error().key == "work-sentinel", "work callback identity preserved");
}
Outcome Check(const std::string& text)
{
    auto result = ImportBytes(text, Settings());
    if (!result.model)
    {
        for (const auto& d : result.report.diagnostics)
        {
            std::cerr << d.source.key << '\n';
        }
    }
    Require(result.status == Status::Accepted && result.model, "complete closed extrusion import");
    Require(result.model->Source()->Source().Bytes() == text, "immutable source bytes");
    Require(result.model->Info().shapeRole == "OpenSurfaceModel" && !result.model->MaterialVolumeMm3(), "surface role retained");
    Near(result.model->Info().areaMm2, IndependentArea(), .002, "independent lateral area");
    const auto topology = result.model->TopologyNormalization();
    Require(
        topology && topology->completed && topology->sourceCurveIntervalsCovered && topology->sourceVerticesPreserved,
        "owning full source coverage"
    );
    Near(result.model->Face().EvaluateSurface(.5, 2.5)[0], 3., 1e-8, "unchanged U");
    Near(result.model->Face().EvaluateSurface(.5, 2.5)[1], 1.5, 1e-8, "unchanged U polynomial");
    Near(result.model->Face().EvaluateSurface(.5, 2.5)[2], 2.5, 1e-8, "unchanged scalar V");
    const auto closest = result.model->Closest({ 5, 1.5, 2.5 });
    Near(closest.distanceMm, 2., 1e-6, "independent maximum-X closest query");
    auto copy = result.model->CopyBrepForModeling();
    Require(copy.model != nullptr, "owning independent copy");
    copy.model->Edit(Native);
    return result;
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "fixture and output directory required");
        std::ifstream in(argv[1], std::ios::binary);
        const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        auto result = Check(text);
        const auto model = result.model;
        const auto calls = result.report.nativeCalls, exact = result.report.exactGeometryWork->usedUnits, bytes = result.report.json.size();
        const auto directory = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(directory);
        const auto archive = directory / "closed-extrusion.smb";
        model->WriteNative(archive);
        SmContext context;
        SmTArray<SmCurve*> curves;
        SmTArray<SmSurface*> surfaces;
        SmTArray<long> trees;
        SmTArray<SmBrep*> bodies;
        SmObjsDelete<SmCurve*> dc(&curves);
        SmObjsDelete<SmSurface*> ds(&surfaces);
        SmObjsDelete<SmBrep*> db(&bodies);
        Require(
            SmNativeArchive::ReadPartFromFile(context, archive.c_str(), curves, surfaces, trees, bodies, SM_BINARY) == SM_SUCCESS &&
                bodies.GetSize() == 1,
            "complete archive replay"
        );
        Native(*bodies[0]);
        auto copy = model->CopyBrepForModeling();
        copy.model->Edit(
            [&](SmBrep& original)
            {
                step_import_test::CompareNativeDefinitions(original, *bodies[0]);
            }
        );
        for (unsigned mode = 0; mode < 3; ++mode)
        {
            auto o = Settings();
            if (mode == 0)
            {
                o.nativeLimits.nativeCalls = calls;
            }
            if (mode == 1)
            {
                o.nativeLimits.reportBytes = bytes;
            }
            if (mode == 2)
            {
                o.nativeLimits.exactWorkUnits = exact;
            }
            Require(ImportBytes(text, o).status == Status::Accepted, "exact selected budget");
            if (mode == 0)
            {
                --o.nativeLimits.nativeCalls;
            }
            if (mode == 1)
            {
                --o.nativeLimits.reportBytes;
            }
            if (mode == 2)
            {
                --o.nativeLimits.exactWorkUnits;
            }
            auto failed = ImportBytes(text, o);
            Require(failed.status == Status::ResourceLimit && !failed.model, "N-1 publishes no partial result");
        }
        for (auto phase : { Phase::BeforeNativeCall, Phase::NativeReady, Phase::BeforeReport, Phase::BeforeCommit })
        {
            auto o = Settings();
            o.execution.control = std::make_shared<ImportControl>();
            o.execution.progress = [&](Progress p)
            {
                if (p.phase == phase)
                {
                    o.execution.control->RequestCancel();
                }
            };
            const auto failed = ImportBytes(text, o);
            Require(failed.status == Status::Cancelled && !failed.model, "cancel releases whole candidate");
        }
        auto noOuter = text;
        Replace(noOuter, "FACE_OUTER_BOUND('',#25", "FACE_BOUND('',#25");
        Check(noOuter);
        auto reversed = text;
        Replace(reversed, "#70=B_SPLINE_CURVE_WITH_KNOTS('',3,(#41,#42,#43,#41)", "#70=B_SPLINE_CURVE_WITH_KNOTS('',3,(#41,#43,#42,#41)");
        Replace(reversed, "#74=EDGE_CURVE('',#60,#60,#70,.T.)", "#74=EDGE_CURVE('',#60,#60,#70,.F.)");
        Check(reversed);
        auto wrong = text;
        Replace(wrong, "#81=ORIENTED_EDGE('',*,*,#75,.F.)", "#81=ORIENTED_EDGE('',*,*,#75,.T.)");
        Require(!ImportBytes(wrong, Settings()).model, "cut cannot bypass source region orientation");
        const auto materialPath = std::filesystem::path(argv[1]).parent_path() / "closed_extrusion_material.step";
        std::ifstream materialIn(materialPath, std::ios::binary);
        const std::string materialText{ std::istreambuf_iterator<char>(materialIn), std::istreambuf_iterator<char>() };
        const auto material = ImportBytes(materialText, Settings());
        Require(material.model && material.status == Status::Accepted, "shared closed rings retain complete caps");
        Require(
            material.model->Info().shapeRole == "MaterialSolid" && material.model->MaterialVolumeMm3(),
            "only explicit source solid grants material role"
        );
        Near(*material.model->MaterialVolumeMm3(), 12., 1e-5, "independent area 12/5 times height five volume");
        Near(material.model->Info().areaMm2, IndependentArea() + 24. / 5, .002, "independent shared-cap area");
        Require(
            material.model->Info().faces == 3 && material.model->Info().edges == 5 && material.model->Info().vertices == 4,
            "global cuts shared by lateral face and caps"
        );
        auto materialCopy = material.model->CopyBrepForModeling();
        Require(materialCopy.model != nullptr, "independent material copy");
        materialCopy.model->Edit(
            [](SmBrep& b)
            {
                SmAssertArray p, d;
                Require(
                    b.ValidatePointers(&p) == SM_SUCCESS && !p.GetSize() && b.AssertValid(&d, SM_LEVEL_2, SM_WALK) && !d.GetSize(),
                    "shared-cap copy validity"
                );
            }
        );
        result = {};
        Require(model->TopologyNormalization()->completed, "snapshot evidence survives report release");
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
