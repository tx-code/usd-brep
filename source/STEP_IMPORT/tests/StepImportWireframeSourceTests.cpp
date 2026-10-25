// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyImport.h"

#include <algorithm>
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
            result.status == Status::Unsupported && !result.assembly && result.report.nativeCalls == 0 &&
                std::any_of(
                    result.report.diagnostics.begin(),
                    result.report.diagnostics.end(),
                    [](const auto& diagnostic)
                    {
                        return diagnostic.source.key == "AssemblyWireframeConstructionUnimplemented" && diagnostic.source.recordId == "967";
                    }
                ),
            "qualified source geometry cannot become an empty body, skipped leaf or partially published assembly"
        );
        Require(!result.report.json.empty(), "explicit missing native capability retains its complete failure report");
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
