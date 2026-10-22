// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <charconv>
#include <stdexcept>
namespace step_import_app
{
inline bool RepairArgument(std::string_view flag, std::string_view value, step_import::Options& options)
{
    if (flag != "--repair-source-sha256" && flag != "--max-face-orientation-edits" && flag != "--repair-face-orientation" &&
        flag != "--max-repair-proof-controls" && flag != "--max-repair-proof-products")
    {
        return false;
    }
    if (!options.faceOrientationRepair)
    {
        options.faceOrientationRepair.emplace();
    }
    auto& repair = *options.faceOrientationRepair;
    if (flag == "--repair-source-sha256")
    {
        if (!repair.sourceSha256.empty())
        {
            throw std::invalid_argument("Duplicate repair source identity");
        }
        repair.sourceSha256 = value;
    }
    else if (flag == "--max-face-orientation-edits" || flag == "--max-repair-proof-controls" || flag == "--max-repair-proof-products")
    {
        auto& budget = flag == "--max-face-orientation-edits" ? repair.maximumFaceEdits :
                       flag == "--max-repair-proof-controls"  ? repair.maximumProofControls :
                                                                repair.maximumProofProducts;
        if (budget)
        {
            throw std::invalid_argument("Duplicate repair edit budget");
        }
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), budget);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !budget)
        {
            throw std::invalid_argument("Explicit positive face edit budget required");
        }
    }
    else
    {
        std::array<std::string_view, 5> fields;
        std::size_t start = 0;
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            const auto end = value.find(':', start);
            if ((i + 1 < fields.size()) == (end == std::string_view::npos))
            {
                throw std::invalid_argument("Use definition:shape:face:original:effective with T/F senses");
            }
            fields[i] = value.substr(start, end == std::string_view::npos ? end : end - start);
            start = end == std::string_view::npos ? value.size() : end + 1;
        }
        if ((fields[3] != "T" && fields[3] != "F") || (fields[4] != "T" && fields[4] != "F"))
        {
            throw std::invalid_argument("Face senses must be T or F");
        }
        repair.edits.push_back({ std::string(fields[0]), std::string(fields[1]), std::string(fields[2]), fields[3] == "T", fields[4] == "T" });
    }
    return true;
}
} // namespace step_import_app
