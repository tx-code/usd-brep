// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportCylinderSeamPlan.h"
#include "StepImportNativeData.h"

#include <SmBSplineCurve.h>
#include <SmCircle.h>
#include <SmContext.h>
#include <SmCylinder.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace step_import;
using namespace step_import::detail;
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
std::unique_ptr<SmBSplineCurve> Half(SmContext& context, double sign, double bulge)
{
    // Public radius-one, height-nine control. Positive rational weights bound
    // the entire change from a circular half by the one moved control point:
    // no sampling is used to infer the fixture's maximum displacement.
    SmTArray<SmPoint3d> points;
    for (const auto& p :
         { SmPoint3d(sign, 0, 9), SmPoint3d(sign, sign, 9), SmPoint3d(0, sign * (1 + bulge), 9), SmPoint3d(-sign, sign, 9), SmPoint3d(-sign, 0, 9) })
    {
        points.Add(p);
    }
    SmTArray<ULONG> multiplicities;
    for (auto value : { 3UL, 2UL, 3UL })
    {
        multiplicities.Add(value);
    }
    SmTArray<double> knots, weights;
    for (auto value : { 0., .5, 1. })
    {
        knots.Add(value);
    }
    for (auto value : { 1., std::sqrt(.5), 1., std::sqrt(.5), 1. })
    {
        weights.Add(value);
    }
    SmBSplineCurve* raw = nullptr;
    Require(
        SmBSplineCurve::CreateCanonical(context, 3, 2, points, SM_CF_UNSPECIFIED, multiplicities, knots, SM_KT_UNSPECIFIED, &weights, nullptr, raw) ==
                SM_SUCCESS &&
            raw,
        "public rational half"
    );
    return std::unique_ptr<SmBSplineCurve>(raw);
}
void Check(double bulge)
{
    SmContext context;
    const SmAxis2Placement frame(SmPoint3d(0, 0, 0), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0));
    SmCircle* raw = nullptr;
    Require(SmCircle::CreateCanonical(context, frame, 1., raw) == SM_SUCCESS && raw, "public anchor circle");
    std::unique_ptr<SmCircle> bottom(raw);
    SmCylinder support(frame.GetOrigin(), frame.GetXAxis(), frame.GetYAxis(), 1, 0, 360, 9, FALSE, FALSE, FALSE, &context);
    auto upper = Half(context, 1, bulge), lower = Half(context, -1, bulge);
    const std::array<Point, 3> points{ Point{ 1, 0, 0 }, Point{ 1, 0, 9 }, Point{ -1, 0, 9 } };
    const std::array<CylinderSeamCurve, 3> curves{ CylinderSeamCurve{ 0, 0, 0, bottom.get(), { 0, 1 } },
                                                   CylinderSeamCurve{ 1, 1, 2, upper.get(), { 0, 1 } },
                                                   CylinderSeamCurve{ 2, 2, 1, lower.get(), { 0, 1 } } };
    const std::array<std::vector<CylinderSeamUse>, 2> loops{ std::vector<CylinderSeamUse>{ { 0, true } },
                                                             std::vector<CylinderSeamUse>{ { 2, false }, { 1, false } } };
    std::size_t calls = 0;
    auto plan = PlanCylinderSeam(
        support,
        points,
        curves,
        loops,
        context,
        1e-9,
        .001,
        10000,
        [&]() -> Result<void>
        {
            ++calls;
            return {};
        }
    );
    if (!plan)
    {
        std::cerr << plan.error().key << '\n';
    }
    // This is planning evidence, not qualification of a complete imported
    // solid. The producer's unchanged post-consumer checks enforce actual
    // private-edge/vertex tolerance equality before this plan can be returned.
    Require(bool(plan) && plan->has_value(), "survey must preserve working tolerances through lazy UV creation");
    Require((**plan).cutVertex == 1 && (**plan).anchorVertex == 0, "public meridian joins the original two boundary vertices");
    Require((**plan).first == points[0] && (**plan).last == points[1], "planning must not move either original endpoint");
    Require(calls > 0, "survey charges its native boundaries");
    auto limited = PlanCylinderSeam(
        support,
        points,
        curves,
        loops,
        context,
        1e-9,
        .001,
        2,
        []() -> Result<void>
        {
            return {};
        }
    );
    Require(!limited && limited.error().status == Status::ResourceLimit, "capacity remains enforced before candidate allocation");
    std::size_t limitedCalls = 0;
    auto stopped = PlanCylinderSeam(
        support,
        points,
        curves,
        loops,
        context,
        1e-9,
        .001,
        10000,
        [&]() -> Result<void>
        {
            if (++limitedCalls == calls)
            {
                return tl::unexpected(Failure{ Status::Cancelled, "PublicSurveyCancellation", "Public boundary", {}, "GeneralBrep" });
            }
            return {};
        }
    );
    Require(!stopped && stopped.error().status == Status::Cancelled, "late cancellation releases the entire private survey");
}
} // namespace
int main()
{
    try
    {
        std::lock_guard lock(NativeMutex());
        Check(0);
        Check(.0008);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
