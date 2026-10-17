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
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "Pass frozen case directory");
        for (const auto name : { "S203-P01", "S203-P03", "S203-P04", "S203-P05", "S203-P08", "S203-P11" })
        {
            auto imported = step_import::ImportFile(std::filesystem::path(argv[1]) / name / "input.step", step_import::Options::PlanarFixture());
            Require(bool(imported.model), "Source model available");
            const auto original = imported.model->Info();
            auto query = original.centroidMm;
            query[2] += 3;
            const auto before = imported.model->Closest(query);
            const auto report = imported.report.json;
            auto copy = imported.model->CopyForModeling();
            Require(copy.status == step_import::Status::Accepted && copy.model, "Independent editable copy created");
            copy.model->Edit(
                [&](SmBrep& brep)
                {
                    Require(SmApiTranslate(&brep, { 100, -20, 3 }) == SM_SUCCESS, "Translate editable copy");
                    SmPoint3d minimum, maximum;
                    Require(SmApiBrepBoundingBox(&brep, TRUE, minimum, maximum) == SM_SUCCESS, "Measure translated copy");
                    Require(
                        std::abs(minimum.x - original.boundsMm[0][0] - 100) < 1e-7 && std::abs(maximum.y - original.boundsMm[1][1] + 20) < 1e-7,
                        "Copy geometry changes independently"
                    );
                    long solids = 0, boundedVoids = 0;
                    // This API counts bounded void regions, including the space inside a closed surface model.
                    // That region is not a material cavity and must never grant solid qualification.
                    Require(
                        SmApiBrepMaterialCensus(&brep, solids, boundedVoids) == SM_SUCCESS &&
                            solids == (original.shapeRole == "MaterialSolid" ? 1 : 0) &&
                            boundedVoids == (original.shapeRole == "ClosedSurfaceModel" ? 1 : 0),
                        "Copy preserves material versus closed-surface role"
                    );
                }
            );
            Require(
                imported.model->Info().boundsMm == original.boundsMm && imported.report.json == report,
                "Editing a copy cannot rewrite the original baseline"
            );
            const auto after = imported.model->Closest(query);
            Require(std::abs(after.distanceMm - before.distanceMm) < 1e-7, "Original native geometry remains unchanged");
            for (std::size_t i = 0; i < 3; ++i)
            {
                Require(std::abs(after.pointMm[i] - before.pointMm[i]) < 1e-7, "Original closest point remains unchanged");
            }
            imported = {};
            copy.model->Edit(
                [](SmBrep& brep)
                {
                    Require(SmApiTranslate(&brep, { 1, 0, 0 }) == SM_SUCCESS, "Copy owns its context after source release");
                }
            );
        }
        std::cout << "Independent editable-copy contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
