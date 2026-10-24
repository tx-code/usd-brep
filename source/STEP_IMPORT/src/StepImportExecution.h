// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <limits>

namespace step_import::detail
{
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
    [[nodiscard]] Result<void> Cancel();
};
} // namespace step_import::detail
