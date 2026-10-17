// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <SmApiGeneral.h>
#include <SmApiQueries.h>
#include <SmBrep.h>
#include <algorithm>
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
struct SheetTruth
{
    std::string_view mode;
    std::size_t vertices, edges, freeEdges, uses, toleranceRows;
    double area, uniqueLength, firstArea, secondArea, maxZ;
    step_import::Point centroid, closest;
};
SheetTruth Expected(std::string_view mode)
{
    const SheetTruth cases[] = {
        { "folded", 6, 7, 6, 8, 80, 18, 21, 12, 6, 2, { 8.0 / 3, 1.5, 1.0 / 3 }, { 4, 1.5, 1 } },
        { "coplanar", 6, 7, 6, 8, 80, 18, 21, 12, 6, 0, { 3, 1.5, 0 }, { 5, 1.5, 0 } },
        { "convex", 6, 7, 6, 8, 80, 19, 20 + std::sqrt(5.0), 12, 7, 2, { 52.0 / 19, 91.0 / 57, 22.0 / 57 }, { 4, 1.5, 1 } },
        { "triangular", 4, 5, 4, 6, 60, 9, 14 + std::sqrt(13.0), 6, 3, 2, { 28.0 / 9, 1, 2.0 / 9 }, { 4, 1.5, 1 } },
        { "pentagonal", 7, 8, 7, 9, 90, 21, 16 + 3 * std::sqrt(5.0), 12, 9, 3, { 20.0 / 7, 103.0 / 63, 4.0 / 7 }, { 4, 1.5, 1 } },
        { "concave", 7, 8, 7, 9, 90, 17, 16 + 3 * std::sqrt(5.0), 12, 5, 2, { 44.0 / 17, 79.0 / 51, 4.0 / 17 }, { 4, 1.5, 1 } },
        { "collinear", 7, 8, 7, 9, 90, 19, 20 + std::sqrt(5.0), 12, 7, 2, { 52.0 / 19, 91.0 / 57, 22.0 / 57 }, { 4, 1.5, 1 } }
    };
    const auto found = std::find_if(
        std::begin(cases),
        std::end(cases),
        [&](const auto& row)
        {
            return row.mode == mode;
        }
    );
    Require(found != std::end(cases), "Known independently specified sheet mode");
    return *found;
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2 || argc == 3, "Pass sheet fixture and optional truth mode");
        const auto truth = Expected(argc == 3 ? argv[2] : "folded");
        const auto options = step_import::Options::PlanarFixture();
        auto imported = step_import::ImportFile(argv[1], options);
        Require(imported.status == step_import::Status::Accepted && imported.model, "S1 yields an owning open sheet");
        const auto original = imported.model->Info();
        Require(
            original.faces == 2 && original.edges == truth.edges && original.vertices == truth.vertices &&
                original.freeBoundaryEdges == truth.freeEdges,
            "Independent variable sheet census"
        );
        Near(original.areaMm2, truth.area, "Both face areas contribute");
        Near(original.boundaryMm, truth.uniqueLength, "Shared edge counted once in unique edge length");
        for (std::size_t i = 0; i < 3; ++i)
        {
            Near(original.centroidMm[i], truth.centroid[i], "Independent area-weighted sheet centroid");
        }
        Require(!original.normal && !original.windowMm && !imported.model->MaterialVolumeMm3(), "No shape-wide plane or material volume");
        Require(!imported.model->ClassifyMaterial({ 2, 1, 0 }), "An open sheet never acquires material classification");
        Require(
            imported.report.nativeRole && imported.report.nativeRole->materialRegions == 0 && imported.report.nativeRole->boundedVoidRegions == 0 &&
                !imported.report.nativeRole->manifoldSolidPredicate,
            "Actual native spatial role remains an open surface"
        );
        Require(imported.report.tolerances.size() == truth.toleranceRows, "Five stages cover BRep, faces, edges and vertices");
        for (const auto& tolerance : imported.report.tolerances)
        {
            Require(tolerance.valueMm == 1e-5, "Canonical working tolerance never grows");
        }
        std::size_t logicalUses = 0;
        std::size_t sharedUses = 0;
        for (const auto& loop : imported.report.loops)
        {
            logicalUses += loop.uses.size();
            for (const auto& use : loop.uses)
            {
                if (use.edgeId == "601" && loop.faceId == "704")
                {
                    ++sharedUses;
                    Require(
                        use.uvStartMm == std::array<double, 2>{ 4, 0 } && use.uvEndMm == std::array<double, 2>{ 4, 3 },
                        "FA shared side uses its own UV and direction"
                    );
                }
                if (use.edgeId == "601" && loop.faceId == "714")
                {
                    ++sharedUses;
                    Require(
                        use.uvStartMm == std::array<double, 2>{ 0, 3 } && use.uvEndMm == std::array<double, 2>{ 0, 0 },
                        "FB shared side uses its own reversed UV"
                    );
                }
            }
        }
        Require(logicalUses == truth.uses && sharedUses == 2, "Every logical source occurrence and both shared uses retained");
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
            [&truth](SmBrep& brep)
            {
                Require(SmApiTranslate(&brep, { 100, -20, 3 }) == SM_SUCCESS, "Translate independent open sheet copy");
                SmPoint3d minimum, maximum;
                Require(SmApiBrepBoundingBox(&brep, TRUE, minimum, maximum) == SM_SUCCESS, "Measure translated copy");
                Near(minimum.x, 100, "Copy minimum x");
                Near(maximum.z, truth.maxZ + 3, "Copy maximum z");
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
        for (std::size_t i = 0; i < 3; ++i)
        {
            Near(nearSecondFace.pointMm[i], truth.closest[i], "Whole-sheet query reaches the second face");
        }
        imported = {};
        for (const auto& view : views)
        {
            Near(view.Info().areaMm2, view.SourceId() == "704" ? truth.firstArea : truth.secondArea, "Owning view survives original result release");
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
