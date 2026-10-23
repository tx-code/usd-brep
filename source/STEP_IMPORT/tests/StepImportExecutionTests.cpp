// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportExecution.h"
#include "StepImportResult.h"

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
void CommitResult(bool cancel, bool expired)
{
    auto options = Options::PlanarFixture();
    options.execution.control = std::make_shared<ImportControl>();
    if (expired)
    {
        options.execution.deadline = std::chrono::steady_clock::now();
    }
    Report report;
    {
        Execution execution(options, report);
        if (cancel)
        {
            Require(options.execution.control->RequestCancel() == CancelResult::Requested, "pending cancellation is observable");
        }
        const auto result = execution.Commit();
        if (expired)
        {
            // Commit retains the original final-guard order: deadline/memory
            // precede the final CAS, unlike Check's observer/cancel/deadline order.
            Require(
                !result && result.error().status == Status::ResourceLimit && result.error().key == "DeadlineExceeded" &&
                    result.error().stage == "Execution",
                "final deadline guard returns its original error before cancellation arbitration"
            );
        }
        else if (cancel)
        {
            Require(
                !result && result.error().status == Status::Cancelled && result.error().key == "ImportCancelled" &&
                    result.error().stage == "Execution",
                "failed final CAS returns cancellation without an exception"
            );
        }
        else
        {
            Require(bool(result), "ready execution commits successfully");
            Require(options.execution.control->RequestCancel() == CancelResult::TooLate, "successful commit wins the one terminal decision");
        }
        Require(report.nativeCalls == 0 && report.checkpoints == 0, "commit introduces no native work, callback or extra checkpoint");
    }
    Require(
        options.execution.control->RequestCancel() == (!cancel && !expired ? CancelResult::TooLate : CancelResult::AlreadyCompleted),
        "execution destruction preserves the committed or failed terminal state"
    );
}
void FinalMemoryGuard()
{
#ifdef _WIN32
    auto options = Options::PlanarFixture();
    options.nativeLimits.processPrivateBytes = 1;
    Report report;
    Execution execution(options, report);
    const auto result = execution.Commit();
    Require(
        !result && result.error().status == Status::ResourceLimit && result.error().key == "ProcessMemoryLimit" &&
            result.error().stage == "Execution" && report.maximumObservedPrivateBytes.value_or(0) > 1 && report.processPeakCommitBytes,
        "final process memory guard returns its error and retains observed memory facts"
    );
    Require(report.checkpoints == 0 && report.nativeCalls == 0, "final memory guard is not another checkpoint");
#endif
}
void RepairCompletion()
{
    for (const bool band : { false, true })
    {
        auto options = Options::PlanarFixture();
        // Ledger-only controls do not perform source admission or establish
        // geometric repair evidence. Test each independently admitted role.
        if (band)
        {
            options.cylindricalBandRepair.emplace();
            options.cylindricalBandRepair->faces.resize(2);
        }
        else
        {
            options.faceOrientationRepair.emplace();
            options.faceOrientationRepair->edits.resize(2);
        }
        Report report;
        Execution execution(options, report);
        if (band)
        {
            execution.ClaimCylindricalBandRepairs({ 0 });
        }
        else
        {
            execution.ClaimRepairEdits({ 0 });
        }
        const auto incomplete = execution.CheckRepairCompletion();
        Require(
            !incomplete && incomplete.error().status == Status::Unsupported &&
                incomplete.error().key == (band ? "CylindricalBandRepairNotApplied" : "RepairEditNotApplied"),
            "uncompleted explicit repair work returns its own error"
        );
        if (band)
        {
            execution.ClaimCylindricalBandRepairs({ 1 });
        }
        else
        {
            execution.ClaimRepairEdits({ 1 });
        }
        Require(bool(execution.CheckRepairCompletion()), "complete claimed work permits publication");
        Require(report.checkpoints == 0 && report.nativeCalls == 0, "repair admission settlement adds no work");
    }
}
} // namespace
int main()
{
    try
    {
        CommitResult(false, false);
        CommitResult(true, false);
        CommitResult(false, true);
        CommitResult(true, true);
        FinalMemoryGuard();
        RepairCompletion();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
