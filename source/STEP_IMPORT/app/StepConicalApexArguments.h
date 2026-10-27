// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <charconv>
#include <stdexcept>

namespace step_import_app
{
inline bool IsConicalApexArgument(std::string_view flag)
{
    return flag == "--apex-repair-source-sha256" || flag == "--max-apex-repair-vertices" || flag == "--max-apex-repair-controls" ||
           flag == "--max-apex-repair-proof-work" || flag == "--max-apex-repair-move-mm" || flag == "--max-apex-repair-total-move-mm" ||
           flag == "--repair-conical-apex-use";
}
inline bool ConicalApexArgument(std::string_view flag, std::string_view value, step_import::Options& options)
{
    if (!IsConicalApexArgument(flag))
    {
        return false;
    }
    if (!options.conicalApexRepair)
    {
        options.conicalApexRepair.emplace();
    }
    auto& r = *options.conicalApexRepair;
    if (flag == "--apex-repair-source-sha256")
    {
        if (!r.sourceSha256.empty())
        {
            throw std::invalid_argument("Duplicate conical apex repair source identity");
        }
        r.sourceSha256 = value;
    }
    else if (flag == "--repair-conical-apex-use")
    {
        std::array<std::string, 7> fields;
        std::size_t start = 0;
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            const auto end = value.find(':', start);
            if ((i + 1 < fields.size()) == (end == std::string_view::npos))
            {
                throw std::invalid_argument("Use definition:shape:face:bound:loop:vertex:surface");
            }
            fields[i] = value.substr(start, end == std::string_view::npos ? end : end - start);
            start = end == std::string_view::npos ? value.size() : end + 1;
        }
        r.uses.push_back({ fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], fields[6] });
    }
    else if (flag == "--max-apex-repair-move-mm" || flag == "--max-apex-repair-total-move-mm")
    {
        auto& budget = flag == "--max-apex-repair-move-mm" ? r.maximumDisplacementMm : r.maximumTotalDisplacementMm;
        if (budget.Numerator() != 0)
        {
            throw std::invalid_argument("Duplicate conical apex movement allowance");
        }
        budget = step_import::Fraction::Decimal(value);
        if (budget.Numerator() <= 0)
        {
            throw std::invalid_argument("Positive exact movement allowance in mm required");
        }
    }
    else
    {
        auto& capacity = flag == "--max-apex-repair-vertices" ? r.maximumVertices :
                         flag == "--max-apex-repair-controls" ? r.maximumProofControls :
                                                                r.maximumProofWork;
        if (capacity)
        {
            throw std::invalid_argument("Duplicate conical apex repair capacity");
        }
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), capacity);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !capacity)
        {
            throw std::invalid_argument("Positive conical apex repair capacity required");
        }
    }
    return true;
}
} // namespace step_import_app
