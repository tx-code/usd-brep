// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <stdexcept>

namespace step_import_app
{
inline bool ComplexMappingArgument(std::string_view flag, std::string_view value, step_import::Options& options)
{
    if (flag != "--complex-mapping-interpretation")
    {
        return false;
    }
    if (options.complexMappingInterpretation != step_semantics::ComplexMappingInterpretation::DeclaredOnly)
    {
        throw std::invalid_argument("Duplicate complex mapping interpretation");
    }
    if (value == "RegisteredFacetNames")
    {
        options.complexMappingInterpretation = step_semantics::ComplexMappingInterpretation::RegisteredFacetNames;
    }
    else if (value != "DeclaredOnly")
    {
        throw std::invalid_argument("Unknown complex mapping interpretation");
    }
    return true;
}
} // namespace step_import_app
