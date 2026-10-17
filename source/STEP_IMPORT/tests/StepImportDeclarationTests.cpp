// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
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
        const auto options = step_import::Options::PlanarFixture();
        for (const auto name : { "S203-P08", "S203-P09", "S203-P10", "S203-P12" })
        {
            auto outcome = step_import::ImportFile(std::filesystem::path(argv[1]) / name / "input.step", options);
            Require(
                outcome.status == step_import::Status::Accepted && outcome.model && outcome.report.declarations,
                "Optional declarations deliver a complete model and report"
            );
            Require(outcome.report.declarations->semantic == outcome.model->Source(), "Report and model retain the same immutable typed source");
            Require(std::abs(outcome.model->Info().areaMm2 - 96) < 1e-6, "Optional declarations do not rewrite F1 geometry");
            for (const auto& tolerance : outcome.report.tolerances)
            {
                Require(tolerance.valueMm == 1e-5, "Source uncertainty never sets working tolerance");
            }
            auto view = outcome.model->Face();
            auto report = outcome.report;
            const auto bytes = report.source->Bytes();
            outcome = {};
            Require(std::abs(view.Closest({ 4.2, 9.6, 8 }).distanceMm - 3) < 1e-7, "Owning view survives the original result");
            const auto& source = *report.declarations->semantic;
            if (std::string_view(name) == "S203-P10")
            {
                const auto mechanical = source.Instances("MECHANICAL_CONTEXT")->at(0);
                const auto design = source.Instances("DESIGN_CONTEXT")->at(0);
                const auto formation = source.Instances("PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE")->at(0);
                Require(
                    source.IsA(mechanical, "PRODUCT_CONTEXT") && source.Field(mechanical, "discipline_type").Value().text == "mechanical",
                    "Legacy mechanical identity and inherited field retained"
                );
                Require(
                    source.IsA(design, "PRODUCT_DEFINITION_CONTEXT") && source.Field(design, "life_cycle_stage").Value().text == "design",
                    "Legacy design identity and inherited field retained"
                );
                Require(source.Field(formation, "make_or_buy").Value().text == "NOT_KNOWN", "Specified SOURCE remains an enum, not a guessed origin");
            }
            if (std::string_view(name) == "S203-P08")
            {
                Require(report.declarations->uncertainties.size() == 2, "Both named source uncertainties retained");
                Require(
                    report.declarations->uncertainties[0].valueMm == step_import::Fraction(1, 10000) &&
                        report.declarations->uncertainties[0].consistencyComparison == "Exceeds",
                    "Advisory uncertainty uses its own physical unit"
                );
            }
            auto exact = options;
            exact.nativeLimits.reportBytes = report.json.size();
            Require(step_import::ImportBytes(bytes, exact).model != nullptr, "Exact complete declaration report budget publishes");
            --exact.nativeLimits.reportBytes;
            const auto failed = step_import::ImportBytes(bytes, exact);
            Require(
                failed.status == step_import::Status::ResourceLimit && !failed.model && failed.report.json.empty() &&
                    failed.report.candidateDiscarded,
                "Incomplete declaration report destroys native candidate"
            );
            Require(
                report.declarations->semantic->Source().Bytes() == bytes && !report.json.empty(),
                "Source declarations remain usable independently of the original result"
            );
        }
        auto imported = step_import::ImportFile(std::filesystem::path(argv[1]) / "S203-P08/input.step", options);
        auto declarations = std::move(*imported.report.declarations);
        imported = {};
        Require(
            declarations.semantic->Source().Find("13") &&
                declarations.semantic->Field(declarations.uncertainties[0].record, "name").Value().text == "distance_accuracy_A",
            "Owning declaration graph survives every native owner"
        );
        std::cout << "Optional source declarations preserve typed identity, quality and ownership\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
