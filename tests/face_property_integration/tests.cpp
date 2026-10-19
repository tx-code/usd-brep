// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "SmFaceIntegralSpans.h"

#include <SmApiBrep.h>
#include <SmApiGeneral.h>
#include <SmApiPrimitives.h>
#include <SmApiQueries.h>
#include <SmBrep.h>
#include <SmContext.h>
#include <SmFace.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace
{
constexpr SM_TYPE forwardingType = 0x7f180;
class ForwardingSpline final : public SmBSplineCurve
{
public:

    explicit ForwardingSpline(const SmBSplineCurve& curve) : SmBSplineCurve(curve)
    {
    }
    mutable bool knotsRequested = false;
    SmStatus GetKnotsAll(SmTArray<double>&) const override
    {
        knotsRequested = true;
        return SM_ERR;
    }
    SM_COMMON(ForwardingSpline, SmBSplineCurve, forwardingType);
};
SmBoolean ForwardingSpline::IsKindOf(SM_TYPE type) const
{
    return type == forwardingType || SmBSplineCurve::IsKindOf(type);
}
void ForwardingSpline::Dump() const
{
    SmBSplineCurve::Dump(FALSE);
}
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
void Good(SmStatus status)
{
    Require(status == SM_SUCCESS, "Native operation failed");
}
std::unique_ptr<SmBrep> Annulus(double outerRadius, double innerRadius)
{
    SmBrep* raw = nullptr;
    Good(SmApiCreatePlanarCircle(SmPoint3d(0, 0, 0), outerRadius, raw));
    std::unique_ptr<SmBrep> outer(raw);
    raw = nullptr;
    Good(SmApiCreatePlanarCircle(SmPoint3d(0, 0, 0), innerRadius, raw));
    std::unique_ptr<SmBrep> inner(raw);
    SmBrep* result = nullptr;
    Good(SmApiBoolean2d(outer.get(), inner.get(), SM_2D_DIFFERENCE, result));
    inner.release(); // A successful difference consumes B and returns A.
    Require(result == outer.get(), "Unexpected Boolean ownership");
    return outer;
}
void CheckAnnuli()
{
    for (const auto radii : std::array{ std::pair{ 10., 9. }, std::pair{ 30., 29. }, std::pair{ 5., 2. }, std::pair{ 1., .9 } })
    {
        auto brep = Annulus(radii.first, radii.second);
        SmTArray<SmFace*> faces;
        brep->GetFaces(faces);
        Require(faces.GetSize() == 1, "Synthetic annulus face count");
        double area = 0;
        SmPoint3d centroid;
        Good(SmApiFaceComputeCentroid(faces[0], 1e-4, area, centroid));
        const double exact = std::numbers::pi * (radii.first * radii.first - radii.second * radii.second);
        const double error = std::abs(area - exact) / exact;
        std::printf("Annulus R=%g r=%g area=%.17g relative error=%.9g\n", radii.first, radii.second, area, error);
        Require(error <= 1e-4, "Annulus area misses analytic comparison budget at the unchanged 1e-4 request");
        Require(centroid.Length() <= radii.first * 1e-4, "Annulus centroid misses analytic comparison budget");
        for (double request : { 1e-3, 1e-1 })
        {
            Good(SmApiFaceComputeCentroid(faces[0], request, area, centroid));
            Require(std::abs(area - exact) <= exact * request, "Annulus at default/loose caller accuracy");
        }
        if (radii.first == 30.)
        {
            // A rigid transform must not change the area. Keep the native
            // plane and both loop senses, including the clockwise hole.
            Good(SmApiRotate(brep.get(), SmPoint3d(0, 0, 0), SmVector3d(1, 2, 3), 37.));
            const SmPoint3d offset(1e5, -2e5, 3e5);
            Good(SmApiTranslate(brep.get(), offset));
            Good(SmApiFaceComputeCentroid(faces[0], 1e-4, area, centroid));
            Require(std::abs(area - exact) <= exact * 1e-4, "Transformed annulus area");
            Require(centroid.DistanceBetween(offset) <= radii.first * 1e-4, "Transformed annulus centroid");
        }
    }
}
std::unique_ptr<SmBSplineCurve> Corner(SmContext& context, double start, double knot, double end)
{
    SmTArray<SmPoint3d> points;
    points.Add(SmPoint3d(0, 0, 0));
    points.Add(SmPoint3d(1, 0, 0));
    points.Add(SmPoint3d(1, 1, 0));
    SmTArray<double> knots;
    knots.Add(start);
    knots.Add(knot);
    knots.Add(end);
    SmTArray<ULONG> mult;
    mult.Add(2);
    mult.Add(1);
    mult.Add(2);
    SmBSplineCurve* raw = nullptr;
    Good(SmBSplineCurve::CreateCanonical(context, 2, 1, points, SM_CF_UNSPECIFIED, mult, knots, SM_KT_UNSPECIFIED, nullptr, nullptr, raw));
    return std::unique_ptr<SmBSplineCurve>(raw);
}
void CheckPartitions()
{
    SmContext context;
    for (const auto domain : std::array{ std::array{ 0., 1., 2. }, std::array{ 1e6, 1e6 + .25, 1e6 + 2. }, std::array{ 0., 1e-10, 2. } })
    {
        auto curve = Corner(context, domain[0], domain[1], domain[2]);
        sm_face_integrals::Partition plan;
        Good(sm_face_integrals::MakePartition(*curve, curve->GetNaturalInterval(), plan));
        Require(plan.IsSplit() && plan.Size() == 2, "Actual positive knot spans retained");
        Require(plan[0].min == domain[0] && plan[0].max == domain[1] && plan[1].max == domain[2], "Native domain unchanged");
        for (ULONG i = 0; i < 2; ++i)
        {
            const auto span = plan[i];
            Require(span.Parameter(0) == span.min && span.Parameter(1) == span.max, "Normalized endpoints map exactly");
            SmVector3d values[2];
            const double endpoint = i == 0 ? span.max : span.min;
            Good(curve->Evaluate(endpoint, 1, span.FromLeft(endpoint), values, FALSE));
            const SmVector3d expected = i == 0 ? SmVector3d(1, 0, 0) : SmVector3d(0, 1, 0);
            // The native derivative has native-parameter units. The Jacobian
            // turns it into the derivative on the integrator's unit interval.
            Require((values[1] * span.Length() - expected).Length() <= 1e-12, "Current span's one-sided derivative and Jacobian");
        }
        sm_face_integrals::Partition clipped;
        const auto interval = curve->GetNaturalInterval();
        const double min = std::lerp(interval.GetMin(), domain[1], .5);
        Good(sm_face_integrals::MakePartition(*curve, SmExtent1d(min, domain[2]), clipped));
        Require(clipped.Size() == 2 && clipped[0].min == min, "Clipped actual domain retained");
    }
    for (ULONG count : { 1UL, 2UL, 3UL, 17UL, 4096UL })
    {
        constexpr double budget = 1e-5;
        const auto fraction = sm_face_integrals::SpanAccuracy(budget, count);
        Require(fraction > 0 && fraction <= budget && fraction * count <= budget, "Per-curve numerical control budget not enlarged");
    }
    auto curve = Corner(context, 0, 1, 2);
    ForwardingSpline derived(*curve);
    sm_face_integrals::Partition reused;
    Good(sm_face_integrals::MakePartition(*curve, curve->GetNaturalInterval(), reused));
    Require(reused.IsSplit(), "Base partition before derived fallback");
    Good(sm_face_integrals::MakePartition(derived, derived.GetNaturalInterval(), reused));
    Require(!reused.IsSplit() && !derived.knotsRequested, "Registered derived evaluator retains its original path");
}
void CheckOtherSurfaces()
{
    for (int kind = 0; kind < 3; ++kind)
    {
        SmBrep* raw = nullptr;
        const SmPoint3d origin(0, 0, 0);
        const auto status = kind == 0 ? SmApiCreatePlane(origin, 4, 3, raw) :
                            kind == 1 ? SmApiCreateSphere(origin, 5, raw) :
                                        SmApiCreateTorus(origin, 5, 1, raw);
        std::unique_ptr<SmBrep> brep(raw);
        Good(status);
        const double exact = kind == 0 ? 12. : kind == 1 ? 100. * std::numbers::pi : 20. * std::numbers::pi * std::numbers::pi;
        double area = 0;
        Good(SmApiBrepComputeArea(brep.get(), 1e-4, area));
        // These curved-surface controls retain the existing API suite's 1e-3
        // comparison budget. They detect periodic/polar regressions; only the
        // targeted annuli above establish the tighter 1e-4 regression here.
        Require(std::abs(area - exact) <= exact * (kind == 0 ? 1e-4 : 1e-3), "Polynomial/periodic/polar control area");
        SmTArray<SmFace*> faces;
        brep->GetFaces(faces);
        for (ULONG i = 0; i < faces.GetSize(); ++i)
        {
            double untouchedArea = -37, untouchedVolume = -41;
            SmTArray<SmVector3d> moments;
            moments.Add(SmVector3d(1, 2, 3));
            Require(
                faces[i]->ComputePreciseProperties(SM_OT_SAME, 1e-4, origin, 1, 0, untouchedArea, untouchedVolume, moments, 0) ==
                    SM_ERR_INVALID_INPUT,
                "Invalid property group rejected"
            );
            Require(
                untouchedArea == -37 && untouchedVolume == -41 && moments.GetSize() == 1 && moments[0] == SmVector3d(1, 2, 3),
                "Invalid group preserves all outputs"
            );
        }
    }
}
void CheckRationalReversal()
{
    SmContext context;
    SmTArray<SmPoint3d> points;
    for (const auto point : std::array{ SmPoint3d(0, 0, 0), SmPoint3d(1, 0, 0), SmPoint3d(1, 1, 0), SmPoint3d(2, 1, 0), SmPoint3d(2, 2, 0) })
    {
        points.Add(point);
    }
    SmTArray<double> weights, knots;
    for (double weight : { 1., .5, 1., .75, 1. })
    {
        weights.Add(weight);
    }
    for (double knot : { 2., 3., 5. })
    {
        knots.Add(knot);
    }
    SmTArray<ULONG> multiplicities;
    multiplicities.Add(3);
    multiplicities.Add(2);
    multiplicities.Add(3);
    SmBSplineCurve* raw = nullptr;
    Good(SmBSplineCurve::CreateCanonical(context, 2, 2, points, SM_CF_UNSPECIFIED, multiplicities, knots, SM_KT_UNSPECIFIED, &weights, nullptr, raw));
    std::unique_ptr<SmBSplineCurve> curve(raw);
    sm_face_integrals::Partition plan;
    Good(sm_face_integrals::MakePartition(*curve, curve->GetNaturalInterval(), plan));
    Require(plan.Size() == 2 && plan[0].max == 3., "Repeated rational knot retained once");
    for (ULONG i = 0; i < 2; ++i)
    {
        const auto span = plan[i];
        const double parameter = i == 0 ? span.max : span.min;
        SmVector3d values[2];
        Good(curve->Evaluate(parameter, 1, span.FromLeft(parameter), values, FALSE));
        const auto exact = i == 0 ? SmVector3d(0, 1, 0) : SmVector3d(1.5, 0, 0);
        Require((values[1] * span.Length() - exact).Length() <= 1e-12, "Rational C0 one-sided derivative");
    }
    SmExtent1d reversedInterval;
    Good(curve->ReverseParameterization(curve->GetNaturalInterval(), reversedInterval));
    Good(sm_face_integrals::MakePartition(*curve, reversedInterval, plan));
    Require(plan.Size() == 2 && plan[0].min == 2. && plan[0].max == 4. && plan[1].max == 5., "Reversed actual knot domain");
    for (ULONG i = 0; i < 2; ++i)
    {
        const auto span = plan[i];
        const double parameter = i == 0 ? span.max : span.min;
        SmVector3d values[2];
        Good(curve->Evaluate(parameter, 1, span.FromLeft(parameter), values, FALSE));
        const auto exact = i == 0 ? SmVector3d(-1.5, 0, 0) : SmVector3d(0, -1, 0);
        Require((values[1] * span.Length() - exact).Length() <= 1e-12, "Reversed rational derivative and Jacobian");
    }
}
} // namespace
int main()
{
    try
    {
        SmApiCreateContext();
        CheckPartitions();
        CheckRationalReversal();
        CheckAnnuli();
        CheckOtherSurfaces();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
