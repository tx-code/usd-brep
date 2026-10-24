// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportDefinitionChecks.h"
#include "StepImportExecution.h"
#include "StepImportGeometryConstruction.h"
#include "StepImportRevolutionConstruction.h"

#include <SmBrepData.h>
#include <SmFace.h>
#include <SmNativeArchive.h>
#include <SmSurfOfRevolution.h>
#include <iostream>

using namespace step_import;
using namespace step_import::detail;
using namespace step_import::detail::construction;
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
void Verify(SmSurface& surface)
{
    auto* revolution = dynamic_cast<SmSurfOfRevolution*>(&surface);
    Require(revolution, "source revolution is not fitted or reclassified");
    SmBSplineCurve* generator = nullptr;
    SmPoint3d origin;
    SmVector3d axis;
    SmBoolean swapped = TRUE;
    Require(revolution->GetCanonical(generator, origin, axis, &swapped) == SM_SUCCESS && generator && !swapped, "retained canonical generator");
    Require(generator->GetSTEPInterval().GetMin() == -2 && generator->GetSTEPInterval().GetMax() == 8, "generator source parameter interval");
    SmTArray<double> knots;
    Require(
        generator->GetKnotsAll(knots) == SM_SUCCESS && knots.GetSize() == 4 && knots[0] == -2 && knots[1] == -2 && knots[2] == 8 && knots[3] == 8,
        "original repeated source knots"
    );
    SmTArray<SmPoint3d> points;
    SmTArray<double> weights;
    Require(generator->GetControlPolygon(points, weights) == SM_SUCCESS && points.GetSize() == 2 && weights.GetSize() == 2, "rational generator net");
    Require(weights[0] == 1 && weights[1] == 2, "source rational weights unchanged");
    Require(
        points[0].x == 0 && points[0].y == 3 && points[0].z == 0 && points[1].x == 0 && points[1].y == 3 && points[1].z == 7,
        "source generator coefficients in physical millimetres"
    );
    for (const double v : { -2., 0., 3., 8. })
    {
        const auto s = (v + 2) / 10;
        const auto z = 14 * s / (1 + s);
        for (const double degrees : { 0., 45., 90., 180., 270., 360. })
        {
            SmPoint3d actual;
            Require(revolution->EvaluateSTEPPoint(SmPoint2d(degrees, v), actual) == SM_SUCCESS, "native STEP evaluator");
            const auto radians = degrees * std::numbers::pi / 180;
            step_import_test::Near(actual.x, -3 * std::sin(radians), 1e-9, "independent revolution x");
            step_import_test::Near(actual.y, 3 * std::cos(radians), 1e-9, "independent revolution y");
            step_import_test::Near(actual.z, z, 1e-9, "generator parameter is not height or [0,1]");
        }
    }
}
void VerifyBrep(SmBrep& brep)
{
    SmTArray<SmFace*> faces;
    brep.GetFaces(faces);
    unsigned revolutions = 0;
    for (ULONG i = 0; i < faces.GetSize(); ++i)
    {
        if (dynamic_cast<SmSurfOfRevolution*>(faces[i]->GetSurface()))
        {
            Verify(*faces[i]->GetSurface());
            ++revolutions;
        }
    }
    Require(revolutions == 1, "complete BRep preserves its original revolution definition");
    step_import_test::CheckNativeWorkBudget(brep, .005);
}
void Lifecycle(const std::filesystem::path& fixture, const std::filesystem::path& directory)
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    options.numericalIntegration = NumericalIntegrationSettings{ 1e-8 };
    auto result = ImportFile(fixture, options);
    Require(result.status == Status::Accepted && result.model && !result.report.json.empty(), "complete revolution import and report");
    auto model = result.model;
    const auto original = model->Source();
    step_import_test::Near(*model->MaterialVolumeMm3(), 63 * std::numbers::pi, .001, "independent rational-cylinder volume");
    step_import_test::Near(model->Info().areaMm2, 60 * std::numbers::pi, .001, "independent rational-cylinder area");
    const auto closest = model->Closest({ 5, 0, 3 });
    step_import_test::Near(closest.distanceMm, 2, 1e-6, "independent revolution closest distance");
    Require(model->ClassifyMaterial({ 0, 0, 3 }) == MaterialPointClass::Inside, "independent revolution material query");
    auto editable = model->CopyForModeling();
    Require(editable.status == Status::Accepted && editable.model, "complete revolution modeling copy");
    result = {};
    editable.model->Edit(VerifyBrep);
    const auto archive = directory / "revolution-brep.smb";
    model->WriteNative(archive);
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    SmObjsDelete<SmCurve*> dc(&curves);
    SmObjsDelete<SmSurface*> ds(&surfaces);
    SmObjsDelete<SmBrep*> db(&breps);
    Require(
        SmNativeArchive::ReadPartFromFile(context, archive.c_str(), curves, surfaces, trees, breps, SM_BINARY) == SM_SUCCESS &&
            breps.GetSize() == 1 && curves.GetSize() == 0 && surfaces.GetSize() == 0,
        "complete BRep archive inventory"
    );
    VerifyBrep(*breps[0]);
    Require(model->Source() == original, "snapshot source remains immutable after copies and archives");
    for (const auto phase : { Phase::BeforeNativeCall, Phase::NativeReady, Phase::BeforeCommit })
    {
        auto cancelled = options;
        cancelled.execution.control = std::make_shared<ImportControl>();
        cancelled.execution.progress = [&, phase](Progress progress)
        {
            if (progress.phase == phase)
            {
                cancelled.execution.control->RequestCancel();
            }
        };
        const auto stopped = ImportFile(fixture, cancelled);
        Require(stopped.status == Status::Cancelled && !stopped.model && !stopped.report.json.empty(), "cancelled revolution never publishes");
    }
    auto bounded = options;
    bounded.nativeLimits.nativeCalls = 2;
    const auto failed = ImportFile(fixture, bounded);
    Require(failed.status == Status::ResourceLimit && !failed.model && !failed.report.json.empty(), "resource-limited revolution never publishes");
    bounded = options;
    bounded.nativeLimits.reportBytes = 1;
    const auto noReport = ImportFile(fixture, bounded);
    Require(noReport.status == Status::ResourceLimit && !noReport.model, "missing required report prevents revolution publication");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public rational-revolution STEP fixture and archive directory required");
        const auto raw = step_source::InspectFile(argv[1], step_source::Limits::PlanarFixtures());
        const auto checked = step_semantics::Check(raw, {}, step_semantics::SourcePolicy::Strict, step_semantics::GeometryScope::GeneralBrep);
        Require(checked.snapshot && checked.status == step_semantics::Status::Checked, "qualified public source");
        const auto record = [&](const char* id)
        {
            return std::size_t(raw.source->Find(id) - raw.source->Records().data());
        };
        auto options = Options::PlanarFixture();
        options.generalBrep = true;
        options.quality = Quality::GeneralDevelopment();
        Report report;
        Execution execution(options, report);
        SmContext context;
        Inputs input(checked.snapshot, options, report, execution, context);
        auto built = MakeSourceRevolution(input, record("142"), record("182"));
        Require(bool(built), built ? "revolution factory" : built.error().key.c_str());
        Verify(**built);
        Require(report.parameterMappings.back().kind == "RevolutionSourceParameters", "explicit source parameter evidence");
        SmSurface* copied = nullptr;
        Require((*built)->Copy(context, copied) == SM_SUCCESS && copied, "independent native revolution copy");
        std::unique_ptr<SmSurface> copy(copied);
        Verify(*copy);
        built->reset();
        Verify(*copy); // Source generator lifetime is independent of the discarded original.
        std::filesystem::create_directories(argv[2]);
        for (const auto format : { SM_BINARY, SM_ASCII })
        {
            SmTArray<SmCurve*> curves;
            SmTArray<SmSurface*> surfaces;
            SmTArray<long> trees;
            SmTArray<SmBrep*> breps;
            surfaces.Add(copy.get()); // Borrowed inventory; copy remains its sole owner.
            const auto path = std::filesystem::path(argv[2]) / (format == SM_BINARY ? "revolution.smb" : "revolution.smt");
            Require(
                SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, breps, format, TRUE, FALSE) == SM_SUCCESS,
                "revolution archive write"
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
                    readSurfaces.GetSize() == 1 && readCurves.GetSize() == 0 && readBreps.GetSize() == 0,
                "complete revolution archive inventory"
            );
            Verify(*readSurfaces[0]);
        }

        auto limited = options;
        limited.nativeLimits.nativeCalls = 0;
        Report limitedReport;
        Execution limitedExecution(limited, limitedReport);
        Inputs limitedInput(checked.snapshot, limited, limitedReport, limitedExecution, context);
        auto refused = MakeSourceRevolution(limitedInput, record("142"), record("182"));
        Require(!refused && refused.error().status == Status::ResourceLimit, "native-call budget remains authoritative");
        Lifecycle(argv[1], argv[2]);
        std::cout << "source revolution definition/parameter/ownership contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
