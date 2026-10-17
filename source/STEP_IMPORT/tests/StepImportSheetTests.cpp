// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <SmApiGeneral.h>
#include <SmApiQueries.h>
#include <SmBrep.h>
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
void Near(double actual, double expected, const char* message)
{
    Require(std::isfinite(actual) && std::abs(actual - expected) <= 1e-7, message);
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2 || (argc == 3 && std::string_view(argv[2]) == "coplanar"), "Pass sheet fixture and optional coplanar mode");
        const bool coplanar = argc == 3;
        const auto options = step_import::Options::PlanarFixture();
        auto imported = step_import::ImportFile(argv[1], options);
        Require(imported.status == step_import::Status::Accepted && imported.model, "S1 yields an owning open sheet");
        const auto original = imported.model->Info();
        Require(original.faces == 2 && original.edges == 7 && original.vertices == 6 && original.freeBoundaryEdges == 6, "Independent S1 census");
        Near(original.areaMm2, 18, "Both face areas contribute");
        Near(original.boundaryMm, 21, "Shared edge counted once in unique edge length");
        Near(original.centroidMm[0], coplanar ? 3 : 8.0 / 3, "Area-weighted sheet centroid x");
        Near(original.centroidMm[1], 1.5, "Area-weighted sheet centroid y");
        Near(original.centroidMm[2], coplanar ? 0 : 1.0 / 3, "Area-weighted sheet centroid z");
        Require(!original.normal && !original.windowMm && !imported.model->MaterialVolumeMm3(), "No shape-wide plane or material volume");
        Require(!imported.model->ClassifyMaterial({ 2, 1, 0 }), "An open sheet never acquires material classification");
        Require(
            imported.report.nativeRole && imported.report.nativeRole->materialRegions == 0 && imported.report.nativeRole->boundedVoidRegions == 0 &&
                !imported.report.nativeRole->manifoldSolidPredicate,
            "Actual native spatial role remains an open surface"
        );
        Require(imported.report.tolerances.size() == 80, "Five stages cover BRep, both faces, seven edges and six vertices");
        for (const auto& tolerance : imported.report.tolerances)
        {
            Require(tolerance.valueMm == 1e-5, "Canonical working tolerance never grows");
        }
        std::size_t logicalUses = 0;
        for (const auto& loop : imported.report.loops)
        {
            logicalUses += loop.uses.size();
            for (const auto& use : loop.uses)
            {
                if (use.orientedEdgeId == "801")
                {
                    Require(
                        use.uvStartMm == std::array<double, 2>{ 4, 0 } && use.uvEndMm == std::array<double, 2>{ 4, 3 },
                        "FA shared side uses its own UV and direction"
                    );
                }
                if (use.orientedEdgeId == "813")
                {
                    Require(
                        use.uvStartMm == std::array<double, 2>{ 0, 3 } && use.uvEndMm == std::array<double, 2>{ 0, 0 },
                        "FB shared side uses its own reversed UV"
                    );
                }
            }
        }
        Require(logicalUses == 8, "Eight logical source occurrences, sixteen native mate uses");
        auto views = imported.model->Faces();
        Require(views.size() == 2, "Both independent owning face views");
        const auto bytes = imported.model->Source()->Source().Bytes();
        const auto stableReport = imported.report;
        const auto calls = imported.report.nativeCalls;
        for (std::size_t limit = 0; limit < calls; ++limit)
        {
            auto limited = options;
            limited.nativeLimits.nativeCalls = limit;
            const auto failed = step_import::ImportBytes(bytes, limited);
            Require(
                failed.status == step_import::Status::ResourceLimit && !failed.model && failed.report.nativeCalls == limit,
                "Every native-call budget boundary discards the incomplete sheet"
            );
            auto cancelledOptions = options;
            const auto control = std::make_shared<step_import::ImportControl>();
            cancelledOptions.execution.control = control;
            cancelledOptions.execution.progress = [control, limit](step_import::Progress progress)
            {
                if (progress.phase == step_import::Phase::BeforeNativeCall && progress.nativeCalls == limit)
                {
                    control->RequestCancel();
                }
            };
            const auto cancelled = step_import::ImportBytes(bytes, cancelledOptions);
            Require(
                cancelled.status == step_import::Status::Cancelled && !cancelled.model && cancelled.report.nativeCalls == limit,
                "Every native-call cancellation boundary discards the incomplete sheet"
            );
        }
        auto exact = options;
        exact.nativeLimits.reportBytes = stableReport.json.size();
        Require(step_import::ImportBytes(bytes, exact).model != nullptr, "Complete report N publishes");
        --exact.nativeLimits.reportBytes;
        const auto incomplete = step_import::ImportBytes(bytes, exact);
        Require(
            incomplete.status == step_import::Status::ResourceLimit && !incomplete.model && incomplete.report.json.empty() &&
                incomplete.report.candidateDiscarded,
            "Report N-1 never publishes a partial sheet"
        );
        auto copy = imported.model->CopyForModeling();
        Require(copy.status == step_import::Status::Accepted && copy.model, "Open shared sheet has an independent editable copy");
        copy.model->Edit(
            [coplanar](SmBrep& brep)
            {
                Require(SmApiTranslate(&brep, { 100, -20, 3 }) == SM_SUCCESS, "Translate independent open sheet copy");
                SmPoint3d minimum, maximum;
                Require(SmApiBrepBoundingBox(&brep, TRUE, minimum, maximum) == SM_SUCCESS, "Measure translated copy");
                Near(minimum.x, 100, "Copy minimum x");
                Near(maximum.z, coplanar ? 3 : 5, "Copy maximum z");
                long materials = 0, boundedVoids = 0;
                Require(
                    SmApiBrepMaterialCensus(&brep, materials, boundedVoids) == SM_SUCCESS && materials == 0 && boundedVoids == 0,
                    "Editable copy retains the surface role"
                );
            }
        );
        Require(
            imported.model->Info().boundsMm == original.boundsMm && imported.report.json == stableReport.json,
            "Editing a copy or failing another request cannot alter the accepted baseline"
        );
        const auto nearSecondFace = imported.model->Closest({ 5, 1.5, 1 });
        Near(nearSecondFace.distanceMm, 1, "Whole-sheet closest point reaches the second face");
        Near(nearSecondFace.pointMm[0], coplanar ? 5 : 4, "Second face closest x");
        Near(nearSecondFace.pointMm[2], coplanar ? 0 : 1, "Second face closest z");
        imported = {};
        for (const auto& view : views)
        {
            Near(view.Info().areaMm2, view.SourceId() == "704" ? 12 : 6, "Owning view survives original result release");
            const auto at = view.Info().centroidMm;
            Near(view.Closest(at).distanceMm, 0, "Released-result view remains queryable");
        }
        views.clear();
        Require(
            stableReport.declarations && stableReport.declarations->semantic->Source().Bytes() == bytes && !stableReport.json.empty(),
            "Report and source survive all original native owners"
        );
        copy.model->Edit(
            [](SmBrep& brep)
            {
                Require(SmApiTranslate(&brep, { 1, 0, 0 }) == SM_SUCCESS, "Copy retains its context after every original owner is gone");
            }
        );
        std::cout << "Open shared sheet ownership, source UV, quality, cancellation and copy contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
