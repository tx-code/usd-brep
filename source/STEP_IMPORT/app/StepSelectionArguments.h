// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <charconv>
#include <stdexcept>

namespace step_import_app
{
struct SelectionArgumentState
{
    bool sourceIsolationPolicyDeclared = false;
};
inline void ValidateSelectionArguments(const step_import::Options& options, const SelectionArgumentState& state)
{
    if (options.sourceErrorIsolation && !state.sourceIsolationPolicyDeclared)
    {
        throw std::invalid_argument("Explicit --source-error-isolation profile required");
    }
}
inline bool IsSelectionArgument(std::string_view flag)
{
    return flag == "--selection-source-sha256" || flag == "--select-definition" || flag == "--source-error-isolation" ||
           flag == "--max-isolated-source-failures" || flag == "--max-source-influence-visits";
}
inline bool SelectionArgument(std::string_view flag, std::string_view value, step_import::Options& options, SelectionArgumentState& state)
{
    if (!IsSelectionArgument(flag))
    {
        return false;
    }
    if (flag == "--source-error-isolation" || flag == "--max-isolated-source-failures" || flag == "--max-source-influence-visits")
    {
        if (!options.sourceErrorIsolation)
        {
            options.sourceErrorIsolation.emplace();
        }
        if (flag == "--source-error-isolation")
        {
            if (value != "FaceBoundsClause")
            {
                throw std::invalid_argument("Unknown source isolation profile");
            }
            if (state.sourceIsolationPolicyDeclared)
            {
                throw std::invalid_argument("Duplicate source isolation profile");
            }
            state.sourceIsolationPolicyDeclared = true;
        }
        else
        {
            std::size_t parsed = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !parsed)
            {
                throw std::invalid_argument("Positive source isolation budget required");
            }
            auto& target = flag == "--max-isolated-source-failures" ? options.sourceErrorIsolation->maximumFailures :
                                                                      options.sourceErrorIsolation->maximumInfluenceVisits;
            if (target)
            {
                throw std::invalid_argument("Duplicate source isolation budget");
            }
            target = parsed;
        }
        return true;
    }
    if (!options.definitionSelection)
    {
        options.definitionSelection.emplace();
    }
    auto& selection = *options.definitionSelection;
    if (flag == "--selection-source-sha256")
    {
        if (!selection.sourceSha256.empty())
        {
            throw std::invalid_argument("Duplicate selection source identity");
        }
        selection.sourceSha256 = value;
    }
    else
    {
        selection.definitionIds.emplace_back(value);
    }
    return true;
}
} // namespace step_import_app
