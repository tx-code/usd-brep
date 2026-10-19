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
void Near(double value, double exact, double budget, const char* message)
{
    if (!std::isfinite(value) || std::abs(value - exact) > budget)
    {
        std::cerr << message << ": actual=" << value << ", exact=" << exact << '\n';
        throw std::runtime_error(message);
    }
}
step_import::Options Options()
{
    auto options = step_import::Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = step_import::Quality::GeneralDevelopment();
    options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
    return options;
}
void Geometry(const std::filesystem::path& path, const step_import::Outcome& result)
{
    if (result.status != step_import::Status::Accepted)
    {
        std::cerr << step_import::StatusName(result.status);
        if (!result.report.diagnostics.empty())
        {
            std::cerr << ": " << result.report.diagnostics.front().source.key;
        }
        std::cerr << '\n';
    }
    Require(result.status == step_import::Status::Accepted && result.model, "Accepted annular solid");
    std::ifstream input(path, std::ios::binary);
    const std::string bytes{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    Require(result.report.source && result.report.source->Bytes() == bytes, "Lossless source bytes retained");
    Require(result.report.native && result.report.pointerReports == 0 && result.report.deepReports == 0, "Complete native checks");
    Require(!result.report.json.empty() && !result.report.candidateDiscarded, "Complete report published with the model");
    Require(
        result.report.json.find("\"native_requested_relative_accuracy\":0.0001") != std::string::npos,
        "Requested integration accuracy remains 1e-4"
    );
    Require(
        result.report.json.find("\"absolute_area_volume_error_bound\":\"NotEstablished\"") != std::string::npos,
        "Requested precision is not relabeled as a certified bound"
    );
    constexpr double pi = std::numbers::pi;
    const auto info = result.model->Info();
    Require(info.faces == 4 && result.model->Faces().size() == 4 && info.materialVolumeMm3, "Complete annular model");
    Near(info.areaMm2, 944 * pi, 944 * pi * 1e-4, "Annular solid area");
    Near(*info.materialVolumeMm3, 413 * pi, 413 * pi * 1e-4, "Annular solid volume");
    Near(info.centroidMm[0], 0, .006, "Solid centroid X");
    Near(info.centroidMm[1], 0, .006, "Solid centroid Y");
    Near(info.centroidMm[2], 3.5, .006, "Solid centroid Z");
    std::size_t caps = 0;
    for (const auto& face : result.model->Faces())
    {
        const auto f = face.Info();
        const auto id = face.SourceId();
        const bool cap = id == "180" || id == "181";
        const double exact = cap ? 59 * pi : id == "182" ? 420 * pi : 406 * pi;
        Require(cap || id == "182" || id == "183", "Source face identity");
        Near(f.areaMm2, exact, exact * 1e-4, "Independent per-face area");
        Near(f.centroidMm[0], 0, .006, "Face centroid X");
        Near(f.centroidMm[1], 0, .006, "Face centroid Y");
        Near(f.centroidMm[2], cap ? (id == "180" ? 0 : 7) : 3.5, .006, "Face centroid Z");
        const auto domain = face.ParameterDomain();
        Require(
            std::isfinite(domain[0]) && std::isfinite(domain[1]) && std::isfinite(domain[2]) && std::isfinite(domain[3]),
            "Actual native parameter domain retained"
        );
        caps += cap;
    }
    Require(caps == 2, "Both independent annular cap observations retained");
}
void Interruptions(const std::filesystem::path& path, std::size_t nativeCalls)
{
    Require(nativeCalls > 16 && nativeCalls < 512, "Bounded synthetic charge inventory");
    // Check every charged boundary, including all fused face queries. The
    // public completed-call counter is used without depending on private stages.
    for (std::size_t call = 0; call < nativeCalls; ++call)
    {
        auto options = Options();
        options.execution.control = std::make_shared<step_import::ImportControl>();
        options.execution.progress = [&](step_import::Progress progress)
        {
            if (progress.phase == step_import::Phase::BeforeNativeCall && progress.nativeCalls == call)
            {
                options.execution.control->RequestCancel();
            }
        };
        const auto result = step_import::ImportFile(path, options);
        Require(
            result.status == step_import::Status::Cancelled && !result.model && (call == 0 || result.report.candidateDiscarded) &&
                !result.report.json.empty(),
            "Cancellation discards the unpublished candidate and retains the required report"
        );
    }
    auto options = Options();
    options.execution.control = std::make_shared<step_import::ImportControl>();
    options.execution.progress = [&](step_import::Progress progress)
    {
        if (progress.phase == step_import::Phase::NativeReady)
        {
            options.execution.control->RequestCancel();
        }
    };
    const auto late = step_import::ImportFile(path, options);
    Require(
        late.status == step_import::Status::Cancelled && !late.model && late.report.candidateDiscarded && !late.report.json.empty(),
        "Final execution decision still precedes publication"
    );
    options = Options();
    options.nativeLimits.nativeCalls = nativeCalls - 1;
    const auto limited = step_import::ImportFile(path, options);
    Require(
        limited.status == step_import::Status::ResourceLimit && !limited.model && limited.report.candidateDiscarded && !limited.report.json.empty(),
        "Native limit prevents partial publication"
    );
    options = Options();
    options.nativeLimits.reportBytes = 1;
    const auto reportLimited = step_import::ImportFile(path, options);
    Require(
        reportLimited.status == step_import::Status::ResourceLimit && !reportLimited.model && reportLimited.report.candidateDiscarded &&
            reportLimited.report.json.empty(),
        "A model cannot publish without its complete report"
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "Expected synthetic annular solid fixture");
        const auto result = step_import::ImportFile(argv[1], Options());
        Geometry(argv[1], result);
        Interruptions(argv[1], result.report.nativeCalls);
        Geometry(argv[1], step_import::ImportFile(argv[1], Options()));
        std::cout << "STEP annular integral contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
