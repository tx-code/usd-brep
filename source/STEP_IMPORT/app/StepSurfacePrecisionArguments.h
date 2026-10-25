// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"
#include "StepImportSurfacePrecision.h"

#include <array>
#include <charconv>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace step_import_app
{
inline bool IsTargetSurfacePrecisionArgument(std::string_view flag)
{
    return flag == "--target-surface-work-source-sha256" || flag == "--target-surface-work-resolution" || flag == "--max-target-surface-work-faces" ||
           flag == "--target-surface-work-face";
}
class TargetSurfacePrecisionArguments
{
public:

    bool Parse(std::string_view flag, std::string_view value)
    {
        if (!IsTargetSurfacePrecisionArgument(flag))
        {
            return false;
        }
        if (!m_request)
        {
            m_request = std::make_shared<step_import::TargetSurfacePrecisionRequest>();
        }
        if (flag == "--target-surface-work-source-sha256")
        {
            if (m_hashSeen)
            {
                throw std::invalid_argument("Duplicate target-surface source identity");
            }
            m_hashSeen = true;
            m_request->sourceSha256 = value;
        }
        else if (flag == "--max-target-surface-work-faces")
        {
            if (m_limitSeen)
            {
                throw std::invalid_argument("Duplicate target-surface face budget");
            }
            m_limitSeen = true;
            auto& amount = m_request->maximumFaces;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), amount);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !amount)
            {
                throw std::invalid_argument("Positive target-surface face budget required");
            }
        }
        else if (flag == "--target-surface-work-resolution")
        {
            if (m_resolutionSeen)
            {
                throw std::invalid_argument("Duplicate target-surface work resolution");
            }
            m_resolutionSeen = true;
            const auto fields = Fields<2>(value);
            // Exact model-mm fraction, without decimal conversion or reduction.
            // This sets geometry calculation resolution, never a topology
            // tolerance or a larger physical acceptance/repair allowance.
            for (std::size_t i = 0; i < fields.size(); ++i)
            {
                auto& amount = m_request->workResolutionMm[i];
                const auto parsed = std::from_chars(fields[i].data(), fields[i].data() + fields[i].size(), amount);
                if (parsed.ec != std::errc{} || parsed.ptr != fields[i].data() + fields[i].size() || amount <= 0)
                {
                    throw std::invalid_argument("Target-surface work resolution requires two positive int64 values");
                }
            }
        }
        else
        {
            const auto fields = Fields<4>(value);
            m_request->faces.push_back({ std::string(fields[0]), std::string(fields[1]), std::string(fields[2]), std::string(fields[3]) });
        }
        return true;
    }
    std::shared_ptr<const step_import::TargetSurfacePrecisionRequest> Request() const
    {
        if (m_request && (!m_hashSeen || !m_limitSeen || !m_resolutionSeen || m_request->faces.empty()))
        {
            throw std::invalid_argument("Target-surface work requires explicit source SHA256, resolution, face budget and face plan");
        }
        // Core request validation owns canonical source identities, supported
        // option combinations and face/surface binding. Import deep-copies this
        // private mutable builder before callbacks or candidate construction.
        return m_request;
    }

private:

    template <std::size_t N>
    static std::array<std::string_view, N> Fields(std::string_view value)
    {
        if (value.size() > 512)
        {
            throw std::invalid_argument("Target-surface argument exceeds supported label capacity");
        }
        std::array<std::string_view, N> result;
        std::size_t start = 0;
        for (std::size_t i = 0; i < N; ++i)
        {
            const auto end = value.find(':', start);
            if ((i + 1 < N) == (end == std::string_view::npos))
            {
                throw std::invalid_argument("Invalid target-surface field count");
            }
            result[i] = value.substr(start, end == std::string_view::npos ? end : end - start);
            if (result[i].empty())
            {
                throw std::invalid_argument("Empty target-surface field");
            }
            start = end == std::string_view::npos ? value.size() : end + 1;
        }
        return result;
    }
    std::shared_ptr<step_import::TargetSurfacePrecisionRequest> m_request;
    bool m_hashSeen = false, m_limitSeen = false, m_resolutionSeen = false;
};
} // namespace step_import_app
