// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPeriodicDiagnosis.h"

#include <SmCurveClass.h>
#include <SmFaceProps.h>
#include <SmPointClass.h>
#include <algorithm>
#include <cmath>

namespace step_import::detail
{
bool IsPeriodicSourceVertexNearMiss(const SmFaceProps& p, std::span<SmVertex* const> sourceVertices)
{
    // Match SmCurveClassification::HasNearMisses (SmCurveClass.cpp): start/end
    // vertex gaps and coincident midpoint-edge gaps use this physical-mm floor.
    // This is an observation threshold, never an import tolerance or allowance.
    constexpr double thresholdMm = SM_EFF_ZERO * 1000;
    if (p.m_eBadMissingSeam != SM_SP_NEITHER || p.m_eBadCrossedSeam != SM_SP_NEITHER || p.m_eBadNearMissSeam != SM_SP_U ||
        p.m_bBadUnpairedNoArea_Loop != FALSE || p.m_lBadMissingPoles != 0 || !p.m_pCrvClassU || sourceVertices.empty() ||
        sourceVertices.size() > 65536 || p.m_pCrvClassU->GetSize() > 128)
    {
        return false;
    }
    bool observed = false;
    const auto vertex = [&](const SmPointClassification& point)
    {
        if (point.GetPointClass() != SM_PC_VERTEX)
        {
            return true;
        }
        const double gap = point.GetGap3d();
        if (!std::isfinite(gap) || gap < 0)
        {
            return false;
        }
        if (gap < thresholdMm)
        {
            return true;
        }
        observed = true;
        return std::find(sourceVertices.begin(), sourceVertices.end(), point.GetVertexObject()) != sourceVertices.end();
    };
    for (ULONG i = 0; i < p.m_pCrvClassU->GetSize(); ++i)
    {
        const auto& interval = p.m_pCrvClassU->GetAt(i);
        if (!vertex(interval.m_vStart) || !vertex(interval.m_vEnd))
        {
            return false;
        }
        if (interval.m_vMid.GetPointClass() == SM_PC_EDGE)
        {
            const double gap = interval.m_vMid.GetGap3d();
            if (!std::isfinite(gap) || gap < 0 || gap >= thresholdMm)
            {
                return false;
            }
        }
    }
    return observed;
}
} // namespace step_import::detail
