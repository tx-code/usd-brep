// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <SmAssertArray.h>
#include <SmBrepData.h>
#include <SmNativeArchive.h>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace step_import;
using step_import_test::Near;
using step_import_test::Require;
namespace
{
void Surface(const SmSurface& surface)
{
    Require(surface.GetType() == SmBSplineSurface_TYPE, "plain source carrier retained");
    const auto& spline = static_cast<const SmBSplineSurface&>(surface);
    double *net = nullptr, *u = nullptr, *v = nullptr;
    ULONG nu = 0, nv = 0, ku = 0, kv = 0;
    Require(spline.GetControlPointsPointer(nu, nv, net) == SM_SUCCESS && nu == 2 && nv == 2, "source carrier control shape retained");
    Require(spline.GetKnotsPointers(ku, kv, u, v) == SM_SUCCESS && ku == 4 && kv == 4, "source carrier knots retained");
    for (ULONG i = 0; i < 4; ++i)
    {
        Require(u[i] == (i < 2 ? -1 : 2) && v[i] == (i < 2 ? -1 : 3), "original UV domain unchanged");
        Require(
            net[4 * i] == (i < 2 ? -2 : 4) && net[4 * i + 1] == (i % 2 ? 9 : -3) && net[4 * i + 2] == 0 && net[4 * i + 3] == NL_NOW,
            "complete physical surface net unchanged"
        );
    }
}
void Verify(SmBrep& brep)
{
    SmAssertArray pointers, findings;
    Require(brep.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "LINE-pcurve model pointer check");
    Require(brep.AssertValid(&findings, SM_LEVEL_2, SM_WALK) && findings.GetSize() == 0, "LINE-pcurve full LEVEL2");
    step_import_test::CheckNativeWorkBudget(brep, .005);
    SmTArray<SmFace*> faces;
    SmTArray<SmEdge*> edges;
    SmTArray<SmVertex*> vertices;
    brep.GetFaces(faces);
    brep.GetEdges(edges);
    brep.GetVertices(vertices);
    Require(faces.GetSize() == 1 && edges.GetSize() == 3 && vertices.GetSize() == 3, "original open triangle topology");
    Surface(*faces[0]->GetSurface());
    std::size_t found = 0;
    for (ULONG i = 0; i < edges.GetSize(); ++i)
    {
        const auto* composition = dynamic_cast<const SmCrvOnSurf*>(edges[i]->GetCurve());
        if (!composition)
        {
            continue;
        }
        ++found;
        const auto* wrapper = dynamic_cast<const SmSTEPSurface*>(composition->GetBaseSurface());
        Require(composition->OwnsBaseSurface() && wrapper && wrapper->OwnsBaseSurface(), "definition owns independent STEP carrier");
        Surface(*wrapper->GetBaseSurface());
        const auto* uv = dynamic_cast<const SmBSplineCurve*>(composition->GetUVCurve());
        Require(uv && uv->GetDim() == 2 && uv->GetDegree() == 1 && !uv->IsRational(), "degree-one parameter window definition");
        SmTArray<SmPoint3d> points;
        SmTArray<double> weights, knots;
        Require(uv->GetControlPolygon(points, weights) == SM_SUCCESS && points.GetSize() == 2, "both complete UV endpoints");
        Require(uv->GetKnotsAll(knots) == SM_SUCCESS && knots.GetSize() == 4, "complete finite work-parameter knots");
        Require(points[0].x == -1 && points[0].y == -1 && points[1].x == 2 && points[1].y == 3, "whole line-carrier intersection retained");
        for (ULONG j = 0; j < 4; ++j)
        {
            Require(knots[j] == (j < 2 ? 0 : 1), "explicit native tau interval");
        }
        Require(edges[i]->GetInterval().GetMin() == 0 && edges[i]->GetInterval().GetMax() == 1, "selected edge uses full native tau interval");
        // Independent analytic truth, not the native inverse/drop operation.
        for (double t : { 0., .125, .5, .875, 1. })
        {
            SmVector3d values[3];
            Require(composition->Evaluate(t, 2, TRUE, values) == SM_SUCCESS, "actual composition evaluation");
            Near(values[0].x, -2 + 6 * t, 1e-12, "analytic LINE x");
            Near(values[0].y, -3 + 12 * t, 1e-12, "analytic LINE y");
            Near(values[0].z, 0, 1e-12, "analytic LINE z");
            Near(values[1].x, 6, 1e-12, "mapped tau derivative x");
            Near(values[1].y, 12, 1e-12, "mapped tau derivative y");
            Near(values[2].x, 0, 1e-12, "linear second derivative x");
            Near(values[2].y, 0, 1e-12, "linear second derivative y");
        }
    }
    Require(found == 1, "one source-priority LINE composition retained");
}
void Run(const std::filesystem::path& fixture, const std::filesystem::path& directory)
{
    std::ifstream input(fixture, std::ios::binary);
    const std::string source{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    options.numericalIntegration = NumericalIntegrationSettings{ 1e-8 };
    auto result = ImportBytes(source, options);
    if (result.status != Status::Accepted)
    {
        for (const auto& diagnostic : result.report.diagnostics)
        {
            std::cerr << diagnostic.source.key << " " << diagnostic.source.message << '\n';
        }
    }
    Require(result.status == Status::Accepted && result.model, "complete source LINE-pcurve import");
    auto model = result.model;
    Require(model->Source()->Source().Bytes() == source, "original LINE, vector, UV, units and lexemes retained");
    Require(model->Info().shapeRole == "OpenSurfaceModel", "surface role retained");
    Near(model->Info().areaMm2, 36, 1e-7, "independent triangle area");
    Require(!model->MaterialVolumeMm3(), "triangle has no material-volume qualification");
    const auto mappings = model->PcurveLineWindows();
    Require(mappings.size() == 1 && mappings[0].targetEdgeIndex, "owning window bound to actual target edge");
    Require(mappings[0].equation.find("(2/1)+(1/1)*native_tau") != std::string::npos, "exact q window and affine map");
    Require(mappings[0].equation.find("source_VECTOR_magnitude") != std::string::npos, "source t is not native tau identity");
    auto copy = model->CopyForModeling();
    Require(copy.status == Status::Accepted && copy.model, "independent full modeling copy");
    copy.model->Edit(Verify);
    std::filesystem::create_directories(directory);
    for (auto format : { SM_BINARY, SM_ASCII })
    {
        const auto path = directory / (format == SM_BINARY ? "line.smb" : "line.txt");
        copy.model->Edit(
            [&](SmBrep& brep)
            {
                SmTArray<SmCurve*> curves;
                SmTArray<SmSurface*> surfaces;
                SmTArray<long> trees;
                SmTArray<SmBrep*> bodies;
                bodies.Add(&brep);
                Require(
                    SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, bodies, format, TRUE, FALSE) == SM_SUCCESS,
                    "complete LINE composition archive"
                );
            }
        );
        SmContext context;
        SmTArray<SmCurve*> curves;
        SmTArray<SmSurface*> surfaces;
        SmTArray<long> trees;
        SmTArray<SmBrep*> bodies;
        SmObjsDelete<SmCurve*> dc(&curves);
        SmObjsDelete<SmSurface*> ds(&surfaces);
        SmObjsDelete<SmBrep*> db(&bodies);
        Require(
            SmNativeArchive::ReadPartFromFile(context, path.c_str(), curves, surfaces, trees, bodies, format) == SM_SUCCESS && bodies.GetSize() == 1,
            "LINE composition archive replay"
        );
        Verify(*bodies[0]);
    }
    const auto reportSize = result.report.json.size();
    Require(result.report.exactGeometryWork && result.report.exactGeometryWork->usedUnits > 0, "exact LINE-window work charged");
    const auto work = result.report.exactGeometryWork->usedUnits;
    result = {};
    Require(model->PcurveLineWindows().size() == 1 && model->Source()->Source().Bytes() == source, "snapshot owns window after report release");
    model.reset();
    copy.model->Edit(Verify);
    for (auto phase : { Phase::BeforeNativeCall, Phase::NativeReady, Phase::BeforeCommit })
    {
        auto stopped = options;
        stopped.execution.control = std::make_shared<ImportControl>();
        stopped.execution.progress = [&, phase](Progress p)
        {
            if (p.phase == phase)
            {
                stopped.execution.control->RequestCancel();
            }
        };
        const auto cancelled = ImportBytes(source, stopped);
        Require(cancelled.status == Status::Cancelled && !cancelled.model, "cancelled LINE import never partially publishes");
    }
    for (auto budget : { work - 1, work })
    {
        auto bounded = options;
        bounded.nativeLimits.exactWorkUnits = budget;
        const auto limited = ImportBytes(source, bounded);
        Require(
            budget == work ? limited.status == Status::Accepted && bool(limited.model) : limited.status == Status::ResourceLimit && !limited.model,
            "whole import exact work N/N-1"
        );
    }
    auto bounded = options;
    bounded.nativeLimits.reportBytes = reportSize - 1;
    const auto limited = ImportBytes(source, bounded);
    Require(limited.status == Status::ResourceLimit && !limited.model, "required LINE map report refuses without partial publication");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "fixture and disposable archive directory required");
        Run(argv[1], argv[2]);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
