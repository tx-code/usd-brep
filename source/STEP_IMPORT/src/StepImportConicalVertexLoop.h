// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportNativeStatus.h"
#include "StepImportVectorMath.h"

namespace step_import::detail
{
struct ConicalVertexLoopInput
{
    Point origin, xAxis, yAxis, zAxis;
    Point circleOrigin, circleAxis, sourceRingVertex, sourcePoleVertex, ringStart, ringEnd;
    double radiusMm = 0, slope = 0, circleRadiusMm = 0;
    bool forwardInCircle = true;
};
struct ConicalVertexLoopGeometry
{
    double longitudeRadians = 0, definitionResidualMm = 0, vertexConnectionMm = 0, analyticApexHeightMm = 0;
};
// A complete coaxial circular boundary and a declared apex vertex select one
// finite cone cap. All positions are mm; slope is tan(the physical semi-angle).
// Definition checks use definitionMm; the original point's incidence uses the
// existing consistencyMm, as for source edge/vertex connections. Neither budget
// is enlarged, and the point is NEVER moved to the analytic apex. This is not
// a source error allowance or a continuous conversion certificate. The
// caller retains both source loops/vertices and must validate the actual native
// seam, region, definitions, tolerances and complete candidate after adoption.
[[nodiscard]] Result<ConicalVertexLoopGeometry> PlanConicalVertexLoop(const ConicalVertexLoopInput& input, double definitionMm, double consistencyMm);
} // namespace step_import::detail
