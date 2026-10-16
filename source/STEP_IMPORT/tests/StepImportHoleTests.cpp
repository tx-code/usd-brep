// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <cmath>
#include <iostream>
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
        if (argc != 2)
        {
            throw std::runtime_error("Pass frozen case directory");
        }
        for (const auto id : { "S203-P02", "S203-P03" })
        {
            const auto result = step_import::ImportFile(std::filesystem::path(argv[1]) / id / "input.step", step_import::Options::PlanarFixture());
            if (!result.model || result.status != step_import::Status::Accepted)
            {
                throw std::runtime_error("P02 must deliver an owning face with its hole");
            }
            const auto info = result.model->Info();
            if (info.faces != 1 || info.edges != 8 || info.vertices != 8 || std::abs(info.areaMm2 - 88) > 1e-6 ||
                std::abs(info.boundaryMm - 52) > 1e-7)
            {
                throw std::runtime_error("P02 independent topology/area/perimeter");
            }
            Require(result.report.loops.size() == 2 && result.report.tolerances.size() == 90, "Both source loops and all tolerance objects");
            auto face = result.model->Face();
            const bool shifted = std::string_view(id) == "S203-P03";
            const double du = shifted ? -6 : 0, dv = shifted ? -4 : 0;
            Require(face.ClassifyUV(10 + du, 6 + dv) == step_import::PointClass::Interior, "Material face region");
            Require(face.ClassifyUV(4 + du, 3 + dv) == step_import::PointClass::Outside, "Hole is outside face");
            Require(face.ClassifyUV(2 + du, 3 + dv) == step_import::PointClass::Edge, "Inner boundary remains an edge");
            Require(face.ClassifyUV(13 + du, 1 + dv) == step_import::PointClass::Outside, "Beyond outer boundary");
            const auto closest = face.Closest({ 2.8, 1.9, 8 });
            Require(
                std::abs(closest.distanceMm - std::sqrt(37.0 / 4)) <= 1e-7 && std::abs(closest.pointMm[0] - 3.2) <= 1e-7 &&
                    std::abs(closest.pointMm[1] - 1.6) <= 1e-7 && std::abs(closest.pointMm[2] - 5) <= 1e-7,
                "Independent closest point above hole"
            );
            const auto corner = face.EvaluateSurface(du, dv);
            Require(
                std::abs(corner[0] - 3) <= 1e-7 && std::abs(corner[1] + 2) <= 1e-7 && std::abs(corner[2] - 5) <= 1e-7,
                "Negative UV preserves source parameter origin without clamping"
            );
            Require(
                info.windowMm && std::abs((*info.windowMm)[0] - du) <= 1e-9 && std::abs((*info.windowMm)[1] - dv) <= 1e-9,
                "Finite window follows source UV"
            );
            bool rejected = false;
            try
            {
                (void)face.EvaluateSurface(du - 1, dv);
            }
            catch (const std::out_of_range&)
            {
                rejected = true;
            }
            Require(rejected, "Out-of-window evaluation rejected before native clamp");
            for (std::size_t limit = 0; limit < result.report.nativeCalls; ++limit)
            {
                auto options = step_import::Options::PlanarFixture();
                options.nativeLimits.nativeCalls = limit;
                const auto failed = step_import::ImportBytes(result.model->Source()->Source().Bytes(), options);
                Require(
                    failed.status == step_import::Status::ResourceLimit && !failed.model && failed.report.nativeCalls == limit,
                    "Every hole construction/UV checkpoint rejects partial publication"
                );
            }
            auto tiny = step_import::Options::PlanarFixture();
            tiny.nativeLimits.reportBytes = 1;
            const auto failed = step_import::ImportBytes(result.model->Source()->Source().Bytes(), tiny);
            Require(
                failed.status == step_import::Status::ResourceLimit && !failed.model && failed.report.candidateDiscarded && failed.report.native &&
                    failed.report.json.empty(),
                "Complete perforated candidate rejected when mandatory report cannot publish"
            );
        }
        std::cout << "P02/P03 holes, negative UV, independent queries, identity and failure boundaries passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
