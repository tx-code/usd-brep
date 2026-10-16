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
        auto result = step_import::ImportFile(std::filesystem::path(argv[1]) / "S203-P05/input.step", step_import::Options::PlanarFixture());
        if (result.status != step_import::Status::Accepted || !result.model)
        {
            throw std::runtime_error("P05 must deliver an owning closed surface model");
        }
        const auto info = result.model->Info();
        if (info.shapeRole != "ClosedSurfaceModel" || info.faces != 6 || info.edges != 12 || info.vertices != 8 ||
            std::abs(info.areaMm2 - 2200) > 1e-6 || info.freeBoundaryEdges != 0)
        {
            throw std::runtime_error("P05 retains the complete shared box boundary with its surface role");
        }
        if (info.materialVolumeMm3 || result.model->MaterialVolumeMm3() || result.model->ClassifyMaterial({ 7, 7, 20 }))
        {
            throw std::runtime_error("A closed surface has no material volume or InsideSolid qualification");
        }
        Require(result.model->Source()->PartDefinition().shapeRole == "ClosedSurfaceModel", "Source snapshot preserves surface role");
        Require(
            result.report.sourceShape && result.report.sourceShape->role == "ClosedSurfaceModel" &&
                result.report.sourceShape->representationType == "MANIFOLD_SURFACE_SHAPE_REPRESENTATION" &&
                result.report.sourceShape->shapeType == "SHELL_BASED_SURFACE_MODEL" && result.report.sourceShape->shellType == "CLOSED_SHELL",
            "Source chain remains queryable"
        );
        Require(
            result.report.nativeRole && result.report.nativeRole->regions == 2 && result.report.nativeRole->shells == 2 &&
                result.report.nativeRole->materialRegions == 0 && result.report.nativeRole->boundedVoidRegions == 1 &&
                result.report.nativeRole->manifoldSolidPredicate,
            "Native topology does not grant material qualification"
        );
        Require(
            !result.report.solidRegions && !result.report.voidCavities && !result.report.volumeBoundMm3 && !result.report.volumeJunctionBoundMm &&
                !result.report.volumeGapFluxBoundMm3 && !result.report.volumeSkippedFluxBoundMm3,
            "Surface census and material volume evidence remain not applicable"
        );
        Require(
            result.report.tolerances.size() == 135 && result.report.loops.size() == 6 && result.report.adjacency.size() == 12,
            "Complete closed boundary evidence"
        );
        for (const auto& row : result.report.adjacency)
        {
            Require(row.nativeUses == 4 && row.faceIds.size() == 2 && row.faceIds[0] != row.faceIds[1], "Actual shared surface edges");
        }
        for (const auto& point : std::array<step_import::Point, 5>{ { { 7, 7, 20 }, { 1, 7, 20 }, { 2, 7, 20 }, { 2, -3, 20 }, { 2, -3, 5 } } })
        {
            Require(!result.model->ClassifyMaterial(point), "Interior, exterior and boundary points have no material classification");
        }
        auto solid = step_import::ImportFile(std::filesystem::path(argv[1]) / "S203-P04/input.step", step_import::Options::PlanarFixture());
        Require(
            solid.model && solid.model->Info().shapeRole == "MaterialSolid" && solid.model->MaterialVolumeMm3() &&
                solid.model->ClassifyMaterial({ 7, 7, 20 }) == step_import::MaterialPointClass::Inside,
            "Concurrent solid retains its own material semantics"
        );
        const auto solidInfo = solid.model->Info();
        Require(
            std::abs(info.areaMm2 - solidInfo.areaMm2) <= 1e-6 && info.boundsMm == solidInfo.boundsMm && info.edges == solidInfo.edges &&
                info.vertices == solidInfo.vertices,
            "Same independent boundary, different role"
        );
        const auto closest = result.model->Closest({ 15, 7, 20 });
        Require(
            std::abs(closest.distanceMm - 3) <= 1e-7 && closest.pointMm == step_import::Point{ 12, 7, 20 },
            "Whole surface closest query remains available"
        );
        const auto source = result.model->Source();
        const auto bytes = source->Source().Bytes();
        const auto solidReport = solid.report;
        solid = {};
        Require(
            result.model->Closest({ 15, 7, 20 }).distanceMm == closest.distanceMm && solidReport.sourceShape->role == "MaterialSolid" &&
                result.report.sourceShape->role == "ClosedSurfaceModel",
            "Destroying solid cannot change surface or either owning report"
        );
        for (std::size_t limit = 0; limit < result.report.nativeCalls; ++limit)
        {
            auto options = step_import::Options::PlanarFixture();
            options.nativeLimits.nativeCalls = limit;
            const auto failed = step_import::ImportBytes(bytes, options);
            Require(
                failed.status == step_import::Status::ResourceLimit && !failed.model && failed.report.nativeCalls == limit,
                "Closed surface call budgets never publish partial topology"
            );
        }
        auto tiny = step_import::Options::PlanarFixture();
        tiny.nativeLimits.reportBytes = 1;
        const auto unpublished = step_import::ImportBytes(bytes, tiny);
        Require(
            unpublished.status == step_import::Status::ResourceLimit && !unpublished.model && unpublished.report.native &&
                unpublished.report.candidateDiscarded && unpublished.report.json.empty(),
            "Complete surface candidate report rejection"
        );
        auto views = result.model->Faces();
        const auto report = result.report;
        const std::weak_ptr<const step_import::ModelSnapshot> wrapper = result.model;
        result = {};
        Require(wrapper.expired() && views.size() == 6, "Views own native data independently of root wrapper");
        std::set<std::string> ids;
        for (const auto& face : views)
        {
            const auto faceInfo = face.Info();
            Require(
                ids.insert(face.SourceId()).second && faceInfo.faces == 1 && faceInfo.normal && faceInfo.windowMm && !faceInfo.materialVolumeMm3 &&
                    face.Closest(faceInfo.centroidMm).distanceMm <= 1e-7,
                "Every surface face view retains qualified local geometry"
            );
        }
        views.clear();
        Require(
            report.sourceShape->role == "ClosedSurfaceModel" && report.source->Bytes() == bytes && !report.json.empty() &&
                source->PartDefinition().shapeRole == "ClosedSurfaceModel",
            "Source and report survive all native surface ownership"
        );
        std::cout << "P05 closed surface retains boundary and nonmaterial role\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
