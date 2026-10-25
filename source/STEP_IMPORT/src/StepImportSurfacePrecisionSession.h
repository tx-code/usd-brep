// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"
#include "StepImportSurfacePrecision.h"

#include <map>
#include <string_view>
#include <tuple>
class SmFace;

namespace step_import::detail
{
void ValidateTargetSurfacePrecisionOptions(const Options& options);
class TargetSurfacePrecisionSession
{
public:

    // Own the request before the first callback. Mutable caller options cannot
    // enlarge a listed source use, precision or budget after execution begins.
    explicit TargetSurfacePrecisionSession(const Options& options);
    Result<void> BindSource(const step_source::SourceSnapshot& source);
    Result<std::optional<std::size_t>> Apply(
        std::string_view definition,
        std::string_view shape,
        std::string_view face,
        std::string_view sourceSurface,
        SmFace& native,
        std::size_t targetFace
    );
    Result<void> Verify(std::size_t action, SmFace& native);
    Result<void> Complete();
    std::shared_ptr<const TargetSurfacePrecisionReceipt> Evidence() const
    {
        return m_receipt;
    }

private:

    std::shared_ptr<TargetSurfacePrecisionReceipt> m_receipt;
    std::map<std::tuple<std::string, std::string, std::string>, std::size_t> m_uses;
    std::vector<bool> m_applied;
    bool m_bound = false;
};
} // namespace step_import::detail
