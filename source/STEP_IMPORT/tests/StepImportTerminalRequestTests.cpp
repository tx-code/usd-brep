// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportExecution.h"

#include <fstream>
#include <iostream>
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
Options Request(const std::string& hash)
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    TerminalDomainSurfacePlan plan;
    plan.surfaceId = "142";
    plan.pcurveIds = { "961" };
    plan.v = std::array<std::string, 2>{ "-2.", "9." };
    options.terminalDomainInterpretation = TerminalDomainInterpretationRequest{ hash, { plan }, 1, 256, 2 };
    return options;
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "public source pcurve/revolution fixture required");
        const auto raw = step_source::InspectFile(argv[1], step_source::Limits::PlanarFixtures());
        Require(raw.source && raw.status == step_source::Status::Inspected, "public immutable capture");
        auto options = Request(raw.source->Sha256());
        Report report;
        Execution execution(options, report);
        Require(bool(execution.BindTerminalDomainSource(*raw.source)), "request binds immutable source");
        const auto receipt = execution.TerminalDomainInterpretation();
        Require(receipt && report.terminalDomainInterpretation == receipt && report.terminalDomainReceiptRoot, "one request receipt");
        Require(bool(execution.SpendTerminalDomainWork()) && bool(execution.SpendTerminalDomainWork()), "work counter accepts exact budget");
        const auto exceeded = execution.SpendTerminalDomainWork();
        Require(!exceeded && exceeded.error().status == Status::ResourceLimit && receipt->proofWorkUnits == 2, "cumulative work never exceeds budget");
        Require(!execution.CheckTerminalDomainCompletion() && !receipt->requestComplete, "unused plan cannot qualify publication");
        TerminalDomainInterpretationAction action;
        action.sourceSurfaceId = "142";
        action.sourceUseId = "961";
        action.targetNativeDomain = { 0, -2, 360, 9 };
        action.wholeNewDomainErrorUpperMm = 1e-10;
        Require(bool(execution.RecordTerminalDomainAction(0, action)), "recorded surface proof");
        Require(
            !execution.CheckTerminalDomainCompletion() && !receipt->requestComplete,
            "a surface proof without its named pcurve cannot complete the request"
        );
        Require(
            bool(execution.ClaimTerminalDomainPcurve("961")) && bool(execution.CheckTerminalDomainCompletion()),
            "complete explicit action receipt"
        );
        Require(receipt->requestComplete && receipt->actions.size() == 1, "const receipt owns completed evidence");
        auto bad = Request(std::string(64, '0'));
        const auto refused = ImportFile(argv[1], bad);
        Require(
            refused.status == Status::Unsupported && !refused.model && refused.report.nativeCalls == 0,
            "wrong source identity refuses before native allocation"
        );
        Require(refused.report.json.find("TerminalDomainSourceMismatch") != std::string::npos, "wrong input retains explicit diagnostic");
        bad = options;
        bad.terminalDomainInterpretation->maximumProofWorkUnits = 0;
        bool invalid = false;
        try
        {
            ImportFile(argv[1], bad);
        }
        catch (const std::invalid_argument&)
        {
            invalid = true;
        }
        Require(invalid, "zero proof budget has no silent default");
        auto cancelled = options;
        cancelled.execution.control = std::make_shared<ImportControl>();
        Report cancelledReport;
        Execution stopped(cancelled, cancelledReport);
        Require(bool(stopped.BindTerminalDomainSource(*raw.source)), "cancelled request source binding");
        cancelled.execution.control->RequestCancel();
        const auto stop = stopped.SpendTerminalDomainWork();
        Require(
            !stop && stop.error().status == Status::Cancelled && stopped.TerminalDomainInterpretation()->proofWorkUnits == 0,
            "cancel before arithmetic work"
        );
        std::cout << "terminal-domain request identity/budget/receipt contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
