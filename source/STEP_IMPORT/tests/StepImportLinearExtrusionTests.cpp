// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <SmAssertArray.h>
#include <SmBrep.h>
#include <SmContext.h>
#include <SmFace.h>
#include <SmNativeArchive.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>

namespace
{
using namespace step_import;
using step_import_test::Require;
void Replace(std::string& text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    Require(at != std::string::npos, "unique public fixture token");
    text.replace(at, from.size(), to);
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    Require(bool(in), "public extrusion fixture");
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}
Options Settings()
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    return options;
}
void Near(double actual, double expected, double tolerance, const char* message)
{
    Require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
void PointNear(const Point& actual, const Point& expected, double tolerance, const char* message)
{
    for (std::size_t k = 0; k < 3; ++k)
    {
        Near(actual[k], expected[k], tolerance, message);
    }
}
void Native(SmBrep& brep)
{
    SmAssertArray pointers, deep;
    Require(brep.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "extrusion native pointers");
    Require(brep.AssertValid(&deep, SM_LEVEL_2, SM_WALK) && deep.GetSize() == 0, "extrusion native LEVEL2");
    step_import_test::CheckNativeWorkBudget(brep, .001);
}
std::string WithPcurves(std::string text)
{
    for (int i = 0; i < 4; ++i)
    {
        const auto identity = std::to_string(70 + i);
        // Replace only the edge-geometry reference, never the defining curve.
        Replace(text, ",#" + identity + ",.T.)", ",#" + std::to_string(170 + i) + ",.T.)");
    }
    std::string extra;
    const std::array<std::pair<int, int>, 4> ends{ std::pair{ 210, 211 }, { 211, 212 }, { 213, 212 }, { 210, 213 } };
    for (int i = 0; i < 4; ++i)
    {
        extra += "#" + std::to_string(170 + i) + "=SURFACE_CURVE('',#" + std::to_string(70 + i) + ",(#" + std::to_string(180 + i) +
                 "),.PCURVE_S1.);\n";
        extra += "#" + std::to_string(180 + i) + "=PCURVE('',#30,#" + std::to_string(190 + i) + ");\n";
        extra += "#" + std::to_string(190 + i) + "=DEFINITIONAL_REPRESENTATION('',(#" + std::to_string(200 + i) + "),#299);\n";
        extra += "#" + std::to_string(200 + i) + "=B_SPLINE_CURVE_WITH_KNOTS('',1,(#" + std::to_string(ends[i].first) + ",#" +
                 std::to_string(ends[i].second) + "),.UNSPECIFIED.,.F.,.F.,(2,2)," + (i % 2 ? "(-3.,5.)" : "(2.,5.)") + ",.UNSPECIFIED.);\n";
    }
    extra +=
        "#210=CARTESIAN_POINT('',(2.,-3.));\n#211=CARTESIAN_POINT('',(5.,-3.));\n"
        "#212=CARTESIAN_POINT('',(5.,5.));\n#213=CARTESIAN_POINT('',(2.,5.));\n"
        "#299=(GEOMETRIC_REPRESENTATION_CONTEXT(2) PARAMETRIC_REPRESENTATION_CONTEXT() REPRESENTATION_CONTEXT('source UV','2D'));\n";
    text.insert(text.rfind("ENDSEC;"), extra);
    return text;
}
void Check(const std::string& text, double area, const Point& evaluation, double u, double v, const std::filesystem::path& archive, bool resources)
{
    auto result = ImportBytes(text, Settings());
    if (!result.model)
    {
        for (const auto& d : result.report.diagnostics)
        {
            std::cerr << d.source.key << '\n';
        }
    }
    Require(result.status == Status::Accepted && result.model, "complete explicit linear extrusion import");
    const auto model = result.model;
    const auto calls = result.report.nativeCalls;
    const auto exact = result.report.exactGeometryWork->usedUnits;
    const auto reportBytes = result.report.json.size();
    Require(model->Source()->Source().Bytes() == text, "source bytes retained");
    Require(model->Info().shapeRole == "OpenSurfaceModel" && !model->MaterialVolumeMm3(), "open extrusion retains its nonmaterial source role");
    Require(model->Info().faces == 1 && model->Info().edges == 4 && model->Info().vertices == 4, "source topology inventory");
    Near(model->Info().areaMm2, area, area * 2e-4, "independent extrusion area");
    const auto view = model->Face();
    PointNear(view.EvaluateSurface(u, v), evaluation, 1e-8, "original U/scalar V parameterization");
    Require(
        std::any_of(
            result.report.parameterMappings.begin(),
            result.report.parameterMappings.end(),
            [](const auto& m)
            {
                return m.kind == "ExactLinearExtrusionChart" && m.targetFaceIndex == 0;
            }
        ),
        "recorded native chart correspondence"
    );
    auto copy = model->CopyBrepForModeling();
    Require(copy.status == Status::Accepted && copy.model, "independent editable extrusion copy");
    copy.model->Edit(Native);
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
            bodies.GetSize() == 1 && !curves.GetSize() && !surfaces.GetSize(),
        "complete extrusion archive replay"
    );
    Native(*bodies[0]);
    copy.model->Edit(
        [&](SmBrep& original)
        {
            step_import_test::CompareNativeDefinitions(original, *bodies[0]);
        }
    );
    result = {};
    PointNear(view.EvaluateSurface(u, v), evaluation, 1e-8, "owning view survives report release");
    if (!resources)
    {
        return;
    }
    auto limits = Settings();
    limits.nativeLimits.nativeCalls = calls;
    Require(ImportBytes(text, limits).status == Status::Accepted, "exact native capacity");
    --limits.nativeLimits.nativeCalls;
    auto failure = ImportBytes(text, limits);
    Require(failure.status == Status::ResourceLimit && !failure.model, "native N-1 withholds candidate");
    limits = Settings();
    limits.nativeLimits.exactWorkUnits = exact;
    Require(ImportBytes(text, limits).status == Status::Accepted, "exact construction-work capacity");
    --limits.nativeLimits.exactWorkUnits;
    failure = ImportBytes(text, limits);
    Require(failure.status == Status::ResourceLimit && !failure.model, "construction-work N-1 withholds candidate");
    limits = Settings();
    limits.nativeLimits.reportBytes = reportBytes;
    Require(ImportBytes(text, limits).status == Status::Accepted, "exact report capacity");
    --limits.nativeLimits.reportBytes;
    failure = ImportBytes(text, limits);
    Require(failure.status == Status::ResourceLimit && !failure.model, "report N-1 withholds candidate");
    for (auto phase : { Phase::BeforeNativeCall, Phase::NativeReady, Phase::BeforeReport, Phase::BeforeCommit })
    {
        limits = Settings();
        limits.execution.control = std::make_shared<ImportControl>();
        limits.execution.progress = [&](Progress p)
        {
            if (p.phase == phase)
            {
                limits.execution.control->RequestCancel();
            }
        };
        failure = ImportBytes(text, limits);
        Require(failure.status == Status::Cancelled && !failure.model, "cancellation releases the whole unpublished extrusion");
    }
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public fixture directory and local output required");
        std::filesystem::create_directories(argv[2]);
        const auto dir = std::filesystem::path(argv[1]), out = std::filesystem::path(argv[2]);
        const auto text = Read(dir / "linear_extrusion_patch.step");
        Check(text, 160, { 5, 1.2, 1.6 }, 3.5, 1, out / "affine.smb", true);
        Check(WithPcurves(text), 160, { 5, 1.2, 1.6 }, 3.5, 1, out / "pcurve.smb", false);
        auto metres = text;
        Replace(metres, "SI_UNIT(.MILLI.,.METRE.)", "SI_UNIT($,.METRE.)");
        Check(WithPcurves(metres), 160000000, { 5000, 1200, 1600 }, 3.5, 1, out / "metres.smb", false);
        Check(Read(dir / "linear_extrusion_parabolic.step"), std::sqrt(5.) + std::asinh(2.) / 2, { .5, 0, .25 }, .5, 0, out / "parabolic.smb", false);
        Check(
            Read(dir / "linear_extrusion_rational.step"),
            6 * std::numbers::pi,
            { 3 / std::sqrt(2.), 3 / std::sqrt(2.), 2 },
            .5,
            1,
            out / "rational.smb",
            false
        );
        const auto material = ImportBytes(Read(dir / "linear_extrusion_box.step"), Settings());
        if (!material.model)
        {
            for (const auto& d : material.report.diagnostics)
            {
                std::cerr << d.source.key << '\n';
            }
        }
        Require(
            material.status == Status::Accepted && material.model && material.model->MaterialVolumeMm3(),
            "material extrusion box publishes completely"
        );
        Near(*material.model->MaterialVolumeMm3(), 6000, .001, "independent material box volume");
        Near(material.model->Info().areaMm2, 2200, .001, "independent material box area");
        Require(material.model->ClassifyMaterial({ 7, 7, 20 }) == MaterialPointClass::Inside, "extruded source face bounds actual material");
        PointNear(material.model->Face().EvaluateSurface(3.5, -.5), { 7, 7, 5 }, 1e-8, "material face retains source scalar parameters");
        auto materialCopy = material.model->CopyBrepForModeling();
        Require(materialCopy.status == Status::Accepted && materialCopy.model, "material modeling copy");
        materialCopy.model->Edit(Native);
        material.model->WriteNative(out / "material.smb");
        auto zero = text;
        Replace(zero, "#45=VECTOR('',#46,2.)", "#45=VECTOR('',#46,0.)");
        const auto refused = ImportBytes(zero, Settings());
        Require(refused.status == Status::Unsupported && !refused.model, "zero vector is a native scope refusal, not an invented source WHERE");
        auto negative = text;
        Replace(negative, "#45=VECTOR('',#46,2.)", "#45=VECTOR('',#46,-2.)");
        const auto rejected = ImportBytes(negative, Settings());
        Require(
            rejected.status == Status::Rejected && !rejected.model && rejected.report.nativeCalls == 0,
            "negative vector magnitude fails the real source VECTOR.WR1 before native work"
        );
        auto wrongDimension = text;
        Replace(wrongDimension, "#46=DIRECTION('',(0.,3.,4.))", "#46=DIRECTION('',(0.,3.))");
        const auto dimension = ImportBytes(wrongDimension, Settings());
        Require(
            dimension.status == Status::Rejected && !dimension.model && dimension.report.nativeCalls == 0,
            "extrusion dependencies retain geometric-context dimensional checks"
        );
        auto parallel = text;
        Replace(parallel, "#46=DIRECTION('',(0.,3.,4.))", "#46=DIRECTION('',(1.,0.,0.))");
        const auto degenerate = ImportBytes(parallel, Settings());
        Require(!degenerate.model, "parallel generator and extrusion vector cannot publish a rank-one face");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
