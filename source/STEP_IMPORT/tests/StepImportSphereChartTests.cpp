// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"
#include "StepImportSphereChart.h"

#include <SmAssertArray.h>
#include <SmCircle.h>
#include <SmNativeArchive.h>
#include <SmSphere.h>
#include <fstream>
#include <iostream>
#include <numbers>
using namespace step_import;
using step_import_test::Near;
using step_import_test::Require;
namespace
{
Options Settings()
{
    auto o = Options::PlanarFixture();
    o.generalBrep = true;
    o.quality = Quality::GeneralDevelopment();
    return o;
}
void Native(SmBrep& native)
{
    SmAssertArray pointers, deep;
    Require(native.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "sphere pointers");
    Require(native.AssertValid(&deep, SM_LEVEL_2, SM_WALK) && deep.GetSize() == 0, "sphere LEVEL2");
    step_import_test::CheckNativeWorkBudget(native, .001);
    SmTArray<SmFace*> faces;
    native.GetFaces(faces);
    Require(faces.GetSize() == 1, "one source face retained");
    const auto* sphere = dynamic_cast<const SmSphere*>(faces[0]->GetSurface());
    Require(sphere && sphere->GetRadius() == 5., "analytic sphere radius and class preserved");
    const auto origin = sphere->GetPosition().GetOriginRef();
    Require(origin.x == 0 && origin.y == 0 && origin.z == 0, "analytic center preserved");
    SmTArray<SmVertex*> vertices;
    native.GetVertices(vertices);
    Require(vertices.GetSize() == 1, "source vertex inventory preserved");
    const auto p = vertices[0]->GetPoint();
    Require(p.x == 4 && p.y == 3 && p.z == 0, "source vertex not moved");
}
void HelperContracts()
{
    using namespace step_import::detail;
    SmContext context;
    std::unique_ptr<SmCircle> curve(new (context) SmCircle({ 4, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, SmExtent1d(0, 360), 3., 3, &context));
    curve->SetOwner(nullptr);
    const auto range = curve->GetNaturalInterval();
    const SphereChartBoundary edge{ curve.get(), { range.GetMin(), range.GetMax() } };
    const SourceFrame frame{ { 0, 0, 0 }, { Point{ 1, 0, 0 }, Point{ 0, 1, 0 }, Point{ 0, 0, 1 } } };
    std::size_t work = 0;
    const auto checkpoint = []() -> Result<void>
    {
        return {};
    };
    const auto charge = [&](std::size_t n) -> Result<void>
    {
        work += n;
        return {};
    };
    const auto bounded = BoundSphereChartBoundary(std::span(&edge, 1), frame, 100, checkpoint, charge);
    Require(
        bounded && *bounded && (**bounded).negativeSideLowerMm[1] > 3.99,
        "complete rational circle control hull bounds negative target-X hemisphere"
    );
    Require((**bounded).negativeSideLowerMm[0] < 0, "opposite hemisphere is not falsely established");
    const auto target = SphereChartFrame(frame, 1);
    Require(
        target.axes[0] == Point{ -1, 0, 0 } && target.axes[1] == Point{ 0, -1, 0 } && target.axes[2] == Point{ 0, 0, 1 },
        "right-handed signed permutation"
    );
    const auto tiny = BoundSphereChartBoundary(std::span(&edge, 1), frame, 1, checkpoint, charge);
    Require(tiny && !*tiny, "insufficient proof capacity does not guess a chart");
    const auto stopped = BoundSphereChartBoundary(
        std::span(&edge, 1),
        frame,
        100,
        []() -> Result<void>
        {
            return tl::unexpected(Failure{ Status::Cancelled, "SphereCallerCancel", "caller", {}, "SphereCaller" });
        },
        charge
    );
    Require(!stopped && stopped.error().key == "SphereCallerCancel" && !stopped.error().record, "caller cancellation propagates unchanged");
    const auto chargedFailure = BoundSphereChartBoundary(
        std::span(&edge, 1),
        frame,
        100,
        checkpoint,
        [](std::size_t) -> Result<void>
        {
            return tl::unexpected(Failure{ Status::ResourceLimit, "SphereCallerWork", "caller", 17, "SphereCaller" });
        }
    );
    Require(
        !chargedFailure && chargedFailure.error().key == "SphereCallerWork" && chargedFailure.error().record == 17,
        "caller work quota propagates unchanged"
    );
    std::unique_ptr<SmCircle> equator(new (context) SmCircle({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, SmExtent1d(0, 360), 5., 3, &context));
    const auto full = equator->GetNaturalInterval();
    const SphereChartBoundary crossing{ equator.get(), { full.GetMin(), full.GetMax() } };
    const auto noChart = BoundSphereChartBoundary(std::span(&crossing, 1), frame, 100, checkpoint, charge);
    Require(
        noChart && *noChart &&
            std::none_of(
                (**noChart).negativeSideLowerMm.begin(),
                (**noChart).negativeSideLowerMm.end(),
                [](double v)
                {
                    return v > 0;
                }
            ),
        "boundary crossing all candidate hemispheres establishes no chart"
    );
    auto outside = edge;
    outside.interval = { range.GetMin() - 1, range.GetMax() };
    const auto refused = BoundSphereChartBoundary(std::span(&outside, 1), frame, 100, checkpoint, charge);
    Require(refused && !*refused, "a full-basis hull cannot certify extrapolated intervals");
    Require(work > 0, "proof accounting observes all controls");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public sphere fixture and output required");
        std::ifstream in(argv[1], std::ios::binary);
        Require(bool(in), "public sphere input");
        const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        auto result = ImportBytes(text, Settings());
        if (!result.model)
        {
            for (const auto& d : result.report.diagnostics)
            {
                std::cerr << d.source.key << '\n';
            }
        }
        Require(result.status == Status::Accepted && result.model, "complete seam-crossing source sphere cap");
        const auto model = result.model;
        Require(model->Source()->Source().Bytes() == text, "original source bytes and frame retained");
        Require(model->Info().shapeRole == "OpenSurfaceModel" && !model->MaterialVolumeMm3(), "cap keeps its open source role");
        // Independent exact sphere-cap geometry: A=2*pi*R*h, R5,h=5-4=1.
        // Derived trims have the unchanged1e-3 mm budget. Native region area
        // is a numerical observation of those trims, not an exact Green integral.
        // This independent area check uses1e-3 mm² (still below the boundary
        // displacement scale perimeter*UVbudget); no import budget is enlarged.
        Near(model->Info().areaMm2, 10 * std::numbers::pi, 1e-3, "independent analytic cap area within derived-trim measurement resolution");
        const auto nearest = model->Closest({ 6, 0, 0 });
        Near(nearest.distanceMm, 1., 1e-6, "independent cap closest distance");
        Near(nearest.pointMm[0], 5., 1e-6, "independent cap closest X");
        const auto source = model->Source();
        const auto* sourcePlacement = source->Source().Find("31");
        Require(sourcePlacement != nullptr, "original sphere placement still present");
        // The source X axis remains +X while the target atlas moves the cut.
        Require(text.find("#33=DIRECTION('',(1.,0.,0.))") != std::string::npos, "source angular frame bytes preserved");
        const auto charts = model->SphereCharts();
        Require(charts.size() == 1, "owning chart evidence");
        Require(
            charts[0].sourceGeometryId == "30" && charts[0].sourceUseId == "23" && charts[0].targetFaceIndex == 0,
            "source face/sphere correspondence"
        );
        const auto calls = result.report.nativeCalls, bytes = result.report.json.size(), exact = result.report.exactGeometryWork->usedUnits;
        auto copy = model->CopyBrepForModeling();
        Require(copy.model != nullptr, "independent sphere modeling copy");
        copy.model->Edit(Native);
        const auto directory = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(directory);
        const auto archive = directory / "sphere.smb";
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
            "sphere archive readback"
        );
        Native(*bodies[0]);
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
            Require(ImportBytes(text, o).status == Status::Accepted, "exact selected budget boundary");
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
            const auto failed = ImportBytes(text, o);
            Require(failed.status == Status::ResourceLimit && !failed.model, "N-1 publishes no partial atlas/model");
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
            Require(failed.status == Status::Cancelled && !failed.model, "cancel publishes no partial sphere");
        }
        auto negative = text;
        const auto radiusAt = negative.find("SPHERICAL_SURFACE('',#31,5.)");
        Require(radiusAt != std::string::npos, "negative source mutation");
        negative.replace(radiusAt, std::string("SPHERICAL_SURFACE('',#31,5.)").size(), "SPHERICAL_SURFACE('',#31,-5.)");
        const auto invalid = ImportBytes(negative, Settings());
        Require(
            !invalid.model && invalid.status == Status::Rejected && invalid.report.nativeCalls == 0,
            "chart selection cannot bypass the source positive-radius rule"
        );
        auto wrongRegion = text;
        const auto orientationAt = wrongRegion.find("#80=ORIENTED_EDGE('',*,*,#74,.T.)");
        Require(orientationAt != std::string::npos, "source loop orientation mutation");
        wrongRegion.replace(orientationAt, std::string("#80=ORIENTED_EDGE('',*,*,#74,.T.)").size(), "#80=ORIENTED_EDGE('',*,*,#74,.F.)");
        const auto opposite = ImportBytes(wrongRegion, Settings());
        Require(!opposite.model, "a hemisphere chart must not silently replace a complementary source region");
        result = {};
        Require(model->SphereCharts()[0].parameters == charts[0].parameters, "snapshot chart survives report/outcome release");
        HelperContracts();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
