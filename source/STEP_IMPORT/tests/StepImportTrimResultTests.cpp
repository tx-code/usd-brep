// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportDerivedUv.h"
#include "StepImportTrimMeasurement.h"

#include <SmContext.h>
#include <SmLine.h>
#include <SmPlane.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
} // namespace
int main()
{
    try
    {
        using namespace step_import::detail;
        SmContext context;
        SmLine edge(SmPoint3d(0, 0, 0), SmPoint3d(1, 0, 0), 3, &context);
        SmLine uv(SmPoint3d(0, .02, 0), SmPoint3d(1, .02, 0), 2, &context);
        SmPlane plane(SmPoint3d(0, 0, 0), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0), SmVector2d(1, 1), SmExtent2d(-1, -1, 2, 2), &context);
        Require(bool(CheckFinalTrimParameters(edge, uv, 0, 1)), "matching actual parameter domains return success");
        const auto measured = MeasureFinalTrim(edge, uv, plane, 0, 1);
        Require(
            measured && measured->samples == 9 && std::abs(measured->maximumMm - .02) < 1e-12,
            "returned paired-parameter observation keeps its physical displacement and sample count"
        );
        for (const auto interval : { std::pair{ .25, .75 },
                                     std::pair{ 1., 0. },
                                     std::pair{ 0., 0. },
                                     std::pair{ 0., std::numeric_limits<double>::infinity() },
                                     std::pair{ std::numeric_limits<double>::quiet_NaN(), 1. } })
        {
            const auto checked = CheckFinalTrimParameters(edge, uv, interval.first, interval.second);
            Require(
                !checked && checked.error() == TrimMeasurementError::ParameterDomain &&
                    std::string_view(TrimMeasurementErrorName(checked.error())) == "FinalTrimParameterDomain",
                "parameter refusal returns its precise numerical reason without an exception"
            );
            const auto refused = MeasureFinalTrim(edge, uv, plane, interval.first, interval.second);
            Require(
                !refused && refused.error() == TrimMeasurementError::ParameterDomain,
                "measurement propagates parameter refusal rather than inventing an observation"
            );
        }
        const auto projection = [&]()
        {
            SmLine* raw = nullptr;
            Require(
                SmLine::CreateLineSegment(context, 2, SmPoint3d(0, .02, 0), SmPoint3d(1, .02, 0), raw) == SM_SUCCESS && raw,
                "fresh unattached projection uses real native line construction"
            );
            return std::unique_ptr<SmBSplineCurve>(raw);
        };
        unsigned checkpoints = 0;
        const auto unestablished = MatchProjectedUvEndpoints(
            projection(),
            edge,
            uv,
            plane,
            SmExtent1d(.25, .75),
            .1,
            [&]() -> Result<void>
            {
                ++checkpoints;
                return {};
            }
        );
        Require(
            unestablished && unestablished->status == DerivedUvStatus::Unestablished && !unestablished->curve && !unestablished->rawPair &&
                checkpoints == 0,
            "local parameter refusal retains the pre-work derived-UV fallback without inventing evidence"
        );
        const auto cancelled = MatchProjectedUvEndpoints(
            projection(),
            edge,
            uv,
            plane,
            SmExtent1d(0, 1),
            .1,
            [&]() -> Result<void>
            {
                ++checkpoints;
                return tl::unexpected(Failure{ step_import::Status::Cancelled, "TrimCheckpointCancelled", "cancelled", {}, "Execution" });
            }
        );
        Require(
            !cancelled && cancelled.error().status == step_import::Status::Cancelled && cancelled.error().key == "TrimCheckpointCancelled" &&
                cancelled.error().stage == "Execution" && checkpoints == 1,
            "execution refusal propagates unchanged instead of becoming a local measurement fallback"
        );
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
