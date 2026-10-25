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
void VerifySurface(const SmSurface& surface)
{
    Require(surface.GetType() == SmBSplineSurface_TYPE, "plain defining B-spline surface retained");
    const auto& spline = static_cast<const SmBSplineSurface&>(surface);
    double *points = nullptr, *u = nullptr, *v = nullptr;
    ULONG nu = 0, nv = 0, ku = 0, kv = 0;
    Require(spline.GetControlPointsPointer(nu, nv, points) == SM_SUCCESS && nu == 2 && nv == 2 && points, "complete surface control inventory");
    Require(spline.GetKnotsPointers(ku, kv, u, v) == SM_SUCCESS && u && v, "all surface knots available");
    Require(ku == 4 && kv == 4, "source knot multiplicities retained");
    for (ULONG i = 0; i < 4; ++i)
    {
        Require(u[i] == (i < 2 ? -1 : 1) && v[i] == (i < 2 ? -1 : 1), "exact target chart knots");
        Require(
            points[4 * i] == (i < 2 ? -1 : 1) && points[4 * i + 1] == (i % 2 ? 1 : -1) && points[4 * i + 2] == 0 && points[4 * i + 3] == NL_NOW,
            "complete original physical control mesh unchanged"
        );
    }
}
void Verify(SmBrep& brep)
{
    SmAssertArray pointers, findings;
    Require(brep.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "complete model pointer checks");
    Require(brep.AssertValid(&findings, SM_LEVEL_2, SM_WALK) && findings.GetSize() == 0, "complete unfiltered native LEVEL2");
    step_import_test::CheckNativeWorkBudget(brep, .005);
    SmTArray<SmFace*> faces;
    SmTArray<SmEdge*> edges;
    brep.GetFaces(faces);
    brep.GetEdges(edges);
    Require(faces.GetSize() == 1 && edges.GetSize() == 2, "original open patch topology");
    VerifySurface(*faces[0]->GetSurface());
    std::size_t found = 0;
    for (ULONG i = 0; i < edges.GetSize(); ++i)
    {
        auto* composition = dynamic_cast<SmCrvOnSurf*>(edges[i]->GetCurve());
        if (!composition)
        {
            continue;
        }
        ++found;
        const auto* wrapper = dynamic_cast<const SmSTEPSurface*>(composition->GetBaseSurface());
        Require(wrapper && wrapper->OwnsBaseSurface(), "composed definition owns independent carrier");
        VerifySurface(*wrapper->GetBaseSurface());
        const auto* uv = dynamic_cast<const SmBSplineCurve*>(composition->GetUVCurve());
        Require(uv && uv->GetDim() == 2 && uv->GetDegree() == 2 && !uv->IsRational(), "source quadratic UV definition retained");
        SmTArray<SmPoint3d> points;
        SmTArray<double> weights, knots;
        Require(uv->GetControlPolygon(points, weights) == SM_SUCCESS && points.GetSize() == 3, "all UV coefficients available");
        Require(uv->GetKnotsAll(knots) == SM_SUCCESS && knots.GetSize() == 6, "all pcurve knots available");
        for (ULONG j = 0; j < 3; ++j)
        {
            Require(
                points[j].x == double(j) - 1 && points[j].y == (j == 1 ? -1 : 1) && points[j].z == 0,
                "UV coefficients reversibly scaled with same chart"
            );
        }
        for (ULONG j = 0; j < 6; ++j)
        {
            Require(knots[j] == (j < 3 ? 0 : 1), "source pcurve t knots unchanged");
        }
        Require(edges[i]->GetInterval().GetMin() == 0 && edges[i]->GetInterval().GetMax() == 1, "source edge interval retained");
        // These analytic observations test the actual native consumer. Exact
        // equivalence is established separately by the whole stored-net map.
        for (double t : { 0., .125, .5, .875, 1. })
        {
            SmVector3d values[3];
            Require(composition->Evaluate(t, 2, TRUE, values) == SM_SUCCESS, "native composition derivatives");
            const double x = 2 * t - 1;
            Near(values[0].x, x, 1e-12, "independent analytic x");
            Near(values[0].y, x * x, 1e-12, "independent analytic y");
            Near(values[0].z, 0, 1e-12, "independent analytic z");
            Near(values[1].x, 2, 1e-12, "original t derivative x");
            Near(values[1].y, 4 * x, 1e-12, "original t derivative y");
            Near(values[2].y, 8, 1e-12, "original t second derivative");
        }
    }
    Require(found == 1, "one original pcurve master, no duplicate topology");
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
            std::cerr << diagnostic.source.message << '\n';
        }
    }
    Require(result.status == Status::Accepted && result.model, "complete chart-normalized import accepted");
    auto model = result.model;
    const auto mappings = model->SplineCharts();
    Require(mappings.size() == 2, "owning carrier and target face correspondence");
    Require(model->Source()->Source().Bytes() == source, "immutable original source including UV and units retained");
    Near(model->Info().areaMm2, 4. / 3, 1e-7, "independent exact analytic area");
    Require(!model->MaterialVolumeMm3(), "open surface never upgraded to material");
    auto copy = model->CopyForModeling();
    Require(copy.status == Status::Accepted && copy.model, "independent complete modeling copy");
    copy.model->Edit(Verify);
    std::filesystem::create_directories(directory);
    for (auto format : { SM_BINARY, SM_ASCII })
    {
        const auto path = directory / (format == SM_BINARY ? "chart.smb" : "chart.txt");
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
                    "complete charted archive write"
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
            "complete charted archive replay"
        );
        Verify(*bodies[0]);
    }
    model->WriteNative(directory / "chart-linked.smb");
    const auto reportSize = result.report.json.size();
    Require(result.report.exactGeometryWork && result.report.exactGeometryWork->usedUnits > 0, "complete chart work charged to request ledger");
    const auto exactWork = result.report.exactGeometryWork->usedUnits;
    result = {};
    Require(model->SplineCharts().size() == 2 && model->Source()->Source().Bytes() == source, "snapshot owns evidence after caller report release");
    model.reset();
    copy.model->Edit(Verify);
    for (auto phase : { Phase::BeforeNativeCall, Phase::NativeReady, Phase::BeforeCommit })
    {
        auto stopped = options;
        stopped.execution.control = std::make_shared<ImportControl>();
        stopped.execution.progress = [&, phase](Progress progress)
        {
            if (progress.phase == phase)
            {
                stopped.execution.control->RequestCancel();
            }
        };
        const auto cancelled = ImportBytes(source, stopped);
        Require(cancelled.status == Status::Cancelled && !cancelled.model, "cancelled chart construction never publishes");
    }
    for (bool report : { false, true })
    {
        auto bounded = options;
        if (report)
        {
            bounded.nativeLimits.reportBytes = reportSize - 1;
        }
        else
        {
            bounded.nativeLimits.nativeCalls = 2;
        }
        const auto limited = ImportBytes(source, bounded);
        Require(limited.status == Status::ResourceLimit && !limited.model, "resource refusal never publishes a partial normalized model");
    }
    for (auto budget : { exactWork - 1, exactWork })
    {
        auto bounded = options;
        bounded.nativeLimits.exactWorkUnits = budget;
        const auto limited = ImportBytes(source, bounded);
        Require(
            budget == exactWork ? limited.status == Status::Accepted && bool(limited.model) :
                                  limited.status == Status::ResourceLimit && !limited.model,
            "full stored-definition work obeys exact N and N-1 request budgets"
        );
    }
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public fixture and disposable archive directory required");
        Run(argv[1], argv[2]);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
