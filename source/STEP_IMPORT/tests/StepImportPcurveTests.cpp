// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportDefinitionChecks.h"
#include "StepImportExecution.h"
#include "StepImportGeometryConstruction.h"
#include "StepImportPcurveConstruction.h"

#include <SmBrepData.h>
#include <SmCrvOnSurf.h>
#include <SmEdge.h>
#include <SmNativeArchive.h>
#include <SmSTEPSurface.h>
#include <iostream>

using namespace step_import;
using namespace step_import::detail;
using namespace step_import::detail::construction;
using step_import_test::Near;
using step_import_test::Require;
namespace
{
class Inputs : public GeometryConstruction
{
public:

    Inputs(std::shared_ptr<const step_semantics::Snapshot> source, const Options& options, Report& report, Execution& execution, SmContext& context)
        : GeometryConstruction(std::move(source), options, report, execution)
    {
        m_context = &context;
        scale = 1;
    }
};
void Verify(SmCurve& curve, bool revolution)
{
    auto* composition = dynamic_cast<SmCrvOnSurf*>(&curve);
    Require(composition && dynamic_cast<const SmSTEPSurface*>(composition->GetBaseSurface()), "exact STEP surface/parameter curve composition");
    const auto* uv = dynamic_cast<const SmBSplineCurve*>(composition->GetUVCurve());
    Require(uv && uv->GetDim() == 2 && uv->GetDegree() == 2 && !uv->IsRational(), "exact quadratic parameter definition");
    SmTArray<double> knots, weights;
    SmTArray<SmPoint3d> points;
    Require(uv->GetKnotsAll(knots) == SM_SUCCESS && knots.GetSize() == 6, "source parameter knot inventory");
    for (ULONG i = 0; i < 6; ++i)
    {
        Require(knots[i] == (i < 3 ? (revolution ? -3 : -2) : (revolution ? 9 : 8)), "original repeated source parameter knots");
    }
    Require(uv->GetControlPolygon(points, weights) == SM_SUCCESS && points.GetSize() == 3, "source parameter controls");
    for (ULONG i = 0; i < 3; ++i)
    {
        const double expected = revolution ? (i == 0 ? -2 : i == 1 ? 0 : 8) : (i == 0 ? 0 : i == 1 ? 2 : 10);
        Require(points[i].x == 0 && points[i].y == expected && points[i].z == 0, "original UV coefficients");
    }
    for (double part : { 0., .2, .5, 1. })
    {
        const double t = revolution ? -3 + 12 * part : -2 + 10 * part;
        const double a = part;
        SmVector3d values[3];
        Require(curve.Evaluate(t, 2, TRUE, values) == SM_SUCCESS, "composition position and derivatives");
        if (revolution)
        {
            // v=-2+4a+6a^2 feeds the retained rational generator parameter,
            // not physical height: z=14s/(1+s), s=(v+2)/10.
            const double s = (4 * a + 6 * a * a) / 10, first = (1. / 3 + a) / 10, second = 1. / 120;
            Near(values[0].x, 0, 1e-10, "independent nested revolution x");
            Near(values[0].y, 3, 1e-10, "independent nested revolution y");
            Near(values[0].z, 14 * s / (1 + s), 1e-10, "independent nested rational image");
            Near(values[1].z, 14 * first / std::pow(1 + s, 2), 1e-10, "independent nested first derivative");
            Near(
                values[2].z,
                14 * second / std::pow(1 + s, 2) - 28 * first * first / std::pow(1 + s, 3),
                1e-10,
                "independent nested second derivative"
            );
            continue;
        }
        Near(values[0].x, 2 + 4 * a + 6 * a * a, 1e-10, "independent nonaffine image x");
        Near(values[0].y, -3, 1e-10, "independent nonaffine image y");
        Near(values[0].z, 5, 1e-10, "independent nonaffine image z");
        Near(values[1].x, .4 + 1.2 * a, 1e-10, "source first derivative is not auxiliary LINE derivative");
        Near(values[2].x, .12, 1e-10, "source second derivative retained through composition");
    }
}
void VerifyBrep(SmBrep& brep, bool revolution)
{
    SmTArray<SmEdge*> edges;
    brep.GetEdges(edges);
    unsigned found = 0;
    for (ULONG i = 0; i < edges.GetSize(); ++i)
    {
        if (dynamic_cast<SmCrvOnSurf*>(edges[i]->GetCurve()))
        {
            Verify(*edges[i]->GetCurve(), revolution);
            Require(
                edges[i]->GetInterval().GetMin() == (revolution ? -3 : -2) && edges[i]->GetInterval().GetMax() == (revolution ? 9 : 8),
                "installed master interval"
            );
            ++found;
        }
    }
    Require(found == 1, "complete BRep retains one source master definition");
    step_import_test::CheckNativeWorkBudget(brep, .005);
}
void Lifecycle(const std::filesystem::path& fixture, const std::filesystem::path& directory, bool revolution)
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    options.numericalIntegration = NumericalIntegrationSettings{ 1e-8 };
    auto result = ImportFile(fixture, options);
    Require(result.status == Status::Accepted && result.model && !result.report.json.empty(), "complete source master import and report");
    auto model = result.model;
    Near(*model->MaterialVolumeMm3(), revolution ? 63 * std::numbers::pi : 6000, .001, "independent volume");
    Near(model->Info().areaMm2, revolution ? 60 * std::numbers::pi : 2200, .001, "independent area");
    Near(model->Closest(revolution ? Point{ 5, 0, 3 } : Point{ 20, 7, 20 }).distanceMm, revolution ? 2 : 8, 1e-6, "independent boundary query");
    Require(model->ClassifyMaterial(revolution ? Point{ 0, 0, 3 } : Point{ 7, 7, 20 }) == MaterialPointClass::Inside, "independent material query");
    auto copy = model->CopyForModeling();
    Require(copy.status == Status::Accepted && copy.model, "owning complete modeling copy");
    const auto verify = [revolution](SmBrep& brep)
    {
        VerifyBrep(brep, revolution);
    };
    copy.model->Edit(verify);
    const auto path = directory / "pcurve-brep.smb";
    model->WriteNative(path);
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    SmObjsDelete<SmCurve*> dc(&curves);
    SmObjsDelete<SmSurface*> ds(&surfaces);
    SmObjsDelete<SmBrep*> db(&breps);
    Require(
        SmNativeArchive::ReadPartFromFile(context, path.c_str(), curves, surfaces, trees, breps, SM_BINARY) == SM_SUCCESS && breps.GetSize() == 1 &&
            curves.GetSize() == 0 && surfaces.GetSize() == 0,
        "complete source pcurve archive inventory"
    );
    VerifyBrep(*breps[0], revolution);
    model.reset();
    result = {};
    copy.model->Edit(verify); // No borrowed child from the discarded snapshot.
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
        auto cancelled = ImportFile(fixture, stopped);
        Require(
            cancelled.status == Status::Cancelled && !cancelled.model && !cancelled.report.json.empty(),
            "cancelled master import never publishes"
        );
    }
    auto bounded = options;
    bounded.nativeLimits.nativeCalls = 2;
    const auto limited = ImportFile(fixture, bounded);
    Require(limited.status == Status::ResourceLimit && !limited.model, "native resource refusal is atomic");
    bounded = options;
    bounded.nativeLimits.reportBytes = 1;
    const auto noReport = ImportFile(fixture, bounded);
    Require(noReport.status == Status::ResourceLimit && !noReport.model, "complete required report precedes publication");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3 || (argc == 4 && std::string_view(argv[3]) == "revolution"), "public source pcurve fixture and archive directory required");
        const bool revolution = argc == 4;
        const auto raw = step_source::InspectFile(argv[1], step_source::Limits::PlanarFixtures());
        const auto checked = step_semantics::Check(raw, {}, step_semantics::SourcePolicy::Strict, step_semantics::GeometryScope::GeneralBrep);
        Require(checked.snapshot && checked.status == step_semantics::Status::Checked, "qualified public source prerequisites");
        const auto record = std::size_t(raw.source->Find(revolution ? "961" : "921") - raw.source->Records().data());
        auto options = Options::PlanarFixture();
        options.generalBrep = true;
        options.quality = Quality::GeneralDevelopment();
        Report report;
        Execution execution(options, report);
        SmContext context;
        Inputs input(checked.snapshot, options, report, execution, context);
        auto built = MakeSourcePcurve(input, record, record);
        Require(bool(built), built ? "source master factory" : built.error().key.c_str());
        Verify(*built->curve, revolution);
        SmCurve* rawCopy = nullptr;
        Require(built->curve->Copy(context, rawCopy) == SM_SUCCESS && rawCopy, "owned exact composition copy");
        std::unique_ptr<SmCurve> copy(rawCopy);
        built->curve.reset();
        Verify(*copy, revolution);
        std::filesystem::create_directories(argv[2]);
        for (auto format : { SM_BINARY, SM_ASCII })
        {
            SmTArray<SmCurve*> curves;
            SmTArray<SmSurface*> surfaces;
            SmTArray<long> trees;
            SmTArray<SmBrep*> breps;
            curves.Add(copy.get());
            const auto path = std::filesystem::path(argv[2]) / (format == SM_BINARY ? "pcurve.smb" : "pcurve.smt");
            Require(
                SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, breps, format, TRUE, FALSE) == SM_SUCCESS,
                "source master archive write"
            );
            SmContext readContext;
            SmTArray<SmCurve*> readCurves;
            SmTArray<SmSurface*> readSurfaces;
            SmTArray<long> readTrees;
            SmTArray<SmBrep*> readBreps;
            SmObjsDelete<SmCurve*> dc(&readCurves);
            SmObjsDelete<SmSurface*> ds(&readSurfaces);
            SmObjsDelete<SmBrep*> db(&readBreps);
            Require(
                SmNativeArchive::ReadPartFromFile(readContext, path.c_str(), readCurves, readSurfaces, readTrees, readBreps, format) == SM_SUCCESS &&
                    readCurves.GetSize() == 1 && readSurfaces.GetSize() == 0 && readBreps.GetSize() == 0,
                "source master archive inventory"
            );
            Verify(*readCurves[0], revolution);
        }
        Lifecycle(argv[1], argv[2], revolution);
        std::cout << "source master composition/parameters/ownership contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
