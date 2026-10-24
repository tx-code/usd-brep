// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"
#include "StepImportSurfaceCurveInterpretation.h"

#include <array>
#include <charconv>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace step_import_app
{
inline bool IsSurfaceCurve3dArgument(std::string_view flag)
{
    return flag == "--surface-curve-3d-source-sha256" || flag == "--surface-curve-3d-use" || flag == "--max-surface-curve-3d-uses";
}
class SurfaceCurve3dArguments
{
public:

    bool Parse(std::string_view flag, std::string_view value)
    {
        if (!IsSurfaceCurve3dArgument(flag))
        {
            return false;
        }
        if (!m_request)
        {
            m_request = std::make_shared<step_import::SurfaceCurve3dInterpretationRequest>();
        }
        if (flag == "--surface-curve-3d-source-sha256")
        {
            if (m_hashSeen)
            {
                throw std::invalid_argument("Duplicate surface-curve source identity");
            }
            m_hashSeen = true;
            m_request->sourceSha256 = value;
        }
        else if (flag == "--max-surface-curve-3d-uses")
        {
            if (m_limitSeen)
            {
                throw std::invalid_argument("Duplicate surface-curve use budget");
            }
            m_limitSeen = true;
            auto& amount = m_request->maximumUses;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), amount);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !amount)
            {
                throw std::invalid_argument("Positive surface-curve use budget required");
            }
        }
        else
        {
            // Exact source-use syntax, not name/path inference or a second
            // STEP interpretation. Preserve both LIST occurrences in order.
            // Core validation owns canonical IDs, source/master/list binding,
            // supported option combinations and complete-plan publication.
            if (value.size() > 512)
            {
                throw std::invalid_argument("Surface-curve use argument exceeds supported label capacity");
            }
            std::array<std::string_view, 8> fields;
            std::size_t count = 0, start = 0;
            while (true)
            {
                if (count == fields.size())
                {
                    throw std::invalid_argument("Too many surface-curve use fields");
                }
                const auto end = value.find(':', start);
                fields[count++] = value.substr(start, end == std::string_view::npos ? end : end - start);
                if (fields[count - 1].empty())
                {
                    throw std::invalid_argument("Empty surface-curve use field");
                }
                if (end == std::string_view::npos)
                {
                    break;
                }
                start = end + 1;
            }
            if (count != 7 && count != 8)
            {
                throw std::invalid_argument("Surface-curve use requires definition:shape:edge:surface-curve:curve3d:master:pcurve[:pcurve]");
            }
            step_import::SurfaceCurve3dInterpretationUse use;
            use.definitionId = fields[0];
            use.shapeId = fields[1];
            use.edgeId = fields[2];
            use.surfaceCurveId = fields[3];
            use.curve3dId = fields[4];
            if (fields[5] == "PCURVE_S1")
            {
                use.expectedMaster = step_import::SurfaceCurveDeclaredMaster::PcurveS1;
            }
            else if (fields[5] == "PCURVE_S2")
            {
                use.expectedMaster = step_import::SurfaceCurveDeclaredMaster::PcurveS2;
            }
            else
            {
                throw std::invalid_argument("Original PCURVE_S1 or PCURVE_S2 declaration required");
            }
            for (std::size_t i = 6; i < count; ++i)
            {
                use.pcurveIds.emplace_back(fields[i]);
            }
            m_request->uses.push_back(std::move(use));
        }
        return true;
    }
    std::shared_ptr<const step_import::SurfaceCurve3dInterpretationRequest> Request() const
    {
        // The mutable builder remains private to argument parsing. Public
        // Import entrypoints deep-copy the complete request before callbacks.
        return m_request;
    }

private:

    std::shared_ptr<step_import::SurfaceCurve3dInterpretationRequest> m_request;
    bool m_hashSeen = false, m_limitSeen = false;
};
} // namespace step_import_app
