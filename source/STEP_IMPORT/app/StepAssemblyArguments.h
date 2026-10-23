// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAssemblyImport.h"

#include <stdexcept>
#include <string_view>

namespace step_import_app
{
struct AssemblyArguments
{
    bool enabled = false;
    step_semantics::UnreferencedPlacementPolicy placements = step_semantics::UnreferencedPlacementPolicy::Reject;
};
inline bool AssemblyArgument(std::string_view key, std::string_view value, AssemblyArguments& request)
{
    if (key == "--assembly")
    {
        if (value != "0" && value != "1")
        {
            throw std::invalid_argument("InvalidAssemblyFlag");
        }
        request.enabled = value == "1";
        return true;
    }
    if (key == "--unreferenced-placement-policy")
    {
        if (value == "Reject")
        {
            request.placements = step_semantics::UnreferencedPlacementPolicy::Reject;
        }
        else if (value == "RetainDisconnected")
        {
            request.placements = step_semantics::UnreferencedPlacementPolicy::RetainDisconnected;
        }
        else
        {
            throw std::invalid_argument("UnknownUnreferencedPlacementPolicy");
        }
        return true;
    }
    return false;
}
} // namespace step_import_app
