// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <cmath>
#include <iostream>
#include <set>
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
        auto result = step_import::ImportFile(std::filesystem::path(argv[1]) / "S203-P04/input.step", step_import::Options::PlanarFixture());
        if (!result.model || result.status != step_import::Status::Accepted)
        {
            throw std::runtime_error("P04 must deliver an owning material solid");
        }
        const auto info = result.model->Info();
        const auto volume = result.model->MaterialVolumeMm3();
        if (info.faces != 6 || info.edges != 12 || info.vertices != 8 || std::abs(info.areaMm2 - 2200) > 1e-6 || !volume ||
            std::abs(*volume - 6000) > 1e-5)
        {
            throw std::runtime_error("P04 independent shared topology, surface area and material volume");
        }
        if (!result.report.volumeBoundMm3 || *result.report.volumeBoundMm3 > 1e-5)
        {
            throw std::runtime_error("Actual target boundary needs an independent volume certificate");
        }
        Require(!info.normal && !info.windowMm && info.freeBoundaryEdges == 0, "Shape-wide normal/UV do not masquerade as a selected face");
        Require(
            result.report.solidRegions == 1 && result.report.voidCavities == 0 && result.report.tolerances.size() == 135,
            "Material census and all five tolerance stages"
        );
        Require(
            result.report.faces.size() == 6 && result.report.adjacency.size() == 12 && result.report.loops.size() == 6,
            "Per-face and shared-edge evidence"
        );
        std::set<std::string> occurrences;
        for (const auto& loop : result.report.loops)
        {
            for (const auto& use : loop.uses)
            {
                Require(occurrences.insert(use.orientedEdgeId).second, "Distinct source OE occurrences");
            }
        }
        Require(occurrences.size() == 24, "Twenty-four source uses are distinct from forty-eight native mate uses");
        for (const auto& adjacency : result.report.adjacency)
        {
            Require(
                adjacency.nativeUses == 4 && adjacency.faceIds.size() == 2 && adjacency.faceIds[0] != adjacency.faceIds[1],
                "True shared edge adjacency"
            );
        }
        const std::array<std::pair<step_import::Point, step_import::MaterialPointClass>, 5> classifications = {
            { { { 7, 7, 20 }, step_import::MaterialPointClass::Inside },
              { { 1, 7, 20 }, step_import::MaterialPointClass::Outside },
              { { 2, 7, 20 }, step_import::MaterialPointClass::Face },
              { { 2, -3, 20 }, step_import::MaterialPointClass::Edge },
              { { 2, -3, 5 }, step_import::MaterialPointClass::Vertex } }
        };
        for (const auto& [point, expected] : classifications)
        {
            Require(result.model->ClassifyMaterial(point) == expected, "Independent material/boundary classification");
        }
        const auto near = result.model->ClassifyMaterial({ 2.000001, 7, 20 });
        Require(
            near == step_import::MaterialPointClass::Inside || near == step_import::MaterialPointClass::Indeterminate,
            "Working tolerance must not manufacture an exact boundary classification beyond query Q"
        );
        const auto closest = result.model->Closest({ 15, 7, 20 });
        Require(
            std::abs(closest.distanceMm - 3) <= 1e-7 && std::abs(closest.pointMm[0] - 12) <= 1e-7 && std::abs(closest.pointMm[1] - 7) <= 1e-7 &&
                std::abs(closest.pointMm[2] - 20) <= 1e-7,
            "Whole-solid closest point is not first-face closest point"
        );
        const auto corner = result.model->Closest({ 1, -5, 1 });
        Require(
            std::abs(corner.distanceMm - std::sqrt(21.0)) <= 1e-7 && std::abs(corner.pointMm[0] - 2) <= 1e-7 &&
                std::abs(corner.pointMm[1] + 3) <= 1e-7 && std::abs(corner.pointMm[2] - 5) <= 1e-7,
            "Independent corner closest point"
        );
        auto faces = result.model->Faces();
        Require(faces.size() == 6, "All owning face views");
        for (const auto& face : faces)
        {
            const auto faceInfo = face.Info();
            Require(
                faceInfo.faces == 1 && faceInfo.edges == 4 && faceInfo.vertices == 4 && faceInfo.normal && faceInfo.windowMm &&
                    !faceInfo.materialVolumeMm3,
                "Each face has its own qualified geometry and UV domain"
            );
        }
        const auto bytes = result.model->Source()->Source().Bytes();
        for (std::size_t limit = 0; limit < result.report.nativeCalls; ++limit)
        {
            auto options = step_import::Options::PlanarFixture();
            options.nativeLimits.nativeCalls = limit;
            const auto failed = step_import::ImportBytes(bytes, options);
            Require(
                failed.status == step_import::Status::ResourceLimit && !failed.model && failed.report.nativeCalls == limit,
                "Every material-region/shared-edge/UV/query checkpoint blocks partial publication"
            );
        }
        auto tiny = step_import::Options::PlanarFixture();
        tiny.nativeLimits.reportBytes = 1;
        const auto unpublished = step_import::ImportBytes(bytes, tiny);
        Require(
            unpublished.status == step_import::Status::ResourceLimit && !unpublished.model && unpublished.report.native &&
                unpublished.report.candidateDiscarded && unpublished.report.json.empty(),
            "Mandatory report discards a complete material candidate"
        );
        const auto reportCopy = result.report;
        const std::weak_ptr<const step_import::ModelSnapshot> root = result.model;
        result = {};
        Require(root.expired(), "Owning face views do not retain the root wrapper");
        for (const auto& face : faces)
        {
            const auto onFace = face.Closest(face.Info().centroidMm);
            Require(onFace.distanceMm <= 1e-7, "Every face view retains the whole shared model");
        }
        faces.clear();
        Require(
            reportCopy.source->Bytes() == bytes && reportCopy.faces.size() == 6 && !reportCopy.json.empty(),
            "Material report survives all native ownership"
        );
        std::cout << "P04 shared material box, queries, ownership and failure boundaries passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
