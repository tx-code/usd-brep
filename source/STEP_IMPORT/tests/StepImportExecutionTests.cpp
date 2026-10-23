// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportExecution.h"
#include "StepImportExecutionCallbacks.h"
#include "StepImportResult.h"

#include <concepts>
#include <iostream>
#include <stdexcept>

using namespace step_import;
using namespace step_import::detail;
static_assert(std::same_as<decltype(std::declval<Execution&>().Check(Phase::BeforeNativeCall)), Result<void>>);
static_assert(!std::constructible_from<ExecutionCheckpoint, decltype([] {})>);
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
            Require(bool(execution.ClaimCylindricalBandRepairs({ 0 })), "repair ledger claim is admitted");
        }
        else
        {
            Require(bool(execution.ClaimRepairEdits({ 0 })), "repair ledger claim is admitted");
        }
        const auto incomplete = execution.CheckRepairCompletion();
        Require(
            !incomplete && incomplete.error().status == Status::Unsupported &&
                incomplete.error().key == (band ? "CylindricalBandRepairNotApplied" : "RepairEditNotApplied"),
            "uncompleted explicit repair work returns its own error"
        );
        if (band)
        {
            Require(bool(execution.ClaimCylindricalBandRepairs({ 1 })), "repair ledger claim is admitted");
        }
        else
        {
            Require(bool(execution.ClaimRepairEdits({ 1 })), "repair ledger claim is admitted");
        }
        Require(bool(execution.CheckRepairCompletion()), "complete claimed work permits publication");
        Require(report.checkpoints == 0 && report.nativeCalls == 0, "repair admission settlement adds no work");
    }
}
void NativeLedger()
{
    auto options = Options::PlanarFixture();
    options.nativeLimits.nativeCalls = 2;
    options.execution.control = std::make_shared<ImportControl>();
    Report root, leaf;
    Execution execution(options, root);
    Require(bool(execution.ChargeNative(root, 17)), "root native work is admitted");
    Require(root.nativeCalls == 1, "root alias is charged once");
    Require(bool(execution.ChargeNative(leaf, 19)), "leaf native work uses the same request budget");
    const auto exhausted = execution.ChargeNative(leaf, 23);
    Require(
        !exhausted && exhausted.error().status == Status::ResourceLimit && exhausted.error().key == "NativeCallLimit" &&
            exhausted.error().record == 23 && exhausted.error().stage == "GeneralBrep",
        "native quota returns its exact source-bound failure"
    );
    Require(root.nativeCalls == 2 && leaf.nativeCalls == 1 && root.checkpoints == 3, "rejected work does not charge root or leaf");
    options.execution.control->RequestCancel();
    const auto cancelled = execution.ChargeNative(leaf, 29);
    Require(
        !cancelled && cancelled.error().status == Status::Cancelled && root.nativeCalls == 2 && leaf.nativeCalls == 1 && root.checkpoints == 4 &&
            root.lastBoundary == Phase::BeforeNativeCall,
        "cancellation check precedes a depleted native allowance"
    );
}
void CheckOrder()
{
    for (const bool observerFails : { false, true })
    {
        auto options = Options::PlanarFixture();
        options.execution.control = std::make_shared<ImportControl>();
        options.execution.control->RequestCancel();
        options.execution.deadline = std::chrono::steady_clock::now();
        unsigned callbacks = 0;
        options.execution.progress = [&](Progress)
        {
            ++callbacks;
            if (observerFails)
            {
                throw std::runtime_error("execution observer fault");
            }
        };
        Report report;
        Execution execution(options, report);
        const auto result = execution.Check(Phase::BeforeNativeCall);
        Require(
            !result && result.error().status == (observerFails ? Status::BuildFailure : Status::Cancelled) &&
                result.error().key == (observerFails ? "ExecutionCallbackFailure" : "ImportCancelled") && result.error().stage == "Execution",
            "observer failure then cancellation retain their priority over deadline"
        );
        Require(callbacks == 1 && report.checkpoints == 1 && report.nativeCalls == 0, "one checkpoint observes exactly one callback");
    }
    auto options = Options::PlanarFixture();
    options.nativeLimits.hardMemoryBytes = 1;
    unsigned callbacks = 0;
    options.execution.progress = [&](Progress)
    {
        ++callbacks;
    };
    Report report;
    Execution execution(options, report);
    const auto result = execution.Check(Phase::BeforeCapture);
    Require(
        !result && result.error().status == Status::Unsupported && result.error().key == "UnsupportedHardMemoryContract" && callbacks == 0 &&
            report.checkpoints == 1,
        "unsupported hard-memory admission precedes the observer"
    );
}
void ProofLedger()
{
    auto options = Options::PlanarFixture();
    options.faceOrientationRepair.emplace();
    options.faceOrientationRepair->maximumProofProducts = 5;
    Report report;
    Execution execution(options, report);
    Require(bool(execution.SpendRepairProof(3)), "proof work within the request allowance succeeds");
    const auto overflow = execution.SpendRepairProof(3);
    Require(
        !overflow && overflow.error().status == Status::ResourceLimit && overflow.error().key == "RepairProofWorkLimit" &&
            report.repairProofProducts == 3 && execution.RepairProofRemaining() == 2,
        "rejected proof work leaves cumulative spending unchanged"
    );
    Require(bool(execution.SpendRepairProof(2)) && bool(execution.SpendRepairProof(0)), "exact proof allowance and zero work remain admissible");
    Require(!execution.SpendRepairProof(1) && report.repairProofProducts == 5 && report.checkpoints == 0, "spent proof quota never resets");
    auto band = Options::PlanarFixture();
    band.cylindricalBandRepair.emplace();
    band.cylindricalBandRepair->maximumProofOperations = 65;
    Report bandReport;
    Execution bandExecution(band, bandReport);
    for (unsigned i = 0; i < 65; ++i)
    {
        Require(bool(bandExecution.SpendCylindricalBandProof()), "band proof operation is admitted");
    }
    const auto bandOverflow = bandExecution.SpendCylindricalBandProof();
    Require(
        !bandOverflow && bandOverflow.error().key == "CylindricalBandRepairProofLimit" && bandReport.cylindricalBandProofOperations == 65 &&
            bandReport.checkpoints == 2,
        "band proof keeps its original charge-before-check cadence and exact cap"
    );
}
void RefusalDetails()
{
    auto options = Options::PlanarFixture();
    options.nativeLimits.nativeCalls = 0;
    Report report;
    Execution execution(options, report);
    const auto refused = execution.ChargeNative(report, 37, NativeCallAccounting::Planar);
    Require(
        !refused && refused.error().record == 37 && refused.error().stage == "D5" &&
            refused.error().message == "Bounded native call allowance exhausted" && report.nativeCalls == 0,
        "planar quota retains its distinct source-bound diagnostic and does not charge refused work"
    );
    for (const bool allocation : { false, true })
    {
        auto observer = Options::PlanarFixture();
        observer.execution.progress = [=](Progress)
        {
            if (allocation)
            {
                throw std::bad_alloc();
            }
            throw 17;
        };
        Report observed;
        Execution request(observer, observed);
        bool propagated = false;
        try
        {
            const auto result = request.Check(Phase::BeforeNativeCall);
            Require(!allocation && !result && result.error().key == "ExecutionCallbackFailure", "unknown observer exception returns callback failure");
        }
        catch (const std::bad_alloc&)
        {
            propagated = true;
        }
        Require(
            propagated == allocation && observed.checkpoints == 1 && observed.nativeCalls == 0,
            "STL allocation failure propagates without becoming a configured quota"
        );
    }
}
void ClaimAndProofRefusal()
{
    for (const bool band : { false, true })
    {
        auto options = Options::PlanarFixture();
        options.execution.control = std::make_shared<ImportControl>();
        options.faceOrientationRepair.emplace();
        options.faceOrientationRepair->edits.resize(2);
        options.cylindricalBandRepair.emplace();
        options.cylindricalBandRepair->faces.resize(2);
        options.cylindricalBandRepair->maximumProofOperations = 65;
        Report report;
        Execution request(options, report);
        const auto claim = [&](std::vector<std::size_t> indices)
        {
            return band ? request.ClaimCylindricalBandRepairs(indices) : request.ClaimRepairEdits(indices);
        };
        const auto conflict = claim({ 0, 2 });
        Require(
            !conflict && conflict.error().key == (band ? "CylindricalBandRepairScopeConflict" : "RepairEditScopeConflict"),
            "out-of-scope claim returns the owning repair error"
        );
        Require(!claim({ 0 }) && bool(claim({ 1 })), "claim conflict preserves the already claimed prefix");
        options.execution.control->RequestCancel();
        const auto cancelled = request.SpendCylindricalBandProof();
        Require(
            !cancelled && cancelled.error().status == Status::Cancelled && report.cylindricalBandProofOperations == 1 && report.checkpoints == 1,
            "band proof preserves charge-before-check behavior when its first checkpoint cancels"
        );
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
        NativeLedger();
        CheckOrder();
        ProofLedger();
        RefusalDetails();
        ClaimAndProofRefusal();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
