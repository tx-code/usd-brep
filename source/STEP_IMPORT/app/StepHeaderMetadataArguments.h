// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <stdexcept>
#include <string_view>
namespace step_import_app
{
inline bool HeaderMetadataArgument(std::string_view flag, std::string_view value, step_import::Options& request)
{
    if (flag != "--preserve-unset-header-authorization")
    {
        return false;
    }
    if (value != "1" || request.headerMetadataPolicy != step_source::HeaderMetadataPolicy::Strict)
    {
        throw std::invalid_argument("Invalid or duplicate HEADER metadata policy");
    }
    request.headerMetadataPolicy = step_source::HeaderMetadataPolicy::PreserveUnsetAuthorization;
    return true;
}
} // namespace step_import_app
