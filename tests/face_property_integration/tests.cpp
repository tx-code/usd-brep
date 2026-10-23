// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "SmFaceIntegralSpans.h"

#include <SmApiBrep.h>
#include <SmApiGeneral.h>
#include <SmApiPrimitives.h>
#include <SmApiQueries.h>
#include <SmBrep.h>
#include <SmContext.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
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
void CheckTightSphere()
{
    // Public analytic controls for the inner integration's coarse-step abort.
    // Repeating about translated frames also exercises zero-valued moments
    // after nonzero RK stages; no importer or native-output oracle is involved.
    const double volumeTruth = 500. * std::numbers::pi / 3.;
    const double areaTruth = 100. * std::numbers::pi;
    for (const auto origin : { SmPoint3d(0, 0, 0), SmPoint3d(1, 2, 3) })
    {
        SmBrep* raw = nullptr;
        Good(SmApiCreateSphere(origin, 5, raw));
        std::unique_ptr<SmBrep> brep(raw);
        for (int repeat = 0; repeat < 2; ++repeat)
        {
            double area = 0, volume = 0, mass = 0;
            SmPoint3d centroid;
            SmVector3d moments, products;
            Good(SmApiBrepComputeMassProperties(brep.get(), 1e-8, 1., origin, area, volume, mass, centroid, moments, products));
            Require(
                std::abs(area - areaTruth) < 1e-5 && std::abs(volume - volumeTruth) < 1e-5 && std::abs(mass - volumeTruth) < 1e-5 &&
                    centroid.DistanceBetween(origin) < 1e-6,
                "tight sphere measures match independent analytic truth"
            );
            for (ULONG axis = 0; axis < 3; ++axis)
            {
                Require(
                    std::abs(moments[axis] - 10. * volumeTruth) < 1e-4 && std::abs(products[axis]) < 1e-5,
                    "tight centered sphere inertia and products"
                );
            }
            const SmPoint3d farOrigin = origin + SmVector3d(1e9, -2e9, 3e9);
            Good(SmApiBrepComputeMassProperties(brep.get(), 1e-8, 1., farOrigin, area, volume, mass, centroid, moments, products));
            Require(
                std::abs(volume - volumeTruth) < 1e-5 && centroid.DistanceBetween(origin) < 1e-6,
                "public Brep query retains analytic volume and centroid at a distant inertia origin"
            );
            const auto delta = origin - farOrigin;
            for (ULONG axis = 0; axis < 3; ++axis)
            {
                const auto a = (axis + 1) % 3, b = (axis + 2) % 3;
                const double expectedMoment = volumeTruth * (10. + delta[a] * delta[a] + delta[b] * delta[b]);
                const double expectedProduct = volumeTruth * delta[a] * delta[b];
                Require(
                    std::abs(moments[axis] - expectedMoment) < 1e-8 * std::abs(expectedMoment) &&
                        std::abs(products[axis] - expectedProduct) < 1e-8 * std::abs(expectedProduct),
                    "distant-origin inertia matches the independent parallel-axis formula"
                );
            }
        }
    }
}
void CheckNarrowNativeParameters()
{
    SmBrep* raw = nullptr;
    Good(SmApiCreatePlanarCircle(SmPoint3d(0, 0, 0), 5, raw));
    std::unique_ptr<SmBrep> brep(raw);
    SmTArray<SmFace*> faces;
    brep->GetFaces(faces);
    Require(faces.GetSize() == 1, "public disk has one face");
    SmTArray<SmEdge*> edges;
    SmTArray<SmEdgeuse*> uses;
    faces[0]->GetEdges(edges);
    Require(edges.GetSize() == 1, "disk has one boundary");
    Good(edges[0]->GetEdgeuses(uses));
    SmBSplineCurve* uv = nullptr;
    Good(uses[0]->GetOrCreateUVTrimCurve(uv));
    auto* curve = dynamic_cast<SmBSplineCurve*>(edges[0]->GetCurve());
    Require(uv && curve, "disk has a paired rational boundary");
    SmTArray<double> knots;
    const auto original = uv->GetNaturalInterval();
    for (ULONG i = 0; i < 100; ++i)
    {
        knots.Add(original.Evaluate((i + 0.5) / 100.));
    }
    Good(uv->RefineCurve(knots));
    const SmExtent1d narrow(1000., 1000. + 1e-7);
    Good(uv->ScaleKnotVector(narrow.GetMin(), narrow.GetMax()));
    Good(curve->ScaleKnotVector(narrow.GetMin(), narrow.GetMax()));
    edges[0]->SetInterval(narrow);
    SmCurve* copied = nullptr;
    Good(uv->Copy(*brep->GetContext(),copied));
    std::unique_ptr<SmCurve> ownedCopy(copied);
    auto* copiedSpline = dynamic_cast<SmBSplineCurve*>(copied);
    Require(copiedSpline,"narrow UV curve copy stays a B-spline");
    SmTArray<double> sourceKnots, copyKnots;
    Good(uv->GetKnotsAll(sourceKnots));
    Good(copiedSpline->GetKnotsAll(copyKnots));
    Require(sourceKnots.GetSize()==copyKnots.GetSize(),"copy retains all native knots");
    for (ULONG k=0;k<sourceKnots.GetSize();++k)
    {
        Require(sourceKnots[k]==copyKnots[k],"copy preserves narrow native knot values without implicit tuning");
    }
    for (const ULONG flags :
         { ULONG(SM_PPF_ALL), ULONG(SM_PPF_AREA), ULONG(SM_PPF_VOLUME), ULONG(SM_PPF_AREA_FIRST_MOMENTS), ULONG(SM_PPF_VOLUME_SECOND_MOMENTS) })
    {
        double area = -37, volume = -41;
        SmTArray<SmVector3d> moments;
        // Integrate from the unchanged knot/control data in local span
        // coordinates. A large native offset must not erase the quadrature
        // nodes or change this independently specified radius-5 disk's area.
        Require(
            faces[0]->ComputePreciseProperties(SM_OT_SAME, 1e-4, SmPoint3d(0, 0, 0), 1., 0., area, volume, moments, flags) == SM_SUCCESS,
            "narrow native spans remain integrable without changing parameters"
        );
        if (std::abs(area - (flags & SM_PPF_AREA ? 25. * std::numbers::pi : 0.)) >= 0.001 || volume != 0.)
        {
            std::fprintf(stderr,"Narrow numeric flags=%lu area=%.17g volume=%.17g original UV type=%lu\n",flags,area,volume,static_cast<unsigned long>(uv->GetType()));
            SmTArray<ULONG> loops;
            SmTArray<SmBSplineCurve*> boundaries;
            SmObjsDelete<SmBSplineCurve*> cleanup(&boundaries);
            Good(faces[0]->CreateOrientedTrimBoundary(*brep->GetContext(),TRUE,TRUE,loops,boundaries));
            for (ULONG n=0;n<boundaries.GetSize();++n)
            {
                sm_face_integrals::Partition plan;
                Good(sm_face_integrals::MakePartition(*boundaries[n],boundaries[n]->GetNaturalInterval(),plan));
                std::fprintf(stderr,"Boundary type=%lu normalized=%d degree=%lu\n",static_cast<unsigned long>(boundaries[n]->GetType()),int(plan.IsSplit()),boundaries[n]->GetDegree());
            }
        }
        Require(
            std::abs(area - (flags & SM_PPF_AREA ? 25. * std::numbers::pi : 0.)) < 0.001 && volume == 0.,
            "narrow native parameterization retains analytic area and unselected totals"
        );
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
    sm_face_integrals::LocalCurve local;
    Good(local.Prepare(*curve));
    for (ULONG i = 0; i < 2; ++i)
    {
        const auto span = plan[i];
        const double parameter = i == 0 ? span.max : span.min;
        SmVector3d values[2];
        Good(curve->Evaluate(parameter, 1, span.FromLeft(parameter), values, FALSE));
        const auto exact = i == 0 ? SmVector3d(0, 1, 0) : SmVector3d(1.5, 0, 0);
        Require((values[1] * span.Length() - exact).Length() <= 1e-12, "Rational C0 one-sided derivative");
        SmPoint3d locally[2];
        Good(local.Evaluate(span, i == 0 ? 1. : 0., locally));
        Require((locally[1] - exact).Length() <= 1e-12, "Local rational C0 derivative and Jacobian");
    }
    SmExtent1d reversedInterval;
    Good(curve->ReverseParameterization(curve->GetNaturalInterval(), reversedInterval));
    Good(sm_face_integrals::MakePartition(*curve, reversedInterval, plan));
    Require(plan.Size() == 2 && plan[0].min == 2. && plan[0].max == 4. && plan[1].max == 5., "Reversed actual knot domain");
    Good(local.Prepare(*curve));
    for (ULONG i = 0; i < 2; ++i)
    {
        const auto span = plan[i];
        const double parameter = i == 0 ? span.max : span.min;
        SmVector3d values[2];
        Good(curve->Evaluate(parameter, 1, span.FromLeft(parameter), values, FALSE));
        const auto exact = i == 0 ? SmVector3d(-1.5, 0, 0) : SmVector3d(0, -1, 0);
        Require((values[1] * span.Length() - exact).Length() <= 1e-12, "Reversed rational derivative and Jacobian");
        SmPoint3d locally[2];
        Good(local.Evaluate(span, i == 0 ? 1. : 0., locally));
        Require((locally[1] - exact).Length() <= 1e-12, "Reversed local rational derivative and Jacobian");
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
        CheckTightSphere();
        CheckNarrowNativeParameters();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
