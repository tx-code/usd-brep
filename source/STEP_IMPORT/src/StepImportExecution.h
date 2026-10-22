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

private:

    const Options& m_options;
    Report& m_report;
    std::shared_ptr<ImportControl> m_control;
    bool m_committed = false;
    void Cancel();
};
} // namespace step_import::detail
