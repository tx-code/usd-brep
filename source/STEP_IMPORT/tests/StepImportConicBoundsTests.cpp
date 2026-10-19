// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>
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
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
    {
        std::cerr << message << ": actual=" << actual << ", expected=" << expected << ", tolerance=" << tolerance << '\n';
    }
    Require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
step_import::Options Options()
{
    auto options = step_import::Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = step_import::Quality::GeneralDevelopment();
    options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
    return options;
}
step_import::Outcome Accepted(const std::filesystem::path& path)
{
    auto result = step_import::ImportFile(path, Options());
    Require(result.status == step_import::Status::Accepted && result.model, "accepted synthetic solid");
    std::ifstream input(path, std::ios::binary);
    const std::string bytes{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    Require(result.report.source && result.report.source->Bytes() == bytes, "immutable source retained");
    Require(
        !result.report.json.empty() && result.report.native && result.report.pointerReports == 0 && result.report.deepReports == 0,
        "complete successful report and native validation"
    );
    const auto info = result.model->Info();
    Require(info.materialVolumeMm3 && result.model->Faces().size() == info.faces, "complete model observations");
    for (const auto& face : result.model->Faces())
    {
        const auto observation = face.Info();
        Require(!face.SourceId().empty() && std::isfinite(observation.areaMm2) && observation.areaMm2 > 0, "owning per-face observation");
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            Require(
                std::isfinite(observation.centroidMm[axis]) && std::isfinite(observation.boundsMm[0][axis]) &&
                    std::isfinite(observation.boundsMm[1][axis]) && observation.boundsMm[0][axis] <= observation.boundsMm[1][axis],
                "finite ordered face bounds"
            );
        }
    }
    return result;
}
void Cylinder(const std::filesystem::path& path)
{
    const auto result = Accepted(path);
    const auto info = result.model->Info();
    constexpr double pi = std::numbers::pi;
    Require(info.faces == 3 && info.edges == 3 && info.vertices == 2, "cylinder topology");
    // Match the import contract's unchanged requested relative integration
    // accuracy. Coordinate bounds below retain their absolute 1e-6 mm check.
    Near(info.areaMm2, 60 * pi, 60 * pi * 1e-4, "independent cylinder area");
    Near(*info.materialVolumeMm3, 63 * pi, 63 * pi * 1e-4, "independent cylinder volume");
    Near(info.boundaryMm, 12 * pi + 7, 1e-6, "unique circle and seam lengths");
    const step_import::Bounds expected{ { { -3, -3, 0 }, { 3, 3, 7 } } };
    for (std::size_t side = 0; side < 2; ++side)
    {
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            Near(info.boundsMm[side][axis], expected[side][axis], 1e-6, "independent cylinder extrema");
        }
    }

    // Exercise late interruption on a conic solid, after bounds queries have
    // completed. A faster kernel query cannot publish a candidate prematurely.
    auto cancelled = Options();
    cancelled.execution.control = std::make_shared<step_import::ImportControl>();
    cancelled.execution.progress = [&](step_import::Progress progress)
    {
        if (progress.phase == step_import::Phase::NativeReady)
        {
            cancelled.execution.control->RequestCancel();
        }
    };
    const auto stopped = step_import::ImportFile(path, cancelled);
    Require(
        stopped.status == step_import::Status::Cancelled && !stopped.model && stopped.report.candidateDiscarded && !stopped.report.json.empty(),
        "late cancellation discards candidate and retains report"
    );
    auto limited = Options();
    limited.nativeLimits.nativeCalls = result.report.nativeCalls - 1;
    const auto exhausted = step_import::ImportFile(path, limited);
    Require(
        exhausted.status == step_import::Status::ResourceLimit && !exhausted.model && exhausted.report.candidateDiscarded &&
            !exhausted.report.json.empty(),
        "late native limit cannot publish partial model"
    );
    limited = Options();
    limited.nativeLimits.reportBytes = 1;
    const auto incomplete = step_import::ImportFile(path, limited);
    Require(
        incomplete.status == step_import::Status::ResourceLimit && !incomplete.model && incomplete.report.candidateDiscarded &&
            incomplete.report.json.empty(),
        "model requires complete bounded report"
    );
}
void SurfaceInterior(const std::filesystem::path& path)
{
    const auto result = Accepted(path);
    const auto info = result.model->Info();
    // Top patch: z=35+80*u*(1-u)*v*(1-v). All boundary edges have
    // z<=35; its interior maximum is 40 at (u,v)=(1/2,1/2).
    Near(info.boundsMm[0][2], 5, 1e-6, "bulged box minimum");
    Near(info.boundsMm[1][2], 40, 1e-6, "surface interior maximum exceeds every edge");
    Near(*info.materialVolumeMm3, 6000 + 4000. / 9, .01, "independent bulged box volume");
    bool found = false;
    for (const auto& face : result.model->Faces())
    {
        found = found || face.Info().boundsMm[1][2] > 39.999999;
    }
    Require(found, "per-face bounds retain surface interior extremum");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "expected cylinder and bulged box fixtures");
        Cylinder(argv[1]);
        SurfaceInterior(argv[2]);
        std::cout << "STEP conic bounds contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
