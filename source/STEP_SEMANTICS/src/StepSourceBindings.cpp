// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalog.h"

#include <iterator>
#include <stdexcept>

namespace step_semantics
{
std::size_t FindFieldBinding(const SemanticData& data, std::size_t record, std::string_view name, std::optional<std::string_view> declaredIn)
{
    if (!data.bindingsComplete)
    {
        throw std::logic_error("Semantic field bindings are not complete");
    }
    const auto& bound = data.fields.at(record);
    const auto matches = bound.named.equal_range(name);
    if (!declaredIn)
    {
        if (matches.first == matches.second || std::next(matches.first) != matches.second)
        {
            throw std::invalid_argument("Missing or ambiguous source field");
        }
        return matches.first->second;
    }
    std::optional<std::size_t> found;
    for (auto at = matches.first; at != matches.second; ++at)
    {
        if (bound.attributes[at->second].declaration.declaredIn == *declaredIn)
        {
            if (found)
            {
                throw std::invalid_argument("Ambiguous declared source field");
            }
            found = at->second;
        }
    }
    if (!found)
    {
        throw std::invalid_argument("Missing declared source field");
    }
    return *found;
}
} // namespace step_semantics
