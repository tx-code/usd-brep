// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <stdexcept>

namespace step_import_app
{
inline bool SelectionArgument(std::string_view flag, std::string_view value, step_import::Options& options)
{
    if (flag != "--selection-source-sha256" && flag != "--select-definition")
    {
        return false;
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
