// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportExecution.h"

#include "StepImportInterval.h"
#include "StepImportSurfaceCurveSession.h"
#include "StepImportSurfacePrecisionSession.h"

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
Result<void> Execution::BindSourceInterpretations(const step_source::SourceSnapshot& source)
{
    if (auto bound = BindTerminalDomainSource(source); !bound)
    {
        return tl::unexpected(std::move(bound.error()));
    }
    if (m_surfaceCurves)
    {
        if (auto bound = m_surfaceCurves->BindSource(source); !bound)
        {
            return tl::unexpected(std::move(bound.error()));
        }
    }
    return m_targetSurfaces ? m_targetSurfaces->BindSource(source) : Result<void>{};
}
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
    if (options.targetSurfacePrecision)
    {
        m_targetSurfaces = std::make_unique<TargetSurfacePrecisionSession>(options);
        m_report.targetSurfacePrecision = m_targetSurfaces->Evidence();
        m_report.targetSurfacePrecisionReceiptRoot = true;
    }
    if (options.surfaceCurve3dInterpretation)
    {
        m_surfaceCurves = std::make_unique<SurfaceCurve3dInterpretationSession>(options);
        m_report.surfaceCurve3dInterpretation = m_surfaceCurves->Evidence();
        m_report.surfaceCurve3dReceiptRoot = true;
    }
    if (options.terminalDomainInterpretation)
    {
        const auto& request = *options.terminalDomainInterpretation;
        m_terminalDomainInterpretation = std::make_shared<TerminalDomainInterpretationReceipt>();
        m_terminalDomainInterpretation->sourceSha256 = request.sourceSha256;
        m_terminalDomainInterpretation->plans = request.surfaces;
        m_terminalDomainInterpretation->maximumSurfaces = request.maximumSurfaces;
        m_terminalDomainInterpretation->maximumProofControls = request.maximumProofControls;
        m_terminalDomainInterpretation->maximumProofWorkUnits = request.maximumProofWorkUnits;
        m_terminalDomainPlansApplied.resize(request.surfaces.size(), false);
        m_report.terminalDomainInterpretation = m_terminalDomainInterpretation;
        m_report.terminalDomainReceiptRoot = true;
    }
    if (options.faceOrientationRepair)
    {
        m_repairEditsClaimed.resize(options.faceOrientationRepair->edits.size(), false);
    }
    if (options.cylindricalBandRepair)
    {
        m_cylindricalBandRepairsClaimed.resize(options.cylindricalBandRepair->faces.size(), false);
        if (options.cylindricalBandRepair->circleVertices)
        {
            const auto& request = *options.cylindricalBandRepair->circleVertices;
            m_circleVerticesClaimed.resize(request.vertices.size(), false);
            m_circleVertexBudget = std::make_shared<CircleVertexBudgetReceipt>();
            m_circleVertexBudget->maximumVertices = request.maximumVertices;
            m_circleVertexBudget->maximumDisplacementMm = request.maximumDisplacementMm;
            m_circleVertexBudget->maximumTotalDisplacementMm = request.maximumTotalDisplacementMm;
            m_report.circleVertexBudget = m_circleVertexBudget;
        }
    }
}
[[nodiscard]] Result<void> Execution::ClaimRepairEdits(const std::vector<std::size_t>& indices)
{
    for (const auto index : indices)
    {
        if (index >= m_repairEditsClaimed.size() || m_repairEditsClaimed[index])
        {
            return tl::unexpected(
                Failure{ Status::Unsupported, "RepairEditScopeConflict", "An edit must apply to exactly one selected definition/body", {}, "Repair" }
            );
        }
        m_repairEditsClaimed[index] = true;
    }
    return {};
}
[[nodiscard]] Result<void> Execution::ClaimCylindricalBandRepairs(const std::vector<std::size_t>& indices)
{
    for (const auto index : indices)
    {
        if (index >= m_cylindricalBandRepairsClaimed.size() || m_cylindricalBandRepairsClaimed[index])
        {
            return tl::unexpected(
                Failure{ Status::Unsupported,
                         "CylindricalBandRepairScopeConflict",
                         "Each planned face use must be repaired exactly once",
                         {},
                         "Repair" }
            );
        }
        m_cylindricalBandRepairsClaimed[index] = true;
    }
    return {};
}
[[nodiscard]] Result<void> Execution::SpendCylindricalBandProof()
{
    if (!m_options.cylindricalBandRepair || m_report.cylindricalBandProofOperations >= m_options.cylindricalBandRepair->maximumProofOperations)
    {
        return tl::unexpected(
            Failure{ Status::ResourceLimit, "CylindricalBandRepairProofLimit", "Cumulative exact proof work exhausted", {}, "Repair" }
        );
    }
    if ((m_report.cylindricalBandProofOperations++ & 63) == 0)
    {
        return Check(Phase::BeforeRepairProof);
    }
    return {};
}
Result<void> Execution::ClaimCircleVertexCorrections(
    const std::vector<std::size_t>& indices,
    const std::vector<CircleVertexCorrectionEvidence>& evidence
)
{
    if (m_committed || indices.empty() || indices.size() != evidence.size() || !m_options.cylindricalBandRepair ||
        !m_options.cylindricalBandRepair->circleVertices)
    {
        return tl::unexpected(
            Failure{ Status::Unsupported, "CircleVertexCorrectionScopeConflict", "Complete scoped vertex evidence required", {}, "Repair" }
        );
    }
    const auto& request = *m_options.cylindricalBandRepair->circleVertices;
    double total = m_circleVertexMovementUpperMm, protection = m_circleVertexProtectionLowerMm;
    try
    {
        for (std::size_t i = 0; i < indices.size(); ++i)
        {
            if (indices[i] >= m_circleVerticesClaimed.size() || m_circleVerticesClaimed[indices[i]] || (i && indices[i] <= indices[i - 1]) ||
                !(evidence[i].displacementUpperMm > 0) || !std::isfinite(evidence[i].displacementUpperMm) ||
                !(evidence[i].localProtectionLowerMm > 0) || !std::isfinite(evidence[i].localProtectionLowerMm))
            {
                return tl::unexpected(
                    Failure{ Status::Unsupported, "CircleVertexCorrectionScopeConflict", "Every planned vertex use applies once", {}, "Repair" }
                );
            }
            total = Up(total + evidence[i].displacementUpperMm);
            protection = std::min(protection, evidence[i].localProtectionLowerMm);
        }
        const double localLimit = Down(protection / 100.);
        if (total > Enclose(request.maximumTotalDisplacementMm).low || total > localLimit)
        {
            return tl::unexpected(
                Failure{ Status::Indeterminate,
                         "CircleVertexCumulativeMovementBudget",
                         "Original-model cumulative movement/protection budget exceeded",
                         {},
                         "Repair" }
            );
        }
    }
    catch (const std::overflow_error&)
    {
        return tl::unexpected(
            Failure{ Status::Indeterminate, "CircleVertexCumulativeNumericUnestablished", "Finite cumulative vertex evidence required", {}, "Repair" }
        );
    }
    catch (const std::domain_error&)
    {
        return tl::unexpected(
            Failure{ Status::Indeterminate, "CircleVertexCumulativeNumericUnestablished", "Finite cumulative vertex evidence required", {}, "Repair" }
        );
    }
    for (const auto index : indices)
    {
        m_circleVerticesClaimed[index] = true;
    }
    m_circleVertexMovementUpperMm = total;
    m_circleVertexProtectionLowerMm = protection;
    m_circleVertexBudget->correctedVertices += evidence.size();
    m_circleVertexBudget->cumulativeDisplacementUpperMm = total;
    m_circleVertexBudget->minimumProtectionLowerMm = protection;
    return {};
}
[[nodiscard]] Result<void> Execution::TightenCircleVertexProtection(double lowerMm)
{
    if (m_committed || !m_circleVertexBudget || !(lowerMm > 0) || !std::isfinite(lowerMm) || m_circleVertexMovementUpperMm > Down(lowerMm / 100.))
    {
        return tl::unexpected(
            Failure{ Status::Indeterminate,
                     "CircleVertexSurfaceProtectionUnestablished",
                     "Nonparticipating surface clearance must protect cumulative original-model movement",
                     {},
                     "Repair" }
        );
    }
    m_circleVertexProtectionLowerMm = std::min(m_circleVertexProtectionLowerMm, lowerMm);
    m_circleVertexBudget->minimumProtectionLowerMm = m_circleVertexProtectionLowerMm;
    return {};
}
[[nodiscard]] Result<void> Execution::CheckRepairCompletion()
{
    if (std::find(m_circleVerticesClaimed.begin(), m_circleVerticesClaimed.end(), false) != m_circleVerticesClaimed.end())
    {
        return tl::unexpected(
            Failure{ Status::Unsupported,
                     "CircleVertexCorrectionNotApplied",
                     "The complete selected request must apply every vertex correction",
                     {},
                     "Repair" }
        );
    }
    if (m_circleVertexBudget)
    {
        m_circleVertexBudget->requestComplete = true;
    }
    if (std::find(m_repairEditsClaimed.begin(), m_repairEditsClaimed.end(), false) != m_repairEditsClaimed.end())
    {
        return tl::unexpected(
            Failure{ Status::Unsupported, "RepairEditNotApplied", "The complete selected request did not apply every explicit edit", {}, "Repair" }
        );
    }
    if (std::find(m_cylindricalBandRepairsClaimed.begin(), m_cylindricalBandRepairsClaimed.end(), false) != m_cylindricalBandRepairsClaimed.end())
    {
        return tl::unexpected(
            Failure{ Status::Unsupported,
                     "CylindricalBandRepairNotApplied",
                     "Every planned face must complete repair before publication",
                     {},
                     "Repair" }
        );
    }
    return {};
}
std::size_t Execution::RepairProofRemaining() const
{
    if (!m_options.faceOrientationRepair || m_report.repairProofProducts >= m_options.faceOrientationRepair->maximumProofProducts)
    {
        return 0;
    }
    return m_options.faceOrientationRepair->maximumProofProducts - m_report.repairProofProducts;
}
[[nodiscard]] Result<void> Execution::SpendRepairProof(std::size_t products)
{
    if (products > RepairProofRemaining())
    {
        return tl::unexpected(
            Failure{ Status::ResourceLimit, "RepairProofWorkLimit", "Request-cumulative repair proof budget exhausted", {}, "Repair" }
        );
    }
    m_report.repairProofProducts += products;
    return {};
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
[[nodiscard]] Result<void> Execution::Cancel()
{
    auto requested = ImportControl::State::Requested;
    m_control->m_state.compare_exchange_strong(requested, ImportControl::State::Cancelled);
    return tl::unexpected(Failure{ Status::Cancelled, "ImportCancelled", "Cooperative cancellation observed at a safe boundary", {}, "Execution" });
}
[[nodiscard]] Result<void> Execution::ChargeNative(Report& observation, std::size_t record, NativeCallAccounting accounting)
{
    auto checked = Check(Phase::BeforeNativeCall);
    if (!checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    if (m_report.nativeCalls >= m_options.nativeLimits.nativeCalls)
    {
        if (accounting == NativeCallAccounting::Planar)
        {
            return tl::unexpected(Failure{ Status::ResourceLimit, "NativeCallLimit", "Bounded native call allowance exhausted", record });
        }
        return tl::unexpected(
            Failure{ Status::ResourceLimit, "NativeCallLimit", "General source-to-native contract is unestablished", record, "GeneralBrep" }
        );
    }
    ++m_report.nativeCalls;
    if (&observation != &m_report)
    {
        ++observation.nativeCalls;
    }
    return {};
}
Result<void> Execution::SpendExactGeometryWork(Report& bodyReport, std::size_t units, std::size_t sourceRecord)
{
    if (auto boundary = Check(Phase::BeforeNativeCall); !boundary)
    {
        return tl::unexpected(std::move(boundary.error()));
    }
    // Exact arithmetic is request-cumulative work, not a native-call count.
    // Check overflow before either root or leaf accounting changes; real STL
    // allocation/callback exceptions retain the existing execution contract.
    if (!m_report.exactGeometryWork)
    {
        m_report.exactGeometryWork = ExactWorkObservation{ m_options.nativeLimits.exactWorkUnits, 0 };
    }
    auto& total = *m_report.exactGeometryWork;
    if (units > total.maximumUnits - total.usedUnits)
    {
        return tl::unexpected(
            Failure{ Status::ResourceLimit, "ExactGeometryWorkLimit", "Bounded exact geometry work exhausted", sourceRecord, "ExactGeometry" }
        );
    }
    total.usedUnits += units;
    if (&bodyReport != &m_report)
    {
        if (!bodyReport.exactGeometryWork)
        {
            bodyReport.exactGeometryWork = ExactWorkObservation{ total.maximumUnits, 0 };
        }
        bodyReport.exactGeometryWork->usedUnits += units;
    }
    return {};
}
[[nodiscard]] Result<void> Execution::Check(Phase phase)
{
    m_report.lastBoundary = phase;
    ++m_report.checkpoints;
    if (m_options.nativeLimits.hardMemoryBytes)
    {
        return tl::unexpected(
            Failure{ Status::Unsupported,
                     "UnsupportedHardMemoryContract",
                     "OS hard memory limits require a qualified isolated execution mode",
                     {},
                     "Execution" }
        );
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
            return tl::unexpected(Failure{ Status::BuildFailure, "ExecutionCallbackFailure", error.what(), {}, "Execution" });
        }
        catch (...)
        {
            return tl::unexpected(Failure{ Status::BuildFailure, "ExecutionCallbackFailure", "Unknown observer exception", {}, "Execution" });
        }
    }
    if (m_control->m_state.load() == ImportControl::State::Requested)
    {
        return Cancel();
    }
    if (m_options.execution.deadline && std::chrono::steady_clock::now() >= *m_options.execution.deadline)
    {
        return tl::unexpected(
            Failure{ Status::ResourceLimit, "DeadlineExceeded", "Cooperative deadline observed after a safe operation boundary", {}, "Execution" }
        );
    }
    if (m_options.nativeLimits.processPrivateBytes || m_options.execution.measureProcessMemory)
    {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS_EX memory{};
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        {
            return tl::unexpected(Failure{ Status::BuildFailure, "ProcessMemoryMeasurementFailed", "GetProcessMemoryInfo failed", {}, "Execution" });
        }
        m_report.maximumObservedPrivateBytes = std::max(
            m_report.maximumObservedPrivateBytes.value_or(0),
            static_cast<std::size_t>(memory.PrivateUsage)
        );
        m_report.processPeakCommitBytes = static_cast<std::size_t>(memory.PeakPagefileUsage);
        if (m_options.nativeLimits.processPrivateBytes && memory.PrivateUsage > *m_options.nativeLimits.processPrivateBytes)
        {
            return tl::unexpected(
                Failure{ Status::ResourceLimit,
                         "ProcessMemoryLimit",
                         "Process-wide private commit charge exceeds the cooperative byte limit",
                         {},
                         "Execution" }
            );
        }
#else
        return tl::unexpected(
            Failure{ Status::Unsupported,
                     "UnsupportedProcessMemoryObserver",
                     "This execution mode has no qualified process memory observer",
                     {},
                     "Execution" }
        );
#endif
    }
    return {};
}
[[nodiscard]] Result<void> Execution::Commit()
{
    // Final serialization is bounded but can take time/allocate. Recheck declared limits before arbitration.
    // Accepted report observations remain explicitly scoped to the pre-serialization checkpoint.
    if (m_options.execution.deadline && std::chrono::steady_clock::now() >= *m_options.execution.deadline)
    {
        return tl::unexpected(Failure{ Status::ResourceLimit, "DeadlineExceeded", "Deadline reached before publication", {}, "Execution" });
    }
#ifdef _WIN32
    if (m_options.nativeLimits.processPrivateBytes)
    {
        PROCESS_MEMORY_COUNTERS_EX memory{};
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        {
            return tl::unexpected(Failure{ Status::BuildFailure, "ProcessMemoryMeasurementFailed", "Final memory guard failed", {}, "Execution" });
        }
        if (memory.PrivateUsage > *m_options.nativeLimits.processPrivateBytes)
        {
            m_report.maximumObservedPrivateBytes = std::max(
                m_report.maximumObservedPrivateBytes.value_or(0),
                static_cast<std::size_t>(memory.PrivateUsage)
            );
            m_report.processPeakCommitBytes = static_cast<std::size_t>(memory.PeakPagefileUsage);
            return tl::unexpected(
                Failure{ Status::ResourceLimit, "ProcessMemoryLimit", "Process memory limit reached before publication", {}, "Execution" }
            );
        }
    }
#endif
    auto expected = ImportControl::State::Ready;
    if (!m_control->m_state.compare_exchange_strong(expected, ImportControl::State::Accepted))
    {
        return Cancel();
    }
    m_committed = true;
    return {};
}
} // namespace detail
} // namespace step_import
