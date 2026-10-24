// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <algorithm>
#include <charconv>
#include <stdexcept>
namespace step_import_app
{
inline bool IsTerminalDomainArgument(std::string_view flag)
{
    return flag == "--terminal-domain-source-sha256" || flag == "--terminal-domain-surface" || flag == "--terminal-domain-pcurve" ||
           flag == "--max-terminal-domain-surfaces" || flag == "--max-terminal-domain-proof-controls" || flag == "--max-terminal-domain-proof-work";
}
template <std::size_t N>
std::array<std::string, N> TerminalFields(std::string_view value)
{
    std::array<std::string, N> fields;
    std::size_t start = 0;
    for (std::size_t i = 0; i < N; ++i)
    {
        const auto end = value.find(':', start);
        if ((i + 1 < N) == (end == std::string_view::npos))
        {
            throw std::invalid_argument("Invalid terminal-domain field count");
        }
        fields[i] = value.substr(start, end == std::string_view::npos ? end : end - start);
        start = end == std::string_view::npos ? value.size() : end + 1;
    }
    return fields;
}
inline bool TerminalDomainArgument(std::string_view flag, std::string_view value, step_import::Options& options)
{
    if (!IsTerminalDomainArgument(flag))
    {
        return false;
    }
    if (!options.terminalDomainInterpretation)
    {
        options.terminalDomainInterpretation.emplace();
    }
    auto& request = *options.terminalDomainInterpretation;
    if (flag == "--terminal-domain-source-sha256")
    {
        if (!request.sourceSha256.empty())
        {
            throw std::invalid_argument("Duplicate terminal-domain source identity");
        }
        request.sourceSha256 = value;
        return true;
    }
    if (flag == "--terminal-domain-surface" || flag == "--terminal-domain-pcurve")
    {
        const auto fields = flag == "--terminal-domain-surface" ? TerminalFields<5>(value) : [&]
        {
            const auto pair = TerminalFields<2>(value);
            return std::array<std::string, 5>{ pair[0], pair[1], "", "", "" };
        }();
        auto found = std::find_if(
            request.surfaces.begin(),
            request.surfaces.end(),
            [&](const auto& p)
            {
                return p.surfaceId == fields[0];
            }
        );
        if (found == request.surfaces.end())
        {
            request.surfaces.push_back({ fields[0], {}, {}, {} });
            found = request.surfaces.end() - 1;
        }
        if (flag == "--terminal-domain-pcurve")
        {
            found->pcurveIds.push_back(fields[1]);
        }
        else
        {
            if (found->u || found->v)
            {
                throw std::invalid_argument("Duplicate terminal-domain surface range");
            }
            if ((fields[1] == "-") != (fields[2] == "-") || (fields[3] == "-") != (fields[4] == "-"))
            {
                throw std::invalid_argument("Both endpoints of an omitted parameter axis must be '-'");
            }
            if (fields[1] != "-")
            {
                found->u = std::array<std::string, 2>{ fields[1], fields[2] };
            }
            if (fields[3] != "-")
            {
                found->v = std::array<std::string, 2>{ fields[3], fields[4] };
            }
        }
        return true;
    }
    auto& amount = flag == "--max-terminal-domain-surfaces"       ? request.maximumSurfaces :
                   flag == "--max-terminal-domain-proof-controls" ? request.maximumProofControls :
                                                                    request.maximumProofWorkUnits;
    if (amount)
    {
        throw std::invalid_argument("Duplicate terminal-domain resource budget");
    }
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), amount);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !amount)
    {
        throw std::invalid_argument("Positive terminal-domain resource budget required");
    }
    return true;
}
} // namespace step_import_app
