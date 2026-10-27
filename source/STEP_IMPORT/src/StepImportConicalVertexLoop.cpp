// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportConicalVertexLoop.h"

#include <algorithm>
#include <cmath>

namespace step_import::detail
{
using namespace construction;
Result<ConicalVertexLoopGeometry> PlanConicalVertexLoop(const ConicalVertexLoopInput& in, double definitionMm, double consistencyMm)
{
    const auto fail = [](const char* key, Status status = Status::Unsupported) -> Result<ConicalVertexLoopGeometry>
    {
        return tl::unexpected(Failure{ status, key, "Conical vertex-loop normalization is unestablished", {}, "GeneralBrep" });
    };
    if (!(std::isfinite(in.radiusMm) && in.radiusMm >= 0 && std::isfinite(in.circleRadiusMm) && in.circleRadiusMm > 0 && std::isfinite(in.slope) &&
          in.slope != 0 && std::isfinite(definitionMm) && definitionMm > 0 && std::isfinite(consistencyMm) && consistencyMm > 0))
    {
        return fail("GeneralConicalVertexLoopRange");
    }
    const auto offset = Sub(in.circleOrigin, in.origin);
    const double height = Dot(offset, in.zAxis), poleHeight = -in.radiusMm / in.slope;
    const auto pole = Add(in.origin, Mul(in.zAxis, poleHeight));
    const auto radial = Sub(Sub(in.sourceRingVertex, in.origin), Mul(in.zAxis, height));
    const double expectedRadius = in.radiusMm + in.slope * height;
    const double errors[]{ Norm(Sub(offset, Mul(in.zAxis, height))),     Norm(Cross(in.circleAxis, in.zAxis)) * in.circleRadiusMm,
                           std::abs(expectedRadius - in.circleRadiusMm), Norm(Sub(in.sourceRingVertex, in.ringStart)),
                           Norm(Sub(in.sourceRingVertex, in.ringEnd)),   std::abs(Norm(radial) - in.circleRadiusMm) };
    double maximum = 0;
    for (const auto error : errors)
    {
        if (!std::isfinite(error))
        {
            return fail("GeneralConicalVertexLoopNumericEvidence", Status::Indeterminate);
        }
        maximum = std::max(maximum, error);
    }
    if (!std::isfinite(poleHeight) || expectedRadius <= 0 || maximum > definitionMm)
    {
        return fail("GeneralConicalVertexLoopSupportUnestablished");
    }
    const double connection = Norm(Sub(in.sourcePoleVertex, pole));
    if (!std::isfinite(connection) || connection > consistencyMm)
    {
        return fail("GeneralConicalVertexLoopConnectionUnestablished");
    }
    // S(U,V) has increasing longitude about z. A cap below the circular
    // boundary requires decreasing U there; a cap above requires increasing U.
    // The caller already composed FACE_BOUND / FACE.same_sense / OE / EC sense.
    // A contradictory directed region is refused, never silently reversed.
    const bool increasingLongitude = in.forwardInCircle == (Dot(in.circleAxis, in.zAxis) > 0);
    if (increasingLongitude != (poleHeight > height))
    {
        return fail("GeneralConicalVertexLoopOrientation", Status::PostcheckFailure);
    }
    return ConicalVertexLoopGeometry{ std::atan2(Dot(radial, in.yAxis), Dot(radial, in.xAxis)), maximum, connection, poleHeight };
}
} // namespace step_import::detail
