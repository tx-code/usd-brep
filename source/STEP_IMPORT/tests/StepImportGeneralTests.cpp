// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportCylinderChart.h"

#include <SmApiQueries.h>
#include <SmAssertArray.h>
#include <SmBSplineSurface.h>
#include <SmBrep.h>
#include <SmBrepData.h>
#include <SmContext.h>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
void Near(double actual, double expected, double tolerance, const char* message)
{
    Require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
double Dot(step_import::Point a, step_import::Point b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
void GaugeContract()
{
    using namespace step_import::detail;
    const CylinderGauge gauge{ { 2, -3, 5 }, { .8, -.6, 0 }, { 0, 0, -1 }, { .6, .8, 0 }, 3.7, .23, .4, -1.2, { .16, .18, .2, .2, .45, .45 } };
    const auto net = MakeCylinderGaugeNet(gauge);
    Require(net.pointsMm.size() == 32 && net.weights.size() == 32, "gauge control shape");
    SmContext context;
    SmTArray<SmPoint3d> cp;
    SmTArray<double> weights, uk, vk;
    SmTArray<ULONG> um, vm;
    for (const auto& p : net.pointsMm)
    {
        cp.Add(SmPoint3d(p[0], p[1], p[2]));
    }
    for (const auto w : net.weights)
    {
        Require(w > 0, "positive rational basis");
        weights.Add(w);
    }
    for (unsigned k = 0; k < 6; ++k)
    {
        uk.Add(double(k) / 4);
        um.Add(k == 0 || k == 5 ? 4 : 3);
    }
    vk.Add(0);
    vk.Add(1);
    vm.Add(2);
    vm.Add(2);
    SmBSplineSurface* raw = nullptr;
    const auto
        status = SmBSplineSurface::CreateCanonical(context, 3, 1, cp, SM_SF_UNSPECIFIED, um, vm, uk, vk, SM_KT_UNSPECIFIED, &weights, nullptr, raw);
    std::unique_ptr<SmBSplineSurface> surface(raw);
    Require(status == SM_SUCCESS && bool(surface), "native gauge construction");
    // Independent physical equations, including a rotated frame and a wider
    // terminal quarter. This detects weight/layout errors and a wrong u*W term.
    for (unsigned quarter = 0; quarter < 5; ++quarter)
    {
        for (unsigned k = 0; k <= 16; ++k)
        {
            for (unsigned j = 0; j <= 4; ++j)
            {
                const double t = double(k) / 16, u = (quarter + t) / 4, v = double(j) / 4;
                SmPoint3d point;
                Require(surface->EvaluatePoint(SmPoint2d(u, v), point) == SM_SUCCESS, "native gauge evaluation");
                const step_import::Point delta{ point.x - gauge.originMm[0], point.y - gauge.originMm[1], point.z - gauge.originMm[2] };
                const auto x = Dot(delta, gauge.x), y = Dot(delta, gauge.y), z = Dot(delta, gauge.z);
                Near(std::hypot(x, y), gauge.radiusMm, 2e-12, "exact cylinder radius");
                const auto width = (1 - t) * gauge.widthsMm[quarter] + t * gauge.widthsMm[quarter + 1];
                Near(z, gauge.lowerMm + gauge.pitchMm * u + width * v, 2e-12, "independent axial gauge equation");
            }
        }
    }
}
struct ReloadedNative
{
    std::unique_ptr<SmContext> context;
    std::unique_ptr<SmBrep> brep;
};
ReloadedNative ReloadNative(const std::filesystem::path& archive)
{
    ReloadedNative result;
    result.context = std::make_unique<SmContext>();
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    Require(
        SmBrepData::ReadPartFromFile(*result.context, archive.c_str(), curves, surfaces, trees, breps, SM_BINARY) == SM_SUCCESS,
        "native archive reload"
    );
    Require(breps.GetSize() == 1 && curves.GetSize() == 0 && surfaces.GetSize() == 0, "archive root inventory");
    result.brep.reset(breps[0]);
    SmAssertArray pointers, deep;
    Require(result.brep->ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "reloaded pointers");
    Require(result.brep->AssertValid(&deep, SM_LEVEL_2, SM_WALK) && deep.GetSize() == 0, "reloaded level2");
    return result;
}
void LifecycleContract(const std::filesystem::path& source, const std::filesystem::path& directory)
{
    auto options = step_import::Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = step_import::Quality::GeneralDevelopment();
    auto outcome = step_import::ImportFile(source, options);
    Require(outcome.status == step_import::Status::Accepted && bool(outcome.model), "owning general import");
    auto model = outcome.model;
    Near(*model->MaterialVolumeMm3(), 6000, 1e-5, "independent box volume");
    const auto closest = model->Closest({ 20, 7, 20 });
    Near(closest.distanceMm, 8, 1e-6, "independent boundary query");
    Near(closest.pointMm[0], 12, 1e-6, "independent boundary coordinate");
    Require(model->ClassifyMaterial({ 7, 7, 20 }) == step_import::MaterialPointClass::Inside, "independent interior query");
    auto copy = model->CopyForModeling();
    Require(copy.status == step_import::Status::Accepted && bool(copy.model), "independent editable copy");
    copy.model->Edit(
        [](SmBrep& brep)
        {
            double volume = 0;
            Require(SmApiBrepComputeVolume(&brep, 1e-4, volume) == SM_SUCCESS, "copy volume query");
            Near(volume, 6000, 1e-5, "copy geometry");
        }
    );
    std::filesystem::create_directories(directory);
    const auto archive = directory / "general-box.smb";
    model->WriteNative(archive);
    auto reloaded = ReloadNative(archive);
    double volume = 0;
    Require(SmApiBrepComputeVolume(reloaded.brep.get(), 1e-4, volume) == SM_SUCCESS, "reloaded volume query");
    Near(volume, 6000, 1e-5, "archive geometry preserved");
    for (const auto limit : { 3U, 30U, 80U })
    {
        auto limited = options;
        limited.nativeLimits.nativeCalls = limit;
        const auto failed = step_import::ImportFile(source, limited);
        Require(failed.status == step_import::Status::ResourceLimit && !failed.model, "private candidate resource cleanup");
    }
    auto retained = outcome.model->Face();
    const auto identity = retained.SourceId();
    outcome = {};
    model.reset();
    Require(!identity.empty() && std::isfinite(retained.Closest({ 20, 7, 20 }).distanceMm), "owning face view lifetime");
}
void DevelopmentContract(const std::filesystem::path& source, const std::filesystem::path& directory, bool study)
{
    auto options = step_import::Options::PlanarFixture();
    options.generalBrep = true;
    options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
    options.quality = study ? step_import::Quality::GeneralToleranceStudy() : step_import::Quality::GeneralDevelopment();
    options.sourceLimits = { 67108864, 1000000, 2000000, 2000000, 128, 1048576, 16777216, 268435456, 1024 };
    options.semanticLimits.dependencyVisits = 10000000;
    options.semanticLimits.preservedDiagnostics = 16384;
    options.nativeLimits.nativeCalls = 100000;
    options.nativeLimits.reportBytes = 268435456;
    auto result = step_import::ImportFile(source, options);
    std::filesystem::create_directories(directory);
    std::ofstream report(directory / "import.json", std::ios::binary);
    report << result.report.json;
    report.close();
    Require(result.status == step_import::Status::Accepted && bool(result.model), "development native import");
    const auto info = result.model->Info();
    step_import::Point outside = info.boundsMm[0];
    const double span = std::hypot(info.boundsMm[1][0] - outside[0], info.boundsMm[1][1] - outside[1], info.boundsMm[1][2] - outside[2]);
    outside[0] -= span + 1;
    const auto closest = result.model->Closest(outside);
    const double distance = std::hypot(closest.pointMm[0] - outside[0], closest.pointMm[1] - outside[1], closest.pointMm[2] - outside[2]);
    Near(closest.distanceMm, distance, 1e-6, "development whole-boundary query distance");
    Require(result.model->ClassifyMaterial(outside) == step_import::MaterialPointClass::Outside, "development exterior classification");
    const auto view = result.model->Face();
    const auto domain = view.ParameterDomain();
    const auto evaluation = view.EvaluateSurface((domain[0] + domain[2]) / 2, (domain[1] + domain[3]) / 2);
    for (const auto value : evaluation)
    {
        Require(std::isfinite(value), "development surface evaluation");
    }
    auto editable = result.model->CopyForModeling();
    Require(editable.status == step_import::Status::Accepted && bool(editable.model), "development independent modeling copy");
    double copiedVolume = 0;
    editable.model->Edit(
        [&](SmBrep& brep)
        {
            Require(SmApiBrepComputeVolume(&brep, 1e-4, copiedVolume) == SM_SUCCESS, "development copy volume");
        }
    );
    const auto expected = *result.model->MaterialVolumeMm3();
    Near(copiedVolume, expected, std::max(1e-6, expected * 1e-9), "copy preserves development geometry");
    const auto archive = directory / "model.smb";
    result.model->WriteNative(archive);
    auto loaded = ReloadNative(archive);
    SmTArray<SmFace*> faces;
    SmTArray<SmEdge*> edges;
    SmTArray<SmVertex*> vertices;
    loaded.brep->GetFaces(faces);
    loaded.brep->GetEdges(edges);
    loaded.brep->GetVertices(vertices);
    Require(faces.GetSize() == info.faces && edges.GetSize() == info.edges && vertices.GetSize() == info.vertices, "archive topology preserved");
    double volume = 0;
    Require(SmApiBrepComputeVolume(loaded.brep.get(), 1e-4, volume) == SM_SUCCESS, "development reloaded volume");
    Near(volume, expected, std::max(1e-6, expected * 1e-9), "archive preserves development geometry");
    SmPoint3d point;
    double queryDistance = 0;
    Require(
        SmApiBrepClosestPoint(loaded.brep.get(), SmPoint3d(outside[0], outside[1], outside[2]), point, queryDistance) == SM_SUCCESS,
        "reloaded boundary query"
    );
    Near(queryDistance, distance, 1e-5, "archive boundary distance preserved");
    std::ofstream checks(directory / "checks.json", std::ios::binary);
    checks << std::setprecision(17) << "{\"status\":\"Passed\",\"source_sha256\":\"" << result.report.sourceSha256 << "\",\"quality_id\":\""
           << options.quality.id << "\",\"faces\":" << info.faces << ",\"edges\":" << info.edges << ",\"vertices\":" << info.vertices
           << ",\"volume_mm3\":" << expected << ",\"copied_volume_mm3\":" << copiedVolume << ",\"reloaded_volume_mm3\":" << volume
           << ",\"outside_distance_mm\":" << distance << ",\"reloaded_distance_mm\":" << queryDistance
           << ",\"native_pointer_reports\":0,\"native_level2_reports\":0,\"reloaded_pointer_reports\":0,\"reloaded_level2_reports\":0}\n";
    Require(bool(checks), "development check receipt");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        if (argc == 5 && std::string_view(argv[1]) == "--development-file")
        {
            DevelopmentContract(argv[2], argv[3], std::string_view(argv[4]) == "study");
            return 0;
        }
        Require(argc == 3, "fixture and local output directory required");
        GaugeContract();
        LifecycleContract(argv[1], argv[2]);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
