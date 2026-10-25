// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyImport.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
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
        using namespace step_import;
        Require(argc == 2, "public mixed assembly source required");
        std::ifstream file(argv[1], std::ios::binary);
        Require(bool(file), "fixture exists");
        const std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        auto options = Options::PlanarFixture();
        options.generalBrep = true;
        options.quality = Quality::GeneralDevelopment();
        const AssemblyLimits limits{ 8, 8, 8 };
        const auto result = ImportAssemblyBytes(text, options, limits);
        Require(
            result.status == Status::Accepted && result.assembly && result.assembly->DefinitionCount() == 3 &&
                result.assembly->InstanceCount() == 4 && result.assembly->BodyCount(0) == 1 && result.assembly->BodyCount(1) == 0 &&
                result.assembly->BodyCount(2) == 0 && result.assembly->WireframeModel(2) && !result.assembly->WireframeModel(0),
            "complete source geometry remains in distinct BRep, wrapper and wire definition owners"
        );
        Require(!result.report.json.empty(), "complete mixed assembly retains its mandatory report");
        const auto view = result.assembly->WireframeModel(2)->Wireframes().at(0);
        const auto a = view.EvaluateCurve(1, 3.), b = view.EvaluateCurve(1, 4.);
        Require(
            std::hypot(a[0] - 68. / 9., a[1] + 7. / 9., a[2] - 5.) < 1e-9 && std::hypot(b[0] - 98. / 9., b[1] - 53. / 9., b[2] - 5.) < 1e-9,
            "independent mixed-fixture quadratic endpoint equations"
        );
        auto outside = text;
        const auto at = outside.find("PARAMETER_VALUE(4.)");
        Require(at != std::string::npos, "public late-wire defect target");
        outside.replace(at, std::string("PARAMETER_VALUE(4.)").size(), "PARAMETER_VALUE(6.)");
        const auto failed = ImportAssemblyBytes(outside, options, limits);
        Require(
            !failed.assembly && failed.report.candidateDiscarded && failed.report.definitions.size() == 3 &&
                failed.report.definitions[0].checksPassed && !failed.report.definitions[2].checksPassed,
            "late wire failure disposes earlier BRep definitions without partial publication"
        );
        for (const auto phase : { Phase::SemanticsChecked, Phase::BeforeNativeCall })
        {
            auto cancelledOptions = options;
            auto control = std::make_shared<ImportControl>();
            cancelledOptions.execution.control = control;
            cancelledOptions.execution.progress = [control, phase](Progress progress)
            {
                if (progress.phase == phase)
                {
                    control->RequestCancel();
                }
            };
            auto cancelled = ImportAssemblyBytes(text, cancelledOptions, limits);
            Require(
                cancelled.status == Status::Cancelled && !cancelled.assembly && cancelled.report.nativeCalls == 0,
                "source/native boundary cancellation retains precedence and publishes no assembly"
            );
        }
        std::cout << "Wire-only source/native qualification boundary passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
