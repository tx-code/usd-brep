// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportExecution.h"

#include <algorithm>
#include <new>
#ifdef _WIN32
// clang-format off: psapi.h requires Windows types before its declarations.
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

namespace step_import
{
CancelResult ImportControl::RequestCancel() noexcept
{
    auto expected = State::Ready;
    if (m_state.compare_exchange_strong(expected, State::Requested))
    {
        return CancelResult::Requested;
    }
    return expected == State::Requested ? CancelResult::AlreadyRequested :
           expected == State::Accepted  ? CancelResult::TooLate :
                                          CancelResult::AlreadyCompleted;
}
const char* PhaseName(Phase phase)
{
    switch (phase)
    {
        case Phase::NotStarted:
            return "NotStarted";
        case Phase::BeforeCapture:
            return "BeforeCapture";
        case Phase::Captured:
            return "Captured";
        case Phase::SemanticsChecked:
            return "SemanticsChecked";
        case Phase::PlanarChecked:
            return "PlanarChecked";
        case Phase::BeforeNativeCall:
            return "BeforeNativeCall";
        case Phase::NativeReady:
            return "NativeReady";
        case Phase::BeforeReport:
            return "BeforeReport";
        case Phase::ReportReady:
            return "ReportReady";
        case Phase::BeforeCommit:
            return "BeforeCommit";
        case Phase::BeforeRepairProof:
            return "BeforeRepairProof";
        case Phase::BeforeRepair:
            return "BeforeRepair";
        case Phase::RepairReady:
            return "RepairReady";
        case Phase::BeforeDefinitionSelection:
            return "BeforeDefinitionSelection";
        case Phase::DefinitionSelectionReady:
            return "DefinitionSelectionReady";
    }
    throw std::logic_error("Unknown execution boundary");
}
namespace detail
{
Execution::Execution(const Options& options, Report& report)
    : m_options(options), m_report(report), m_control(options.execution.control ? options.execution.control : std::make_shared<ImportControl>())
{
    if (m_control->m_started.exchange(true))
    {
        throw std::invalid_argument("ImportControl is single-use; create a new control for each invocation");
    }
    m_report.processPrivateLimitBytes = options.nativeLimits.processPrivateBytes;
    m_report.hardMemoryLimitBytes = options.nativeLimits.hardMemoryBytes;
    m_report.deadlineConfigured = bool(options.execution.deadline);
    if (options.faceOrientationRepair)
    {
        m_repairEditsClaimed.resize(options.faceOrientationRepair->edits.size(), false);
    }
}
void Execution::ClaimRepairEdits(const std::vector<std::size_t>& indices)
{
    for (const auto index : indices)
    {
        if (index >= m_repairEditsClaimed.size() || m_repairEditsClaimed[index])
        {
            throw Failure{ Status::Unsupported,
                           "RepairEditScopeConflict",
                           "An edit must apply to exactly one selected definition/body",
                           {},
                           "Repair" };
        }
        m_repairEditsClaimed[index] = true;
    }
}
void Execution::CheckRepairCompletion()
{
    if (std::find(m_repairEditsClaimed.begin(), m_repairEditsClaimed.end(), false) != m_repairEditsClaimed.end())
    {
        throw Failure{ Status::Unsupported, "RepairEditNotApplied", "The complete selected request did not apply every explicit edit", {}, "Repair" };
    }
}
std::size_t Execution::RepairProofRemaining() const
{
    if (!m_options.faceOrientationRepair || m_report.repairProofProducts >= m_options.faceOrientationRepair->maximumProofProducts)
    {
        return 0;
    }
    return m_options.faceOrientationRepair->maximumProofProducts - m_report.repairProofProducts;
}
void Execution::SpendRepairProof(std::size_t products)
{
    if (products > RepairProofRemaining())
    {
        throw Failure{ Status::ResourceLimit, "RepairProofWorkLimit", "Request-cumulative repair proof budget exhausted", {}, "Repair" };
    }
    m_report.repairProofProducts += products;
}
Execution::~Execution()
{
    if (!m_committed)
    {
        auto state = m_control->m_state.load();
        while ((state == ImportControl::State::Ready || state == ImportControl::State::Requested) &&
               !m_control->m_state.compare_exchange_weak(state, ImportControl::State::Failed))
        {
        }
    }
}
void Execution::Cancel()
{
    auto requested = ImportControl::State::Requested;
    m_control->m_state.compare_exchange_strong(requested, ImportControl::State::Cancelled);
    throw Failure{ Status::Cancelled, "ImportCancelled", "Cooperative cancellation observed at a safe boundary", {}, "Execution" };
}
void Execution::ChargeNative(Report& observation, std::size_t record)
{
    Check(Phase::BeforeNativeCall);
    if (m_report.nativeCalls >= m_options.nativeLimits.nativeCalls)
    {
        throw Failure{ Status::ResourceLimit, "NativeCallLimit", "General source-to-native contract is unestablished", record, "GeneralBrep" };
    }
    ++m_report.nativeCalls;
    if (&observation != &m_report)
    {
        ++observation.nativeCalls;
    }
}
void Execution::Check(Phase phase)
{
    m_report.lastBoundary = phase;
    ++m_report.checkpoints;
    if (m_options.nativeLimits.hardMemoryBytes)
    {
        throw Failure{ Status::Unsupported,
                       "UnsupportedHardMemoryContract",
                       "OS hard memory limits require a qualified isolated execution mode",
                       {},
                       "Execution" };
    }
    if (m_options.execution.progress)
    {
        try
        {
            m_options.execution.progress({ phase, m_report.nativeCalls });
        }
        catch (const std::bad_alloc&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            throw Failure{ Status::BuildFailure, "ExecutionCallbackFailure", error.what(), {}, "Execution" };
        }
        catch (...)
        {
            throw Failure{ Status::BuildFailure, "ExecutionCallbackFailure", "Unknown observer exception", {}, "Execution" };
        }
    }
    if (m_control->m_state.load() == ImportControl::State::Requested)
    {
        Cancel();
    }
    if (m_options.execution.deadline && std::chrono::steady_clock::now() >= *m_options.execution.deadline)
    {
        throw Failure{ Status::ResourceLimit, "DeadlineExceeded", "Cooperative deadline observed after a safe operation boundary", {}, "Execution" };
    }
    if (m_options.nativeLimits.processPrivateBytes || m_options.execution.measureProcessMemory)
    {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX memory{};
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        {
            throw Failure{ Status::BuildFailure, "ProcessMemoryMeasurementFailed", "GetProcessMemoryInfo failed", {}, "Execution" };
        }
        m_report.maximumObservedPrivateBytes = std::max(
            m_report.maximumObservedPrivateBytes.value_or(0),
            static_cast<std::size_t>(memory.PrivateUsage)
        );
        m_report.processPeakCommitBytes = static_cast<std::size_t>(memory.PeakPagefileUsage);
        if (m_options.nativeLimits.processPrivateBytes && memory.PrivateUsage > *m_options.nativeLimits.processPrivateBytes)
        {
            throw Failure{ Status::ResourceLimit,
                           "ProcessMemoryLimit",
                           "Process-wide private commit charge exceeds the cooperative byte limit",
                           {},
                           "Execution" };
        }
#else
        throw Failure{ Status::Unsupported,
                       "UnsupportedProcessMemoryObserver",
                       "This execution mode has no qualified process memory observer",
                       {},
                       "Execution" };
#endif
    }
}
void Execution::Commit()
{
    // Final serialization is bounded but can take time/allocate. Recheck declared limits before arbitration.
    // Accepted report observations remain explicitly scoped to the pre-serialization checkpoint.
    if (m_options.execution.deadline && std::chrono::steady_clock::now() >= *m_options.execution.deadline)
    {
        throw Failure{ Status::ResourceLimit, "DeadlineExceeded", "Deadline reached before publication", {}, "Execution" };
    }
#ifdef _WIN32
    if (m_options.nativeLimits.processPrivateBytes)
    {
        PROCESS_MEMORY_COUNTERS_EX memory{};
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        {
            throw Failure{ Status::BuildFailure, "ProcessMemoryMeasurementFailed", "Final memory guard failed", {}, "Execution" };
        }
        if (memory.PrivateUsage > *m_options.nativeLimits.processPrivateBytes)
        {
            m_report.maximumObservedPrivateBytes = std::max(
                m_report.maximumObservedPrivateBytes.value_or(0),
                static_cast<std::size_t>(memory.PrivateUsage)
            );
            m_report.processPeakCommitBytes = static_cast<std::size_t>(memory.PeakPagefileUsage);
            throw Failure{ Status::ResourceLimit, "ProcessMemoryLimit", "Process memory limit reached before publication", {}, "Execution" };
        }
    }
#endif
    auto expected = ImportControl::State::Ready;
    if (!m_control->m_state.compare_exchange_strong(expected, ImportControl::State::Accepted))
    {
        Cancel();
    }
    m_committed = true;
}
} // namespace detail
} // namespace step_import
