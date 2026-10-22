// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportPlan.h"

namespace step_import::detail
{
class Execution
{
public:

    Execution(const Options& options, Report& report);
    ~Execution();
    void Check(Phase phase);
    void ChargeNative(Report& bodyReport, std::size_t sourceRecord); // Shared request budget; local body observations are separate.
    void Commit(); // Sole success/cancellation arbitration. No allocation or native operation after success.
    std::size_t NativeCalls() const
    {
        return m_report.nativeCalls;
    }
    bool IsRootReport(const Report& report) const
    {
        return &report == &m_report;
    }
    void ClaimRepairEdits(const std::vector<std::size_t>& indices);
    void CheckRepairCompletion();
    std::size_t RepairProofRemaining() const;
    void SpendRepairProof(std::size_t products);
    void ClaimCylindricalBandRepairs(const std::vector<std::size_t>& indices);
    void SpendCylindricalBandProof();

private:

    const Options& m_options;
    Report& m_report;
    std::shared_ptr<ImportControl> m_control;
    bool m_committed = false;
    std::vector<bool> m_repairEditsClaimed;
    std::vector<bool> m_cylindricalBandRepairsClaimed;
    void Cancel();
};
} // namespace step_import::detail
