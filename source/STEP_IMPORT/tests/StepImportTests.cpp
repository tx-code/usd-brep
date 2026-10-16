// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
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
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "Pass the frozen case directory");
        const auto path = std::filesystem::path(argv[1]) / "S203-P01/input.step";
        auto result = step_import::ImportFile(path, step_import::Options::PlanarFixture());
        if (result.status != step_import::Status::Accepted)
        {
            for (const auto& diagnostic : result.report.diagnostics)
            {
                std::cerr << diagnostic.source.key << ": " << diagnostic.source.message << '\n';
            }
        }
        Require(result.status == step_import::Status::Accepted && result.model, "F1 must produce an owning native model");
        const auto info = result.model->Info();
        Require(info.faces == 1 && info.edges == 4 && info.vertices == 4, "F1 topology");
        Require(std::abs(info.areaMm2 - 96) <= 1e-6 && std::abs(info.boundaryMm - 40) <= 1e-7, "Independent F1 measures");
        Require(!result.model->MaterialVolumeMm3(), "An open surface has no material volume qualification");
        Require(!result.model->ClassifyMaterial({ 4.2, 9.6, 5 }), "An open surface has no InsideSolid qualification");
        Require(result.report.pointerReports == 0 && result.report.deepReports == 0, "Both native validators actually executed");
        Require(result.report.definitionBoundMm && *result.report.definitionBoundMm <= 1e-8, "Native defining geometry certificate");
        Require(
            result.report.consistencyBoundMm && *result.report.consistencyBoundMm <= 1e-7,
            "Actual native vertex/edge/plane consistency certificate"
        );
        Require(result.report.uvBoundMm && *result.report.uvBoundMm <= 1e-7, "Whole-interval derived UV certificate");
        Require(result.report.tolerances.size() == 50, "Five stages observe Brep, face, four vertices and four edges");
        Require(result.report.edges.size() == 4, "Source curve parameter mappings retained");
        const auto originalSource = result.model->Source();
        const auto sourceBytes = originalSource->Source().Bytes();
        const auto reportCopy = result.report;
        const std::weak_ptr<const step_import::ModelSnapshot> root = result.model;
        std::optional face = result.model->Face();
        Require(face->SourceId() == "302", "Face identity maps to its original source record");
        result = {};
        Require(root.expired(), "Root ownership released while the view remains alive");
        const auto closest = face->Closest({ 4.2, 9.6, 8 });
        Require(
            std::abs(closest.distanceMm - 3) <= 1e-7 && std::abs(closest.pointMm[0] - 4.2) <= 1e-7 && std::abs(closest.pointMm[1] - 9.6) <= 1e-7 &&
                std::abs(closest.pointMm[2] - 5) <= 1e-7,
            "An owning face view survives the result and performs a real trimmed-face query"
        );
        Require(
            face->ClassifyUV(6, 4) == step_import::PointClass::Interior && face->ClassifyUV(20, 4) == step_import::PointClass::Outside,
            "Native trimmed-region classification"
        );
        const auto origin = face->EvaluateSurface(0, 0);
        Require(
            std::abs(origin[0] - 3) <= 1e-7 && std::abs(origin[1] + 2) <= 1e-7 && std::abs(origin[2] - 5) <= 1e-7,
            "Native UV zero preserves the source parameter origin"
        );
        bool domainRejected = false;
        try
        {
            (void)face->EvaluateSurface(1e6, 1e6);
        }
        catch (const std::out_of_range&)
        {
            domainRejected = true;
        }
        Require(domainRejected, "An out-of-window query must fail before native clamping");
        Require(!reportCopy.json.empty() && reportCopy.source->Bytes() == sourceBytes, "Report/source survive independently");

        for (std::size_t limit = 0; limit < reportCopy.nativeCalls; ++limit)
        {
            auto options = step_import::Options::PlanarFixture();
            options.nativeLimits.nativeCalls = limit;
            const auto failed = step_import::ImportBytes(sourceBytes, options);
            Require(failed.status == step_import::Status::ResourceLimit && !failed.model, "Every native call boundary blocks publication");
            Require(
                failed.report.nativeCalls == limit && failed.report.diagnostics.back().source.key == "NativeCallLimit",
                "Exact native call budget and diagnostic"
            );
        }
        auto tiny = step_import::Options::PlanarFixture();
        tiny.nativeLimits.reportBytes = 1;
        const auto unpublished = step_import::ImportBytes(sourceBytes, tiny);
        Require(
            unpublished.status == step_import::Status::ResourceLimit && !unpublished.model && unpublished.report.json.empty() &&
                unpublished.report.candidateDiscarded && unpublished.report.native,
            "Mandatory report overflow discards a fully constructed and checked candidate"
        );
        Require(
            originalSource->Source().Bytes() == sourceBytes && face->Closest({ 4.2, 9.6, 8 }).distanceMm == closest.distanceMm,
            "Failed later imports leave prior source and model unchanged"
        );
        face.reset();
        Require(reportCopy.source->Bytes() == sourceBytes && !reportCopy.json.empty(), "Report remains usable after every native owner is released");
        auto strict = step_import::Options::PlanarFixture();
        strict.quality.id = "explicit-unattainable-definition";
        strict.quality.definitionMm = step_import::Fraction(1, 1000000000000000LL);
        const auto uncertain = step_import::ImportBytes(sourceBytes, strict);
        Require(
            uncertain.status == step_import::Status::Indeterminate && !uncertain.model && uncertain.report.candidateDiscarded &&
                uncertain.report.diagnostics.back().source.key == "NativeDefinitionErrorBudget",
            "Unestablished error budget destroys candidate and cannot be called a proved source defect"
        );
        bool qualityRejected = false;
        try
        {
            strict.quality.id = "Q-P0-mm-01";
            (void)step_import::ImportBytes(sourceBytes, strict);
        }
        catch (const std::invalid_argument&)
        {
            qualityRejected = true;
        }
        Require(qualityRejected, "Frozen quality identity cannot describe changed parameters");
        const auto malformed = step_import::ImportBytes("incomplete STEP", step_import::Options::PlanarFixture());
        Require(
            malformed.status == step_import::Status::Rejected && !malformed.model && malformed.report.nativeCalls == 0 &&
                !malformed.report.candidateDiscarded && !malformed.report.diagnostics.empty(),
            "Source failure never starts a candidate"
        );
        std::cout << "Passed F1 geometry, parameters, queries, ownership, native-boundary and report-publication contracts\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
