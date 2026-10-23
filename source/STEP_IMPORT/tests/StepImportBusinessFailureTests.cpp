// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportExecution.h"
#include "StepImportNativeBuild.h"
#include "StepImportPlan.h"

#include <cfenv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace step_import;
using namespace step_import::detail;
namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
void PlanarBusinessErrors(const Plan& plan, bool late)
{
    auto options = Options::PlanarFixture();
    Report report;
    Execution execution(options, report);
    if (late)
    {
        options.quality.definitionMm = Fraction(1, 1000000000000000LL);
        const auto result = Build(plan, options, report, execution);
        Require(
            !result && result.error().status == Status::Indeterminate && result.error().key == "NativeDefinitionErrorBudget" &&
                result.error().record == plan.faces.front().face && result.error().stage == "D5",
            "conservative proof refusal returns its source-bound business failure"
        );
        Require(
            report.nativeCalls > 0 && report.candidateDiscarded && report.definitionBoundMm.value_or(0) > 0 && report.pointerReports == 0 &&
                report.deepReports == 0,
            "returned proof refusal retains validation and numerical observations and discards its candidate"
        );
    }
    else
    {
        struct RestoreRounding
        {
            int value = std::fegetround();
            ~RestoreRounding()
            {
                std::fesetround(value);
            }
        } restore;
        Require(std::fesetround(FE_UPWARD) == 0, "unsupported native rounding fixture");
        const auto result = Build(plan, options, report, execution);
        Require(
            !result && result.error().status == Status::Indeterminate && result.error().key == "UnsupportedNativeNumericEnvironment" &&
                result.error().record == plan.record && result.error().stage == "D5",
            "native numeric-environment admission returns its business failure"
        );
        Require(
            report.nativeCalls == 0 && report.checkpoints == 0 && !report.candidateDiscarded,
            "early refusal invents neither work nor a held candidate"
        );
    }
}
void PlanarCorrespondenceErrors(Plan plan)
{
    // Deliberately contradict only the selected-use evidence. Native defining
    // geometry and the immutable qualified source remain unchanged.
    auto& use = plan.faces.front().loops.front().selectedUses.front();
    use.forward = !use.forward;
    const auto options = Options::PlanarFixture();
    Report report;
    Execution execution(options, report);
    const auto result = Build(plan, options, report, execution);
    Require(
        !result && result.error().status == Status::PostcheckFailure && result.error().key == "NativeDirectedSourcePathMismatch" &&
            result.error().record == plan.record && result.error().stage == "D5",
        "directed native/source disagreement returns its postcheck business failure"
    );
    Require(
        report.candidateDiscarded && report.nativeCalls > 0 && report.pointerReports == 0 && report.deepReports == 0 && report.loops.empty() &&
            !report.definitionBoundMm,
        "correspondence refusal retains completed validators and prevents later proof or report rows"
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2 || argc == 3, "public planar fixture and optional --late required");
        std::ifstream input(argv[1], std::ios::binary);
        const std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        const auto options = Options::PlanarFixture();
        const auto raw = step_source::InspectBytes(text, options.sourceLimits);
        const auto semantics = step_semantics::Check(raw, options.semanticLimits);
        const auto checked = step_planar::Check(semantics, { options.quality.id, options.quality.consistencyMm }, options.planarLimits);
        Require(checked.status == step_planar::Status::Checked, "public fixture qualifies before native construction");
        const auto plan = MakePlan(checked);
        if (argc == 3 && std::string_view(argv[2]) == "--mapping")
        {
            PlanarCorrespondenceErrors(plan);
        }
        else
        {
            PlanarBusinessErrors(plan, argc == 3);
        }
        return 0;
    }
    catch (const Failure& error)
    {
        std::cerr << "Business Failure escaped the result seam: " << error.key << '\n';
        return 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
