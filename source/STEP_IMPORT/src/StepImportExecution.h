// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <limits>

namespace step_import::detail
{
class SurfaceCurve3dInterpretationSession;
class TargetSurfacePrecisionSession;
enum class NativeCallAccounting
{
    General,
    Planar
};
class Execution
{
public:

    Execution(const Options& options, Report& report);
    ~Execution();
    [[nodiscard]] Result<void> Check(Phase phase);
    // Shared request budget; keep the established planar/general diagnostic
    // vocabulary while charging the root and current leaf only once.
    [[nodiscard]] Result<void> ChargeNative(
        Report& bodyReport,
        std::size_t sourceRecord,
        NativeCallAccounting accounting = NativeCallAccounting::General
    );
    [[nodiscard]] Result<void> Commit(); // Sole success/cancellation arbitration. No allocation or native operation after success.
    std::size_t NativeCalls() const
    {
        return m_report.nativeCalls;
    }
    bool IsRootReport(const Report& report) const
    {
        return &report == &m_report;
    }
    [[nodiscard]] Result<void> ClaimRepairEdits(const std::vector<std::size_t>& indices);
    [[nodiscard]] Result<void> CheckRepairCompletion();
    std::size_t RepairProofRemaining() const;
    [[nodiscard]] Result<void> SpendRepairProof(std::size_t products);
    [[nodiscard]] Result<void> ClaimCylindricalBandRepairs(const std::vector<std::size_t>& indices);
    [[nodiscard]] Result<void> SpendCylindricalBandProof();
    [[nodiscard]] Result<void> ClaimCircleVertexCorrections(
        const std::vector<std::size_t>& indices,
        const std::vector<CircleVertexCorrectionEvidence>& evidence
    );
    std::shared_ptr<const CircleVertexBudgetReceipt> CircleVertexBudget() const
    {
        return m_circleVertexBudget;
    }
    [[nodiscard]] Result<void> TightenCircleVertexProtection(double lowerMm);
    [[nodiscard]] Result<void> BindTerminalDomainSource(const step_source::SourceSnapshot& source);
    [[nodiscard]] Result<void> BindSourceInterpretations(const step_source::SourceSnapshot& source);
    SurfaceCurve3dInterpretationSession* SurfaceCurves() const
    {
        return m_surfaceCurves.get();
    }
    TargetSurfacePrecisionSession* TargetSurfaces() const
    {
        return m_targetSurfaces.get();
    }
    [[nodiscard]] Result<void> SpendTerminalDomainWork(std::size_t units = 1);
    [[nodiscard]] Result<void> SpendExactGeometryWork(Report& bodyReport, std::size_t units, std::size_t sourceRecord);
    [[nodiscard]] Result<void> RecordTerminalDomainAction(std::size_t plan, TerminalDomainInterpretationAction action);
    [[nodiscard]] Result<void> CheckTerminalDomainCompletion();
    [[nodiscard]] Result<void> ClaimTerminalDomainPcurve(const std::string& sourceId);
    std::shared_ptr<const TerminalDomainInterpretationReceipt> TerminalDomainInterpretation() const
    {
        return m_terminalDomainInterpretation;
    }

private:

    const Options& m_options;
    Report& m_report;
    std::shared_ptr<ImportControl> m_control;
    bool m_committed = false;
    std::vector<bool> m_repairEditsClaimed;
    std::vector<bool> m_cylindricalBandRepairsClaimed;
    std::vector<bool> m_circleVerticesClaimed;
    double m_circleVertexMovementUpperMm = 0;
    double m_circleVertexProtectionLowerMm = std::numeric_limits<double>::infinity();
    // Private mutation stops before any snapshot can be published. All models
    // own the final const request-wide receipt, including after report release.
    std::shared_ptr<CircleVertexBudgetReceipt> m_circleVertexBudget;
    std::shared_ptr<TerminalDomainInterpretationReceipt> m_terminalDomainInterpretation;
    std::vector<bool> m_terminalDomainPlansApplied;
    std::vector<std::string> m_terminalDomainPcurvesApplied;
    bool m_terminalDomainSourceBound = false;
    std::unique_ptr<SurfaceCurve3dInterpretationSession> m_surfaceCurves;
    std::unique_ptr<TargetSurfacePrecisionSession> m_targetSurfaces;
    [[nodiscard]] Result<void> Cancel();
};
} // namespace step_import::detail
