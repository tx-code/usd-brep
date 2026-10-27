// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <span>

namespace step_import::detail
{
class Execution;
void ValidateConicalApexOptions(const Options& options); // Invalid application configuration retains std::invalid_argument.
class ConicalApexRepairSession
{
public:

    explicit ConicalApexRepairSession(const ConicalApexRepairRequest& request);
    const ConicalApexRepairRequest& Request() const
    {
        return m_request;
    }
    std::shared_ptr<const ConicalApexRepairBudget> Budget() const
    {
        return m_budget;
    }
    [[nodiscard]] Result<void> BindSource(const step_source::SourceSnapshot& source);
    [[nodiscard]] Result<void> ValidateTargets(const step_semantics::Outcome& checked, Execution& execution);
    [[nodiscard]] Result<void> Spend(Execution& execution, std::size_t amount = 1);
    [[nodiscard]] Result<void> Claim(std::span<const std::size_t> indices, std::span<const ConicalApexRepairAction> actions);
    [[nodiscard]] Result<void> Complete();

private:

    const ConicalApexRepairRequest m_request; // Deep copy before any user callback; caller aliases cannot retarget the request.
    std::shared_ptr<ConicalApexRepairBudget> m_budget;
    std::vector<bool> m_claimed;
    bool m_bound = false, m_validated = false;
};
} // namespace step_import::detail
