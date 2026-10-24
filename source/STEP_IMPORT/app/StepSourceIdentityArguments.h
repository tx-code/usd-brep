// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>
namespace step_import_app
{
inline bool SourceIdentityArgument(std::string_view key, std::string_view value, step_import::Options& options)
{
    if (key != "--schema-source-sha256")
    {
        return false;
    }
    if (!options.schemaInterpretation.sourceSha256.empty() || value.size() != 64 ||
        !std::all_of(
            value.begin(),
            value.end(),
            [](char c)
            {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            }
        ))
    {
        throw std::invalid_argument("UniqueExactOriginalSchemaIdentityRequired");
    }
    // This is transport only. No path hashing or schema inference here: the
    // native source layer binds this request to its own immutable capture.
    options.schemaInterpretation.sourceSha256 = value;
    return true;
}
} // namespace step_import_app
