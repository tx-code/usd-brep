// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <charconv>
#include <stdexcept>
namespace step_import_app
{
inline bool IsCylindricalBandArgument(std::string_view flag)
{
    return flag == "--band-repair-source-sha256" || flag == "--max-band-repair-faces" || flag == "--max-band-bound-edits" ||
           flag == "--max-band-proof-operations" || flag == "--repair-cylindrical-band";
}
inline bool CylindricalBandArgument(std::string_view flag, std::string_view value, step_import::Options& options)
{
    if (!IsCylindricalBandArgument(flag))
    {
        return false;
    }
    if (!options.cylindricalBandRepair)
    {
        options.cylindricalBandRepair.emplace();
    }
    auto& r = *options.cylindricalBandRepair;
    if (flag == "--band-repair-source-sha256")
    {
        if (!r.sourceSha256.empty())
        {
            throw std::invalid_argument("Duplicate band repair identity");
        }
        r.sourceSha256 = value;
    }
    else if (flag == "--repair-cylindrical-band")
    {
        std::array<std::string, 5> fields;
        std::size_t start = 0;
        for (std::size_t i = 0; i < 5; ++i)
        {
            const auto end = value.find(':', start);
            if ((i < 4) == (end == std::string_view::npos))
            {
                throw std::invalid_argument("Use definition:shape:face:bound0:bound1");
            }
            fields[i] = value.substr(start, end == std::string_view::npos ? end : end - start);
            start = end == std::string_view::npos ? value.size() : end + 1;
        }
        r.faces.push_back({ fields[0], fields[1], fields[2], { fields[3], fields[4] } });
    }
    else
    {
        auto& n = flag == "--max-band-repair-faces" ? r.maximumFaces :
                  flag == "--max-band-bound-edits"  ? r.maximumBoundEdits :
                                                      r.maximumProofOperations;
        if (n)
        {
            throw std::invalid_argument("Duplicate band repair budget");
        }
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), n);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !n)
        {
            throw std::invalid_argument("Explicit positive band repair budget required");
        }
    }
    return true;
}
} // namespace step_import_app
