// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmAssertArray.h>
#include <SmAxis2Placement.h>
#include <SmContext.h>
#include <SmCylinder.h>
#include <SmEllipse.h>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
class Cylinder : public SmCylinder
{
public:

    using SmCylinder::SmCylinder;
    const SmEllipse* PolarCurve() const
    {
        return m_vPolarConverter.GetCurve();
    }
    SmStatus RebuildCache()
    {
        return RebuildPolarConverter();
    }
};
std::vector<double> Basis(SmBSplineSurface& surface)
{
    ULONG u = 0, v = 0;
    double* points = nullptr;
    Require(surface.GetControlPointsPointer(u, v, points) == SM_SUCCESS, "read NURBS coefficients");
    // Copy borrowed coefficients before rebuilding any cache. Each control
    // point stores four homogeneous coordinates, including its weight.
    std::vector<double> result{ double(u), double(v) };
    result.insert(result.end(), points, points + 4 * u * v);
    double *uk = nullptr, *vk = nullptr;
    Require(surface.GetKnotsPointers(u, v, uk, vk) == SM_SUCCESS, "read NURBS knots");
    result.push_back(double(u));
    result.push_back(double(v));
    result.insert(result.end(), uk, uk + u);
    result.insert(result.end(), vk, vk + v);
    return result;
}
void Check(double start, double end, double radius, bool swap, bool insideOut)
{
    SmContext context;
    auto cylinder = std::unique_ptr<Cylinder>(new (context) Cylinder(
        SmPoint3d(2, -3, 5),
        SmVector3d(1, 0, 0),
        SmVector3d(0, 1, 0),
        radius,
        start,
        end,
        7,
        swap ? TRUE : FALSE,
        insideOut ? TRUE : FALSE,
        FALSE,
        &context
    ));
    cylinder->GetNaturalUVDomain(); // The first lazy NURBS construction builds the polar table.
    const auto polar = cylinder->PolarCurve();
    Require(polar != nullptr, "polar curve exists after lazy construction");
    const auto interval = polar->GetSTEPInterval();
    Require(
        std::abs(interval.GetMin() - start) <= 1e-12 && std::abs(interval.GetMax() - end) <= 1e-12,
        "polar cache must preserve real endpoints near a periodic seam"
    );
    SmAssertArray reports;
    Require(cylinder->AssertValid(&reports, SM_LEVEL_2, SM_NO_WALK) && reports.GetSize() == 0, "analytic and NURBS surface parameterizations agree");
    const auto basis = Basis(*cylinder);
    Require(
        cylinder->RebuildCache() == SM_SUCCESS && Basis(*cylinder) == basis,
        "rebuilding the polar table cannot change defining NURBS coefficients, weights or knots"
    );
    // Independent cylinder equation, in mm and degrees. Conversion-table tests
    // use sampled points; they do not establish a continuous error certificate.
    for (double fraction : { 0., .2, .4567, .8, 1. })
    {
        const double angle = start + (end - start) * fraction;
        const double radians = angle * std::numbers::pi / 180;
        const double height = 2.345;
        SmPoint2d uv;
        Require(cylinder->ConvertUVFromSTEPToNURBS(SmPoint2d(angle, height), uv) == SM_SUCCESS, "STEP to NURBS conversion");
        SmPoint3d point;
        Require(cylinder->EvaluatePoint(uv, point) == SM_SUCCESS, "NURBS surface evaluation");
        const SmPoint3d truth(2 + radius * std::cos(radians), -3 + radius * std::sin(radians), 5 + height);
        Require(point.DistanceBetween(truth) < 1e-9, "polar mapping agrees with the independent analytic cylinder");
    }
    SmSurface* raw = nullptr;
    Require(cylinder->Copy(context, raw) == SM_SUCCESS && raw, "copy preserves analytic cylinder");
    std::unique_ptr<SmSurface> copy(raw);
    SmAssertArray copied;
    Require(
        copy->GetType() == SmCylinder_TYPE && copy->AssertValid(&copied, SM_LEVEL_2, SM_NO_WALK) && copied.GetSize() == 0,
        "copied polar cache remains valid"
    );
}
void CheckEllipse(double start, double end, bool insideOut)
{
    SmContext context;
    const SmAxis2Placement frame;
    const SmExtent1d interval(start, end);
    SmBoolean reverse = insideOut ? TRUE : FALSE;
    SmEllipse* raw = nullptr;
    Require(
        SmEllipse::CreateCanonical(context, frame, 3.7, 2.1, raw, &interval, &reverse) == SM_SUCCESS && raw,
        "independent elliptic arc construction"
    );
    std::unique_ptr<SmEllipse> ellipse(raw);
    ellipse->GetNaturalInterval();
    const auto declared = ellipse->GetSTEPInterval();
    Require(
        std::abs(declared.GetMin() - start) <= 1e-12 && std::abs(declared.GetMax() - end) <= 1e-12,
        "elliptic polar setup must not enlarge a real endpoint"
    );
    for (double fraction : { 0., .2, .4567, .8, 1. })
    {
        const double angle = start + (end - start) * fraction;
        const double radians = angle * std::numbers::pi / 180;
        double parameter = 0;
        Require(ellipse->ConvertTFromSTEPToNURBS(angle, parameter) == SM_SUCCESS, "ellipse polar parameter conversion");
        SmPoint3d point;
        Require(ellipse->EvaluatePoint(parameter, point) == SM_SUCCESS, "elliptic NURBS evaluation");
        const SmPoint3d truth(3.7 * std::cos(radians), 2.1 * std::sin(radians), 0);
        Require(point.DistanceBetween(truth) < 1e-9, "ellipse mapping agrees with the independent equation");
    }
}
} // namespace
int main()
{
    try
    {
        constexpr double epsilon = 1e-9;
        // Independently constructed endpoint literals are intentional: adding
        // the sweep to the first endpoint can round away the regression.
        constexpr std::array<std::array<double, 2>, 6> windows{ { { -180 - epsilon, -epsilon },
                                                                  { -epsilon, 90 + epsilon },
                                                                  { 90 - epsilon, 180 + epsilon },
                                                                  { 0, 360 },
                                                                  { -170, 10 },
                                                                  { 180 - epsilon, 270 + epsilon } } };
        for (const auto& interval : windows)
        {
            for (bool insideOut : { false, true })
            {
                CheckEllipse(interval[0], interval[1], insideOut);
            }
            for (double radius : { .7, 3.7, 10. })
            {
                for (bool swap : { false, true })
                {
                    for (bool insideOut : { false, true })
                    {
                        Check(interval[0], interval[1], radius, swap, insideOut);
                    }
                }
            }
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
