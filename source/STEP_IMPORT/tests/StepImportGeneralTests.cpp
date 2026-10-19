// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportCylinderChart.h"
#include "StepImportDerivedUv.h"
#include "StepImportGeneralMeasurements.h"
#include "StepImportNativeData.h"
#include "StepImportTrimMeasurement.h"

#include <SmApiQueries.h>
#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmBrep.h>
#include <SmBrepData.h>
#include <SmContext.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <SmFaceuse.h>
#include <SmLine.h>
#include <SmPlane.h>
#include <SmRegion.h>
#include <SmShell.h>
#include <SmVertex.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <set>
#include <stdexcept>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
void Near(double actual, double expected, double tolerance, const char* message)
{
    Require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
double Dot(step_import::Point a, step_import::Point b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
void GaugeContract()
{
    using namespace step_import::detail;
    const CylinderGauge gauge{ { 2, -3, 5 }, { .8, -.6, 0 }, { 0, 0, -1 }, { .6, .8, 0 }, 3.7, .23, .4, -1.2, { .16, .18, .2, .2, .45, .45 } };
    const auto net = MakeCylinderGaugeNet(gauge);
    Require(net.pointsMm.size() == 32 && net.weights.size() == 32, "gauge control shape");
    SmContext context;
    SmTArray<SmPoint3d> cp;
    SmTArray<double> weights, uk, vk;
    SmTArray<ULONG> um, vm;
    for (const auto& p : net.pointsMm)
    {
        cp.Add(SmPoint3d(p[0], p[1], p[2]));
    }
    for (const auto w : net.weights)
    {
        Require(w > 0, "positive rational basis");
        weights.Add(w);
    }
    for (unsigned k = 0; k < 6; ++k)
    {
        uk.Add(double(k) / 4);
        um.Add(k == 0 || k == 5 ? 4 : 3);
    }
    vk.Add(0);
    vk.Add(1);
    vm.Add(2);
    vm.Add(2);
    SmBSplineSurface* raw = nullptr;
    const auto
        status = SmBSplineSurface::CreateCanonical(context, 3, 1, cp, SM_SF_UNSPECIFIED, um, vm, uk, vk, SM_KT_UNSPECIFIED, &weights, nullptr, raw);
    std::unique_ptr<SmBSplineSurface> surface(raw);
    Require(status == SM_SUCCESS && bool(surface), "native gauge construction");
    // Independent physical equations, including a rotated frame and a wider
    // terminal quarter. This detects weight/layout errors and a wrong u*W term.
    for (unsigned quarter = 0; quarter < 5; ++quarter)
    {
        for (unsigned k = 0; k <= 16; ++k)
        {
            for (unsigned j = 0; j <= 4; ++j)
            {
                const double t = double(k) / 16, u = (quarter + t) / 4, v = double(j) / 4;
                SmPoint3d point;
                Require(surface->EvaluatePoint(SmPoint2d(u, v), point) == SM_SUCCESS, "native gauge evaluation");
                const step_import::Point delta{ point.x - gauge.originMm[0], point.y - gauge.originMm[1], point.z - gauge.originMm[2] };
                const auto x = Dot(delta, gauge.x), y = Dot(delta, gauge.y), z = Dot(delta, gauge.z);
                Near(std::hypot(x, y), gauge.radiusMm, 2e-12, "exact cylinder radius");
                const auto width = (1 - t) * gauge.widthsMm[quarter] + t * gauge.widthsMm[quarter + 1];
                Near(z, gauge.lowerMm + gauge.pitchMm * u + width * v, 2e-12, "independent axial gauge equation");
            }
        }
    }
}
void FinalTrimContract()
{
    SmContext context;
    SmPlane plane(SmPoint3d(0, 0, 0), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0), SmVector2d(1, 1), SmExtent2d(-1, -1, 2, 2), &context);
    SmLine *edgeRaw = nullptr, *uvRaw = nullptr;
    Require(SmLine::CreateLineSegment(context, 3, SmPoint3d(0, 0, 0), SmPoint3d(1, 0, 0), edgeRaw) == SM_SUCCESS, "trim control edge");
    std::unique_ptr<SmLine> edge(edgeRaw);
    Require(SmLine::CreateLineSegment(context, 2, SmPoint3d(0, .02, 0), SmPoint3d(1, .02, 0), uvRaw) == SM_SUCCESS, "displaced final UV");
    std::unique_ptr<SmLine> uv(uvRaw);
    const auto observed = step_import::detail::MeasureFinalTrim(*edge, *uv, plane, 0, 1);
    Near(observed.maximumMm, .02, 1e-12, "remeasure final UV after endpoint movement");
    Require(observed.samples == 9 && observed.maximumMm > .001, "final trim exceeds independent budget");

    // A projection may leave parameter endpoints a few representable values
    // apart. The kernel accepts this roundoff; paired evaluation must still
    // measure the actual lift rather than relabeling or rescaling parameters.
    const double roundoff = 4 * std::numeric_limits<double>::epsilon();
    SmExtent1d nearDomain(-roundoff, 1 + roundoff);
    SmTArray<SmPoint3d> nearControls;
    nearControls.Add(SmPoint3d(0, 0, 0));
    nearControls.Add(SmPoint3d(1, 0, 0));
    SmTArray<ULONG> nearMultiplicities;
    nearMultiplicities.Add(2);
    nearMultiplicities.Add(2);
    SmTArray<double> nearKnots;
    nearKnots.Add(nearDomain.GetMin());
    nearKnots.Add(nearDomain.GetMax());
    SmBSplineCurve* nearRaw = nullptr;
    Require(
        SmBSplineCurve::CreateCanonical(
            context,
            2,
            1,
            nearControls,
            SM_CF_UNSPECIFIED,
            nearMultiplicities,
            nearKnots,
            SM_KT_UNSPECIFIED,
            nullptr,
            nullptr,
            nearRaw
        ) == SM_SUCCESS,
        "roundoff final UV interval"
    );
    std::unique_ptr<SmBSplineCurve> nearUv(nearRaw);
    Require(nearUv->GetNaturalInterval().GetMin() != 0, "fixture retains actual parameter roundoff");
    const auto roundoffMeasurement = step_import::detail::MeasureFinalTrim(*edge, *nearUv, plane, 0, 1);
    Require(roundoffMeasurement.samples == 9 && roundoffMeasurement.maximumMm < 1e-12, "parameter roundoff preserves analytic straight-edge lift");
    const auto displaced = step_import::detail::MeasureFinalTrim(*edge, *uv, plane, roundoff, 1 - roundoff);
    Near(displaced.maximumMm, .02, 1e-12, "parameter comparison cannot hide physical UV displacement");
    Require(displaced.maximumMm > .001, "physical trim budget remains separate from parameter roundoff");

    const auto domainRejected = [&](const SmCurve& trim, double begin, double end)
    {
        try
        {
            step_import::detail::MeasureFinalTrim(*edge, trim, plane, begin, end);
        }
        catch (const std::runtime_error& error)
        {
            return std::string_view(error.what()) == "FinalTrimParameterDomain";
        }
        return false;
    };
    Require(domainRejected(*nearUv, .25, .75), "a covering UV interval is not an equivalent parameter domain");
    Require(domainRejected(*nearUv, 1, 0), "reversed final edge interval");
    Require(domainRejected(*nearUv, 0, std::numeric_limits<double>::infinity()), "nonfinite final edge interval");
    Require(domainRejected(*nearUv, std::numeric_limits<double>::quiet_NaN(), 1), "undefined final edge interval");
    nearKnots[0] = -.25;
    nearKnots[1] = 1.25;
    SmBSplineCurve* outsideRaw = nullptr;
    Require(
        SmBSplineCurve::CreateCanonical(
            context,
            2,
            1,
            nearControls,
            SM_CF_UNSPECIFIED,
            nearMultiplicities,
            nearKnots,
            SM_KT_UNSPECIFIED,
            nullptr,
            nullptr,
            outsideRaw
        ) == SM_SUCCESS,
        "UV domain outside defining edge domain"
    );
    std::unique_ptr<SmBSplineCurve> outsideUv(outsideRaw);
    Require(domainRejected(*outsideUv, -.25, 1.25), "matching UV/edge trim range cannot exceed the defining curve domain");
}
void DerivedUvEndpointContract()
{
    SmContext context;
    SmPlane plane(SmPoint3d(0, 0, 0), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0), SmVector2d(1, 1), SmExtent2d(-1, -1, 9, 1), &context);
    const auto make = [&](ULONG dimension, bool rational = false, double y = 0)
    {
        SmTArray<SmPoint3d> points;
        for (double x : { 0., .01, .02, 8. })
        {
            points.Add(SmPoint3d(x, y, 0));
        }
        SmTArray<ULONG> mult;
        mult.Add(4);
        mult.Add(4);
        SmTArray<double> knots;
        knots.Add(0);
        knots.Add(1);
        SmBSplineCurve* raw = nullptr;
        SmTArray<double> weights;
        if (rational)
        {
            for (double w : { 2., 3., 4., 5. })
            {
                weights.Add(w);
            }
        }
        const auto status = SmBSplineCurve::CreateCanonical(
            context,
            dimension,
            3,
            points,
            SM_CF_UNSPECIFIED,
            mult,
            knots,
            SM_KT_UNSPECIFIED,
            rational ? &weights : nullptr,
            nullptr,
            raw
        );
        std::unique_ptr<SmBSplineCurve> result(raw);
        Require(status == SM_SUCCESS && result && result->GetDegree() == 3, "nonuniform straight Bezier fixture");
        return result;
    };
    auto edge = make(3), raw = make(2), fixed = make(2);
    const SmExtent1d interval(0, 1);
    const double cut = 1. / 16;
    SmPoint3d target;
    Require(edge->EvaluatePoint(cut, target) == SM_SUCCESS, "analytic endpoint target");
    const double displacement = .03 * cut + 7.97 * cut * cut * cut;
    Near(target.x, displacement, 1e-12, "independent Bernstein endpoint");
    Require(displacement > 0 && displacement < .01, "endpoint inside explicit derived-UV budget");
    // Reproduce SmFace's trim/restore-range endpoint operation. The curve's
    // image remains a straight line, but its parameter correspondence changes.
    SmExtent1d shortened(cut, 1);
    Require(fixed->Trim(shortened) == SM_SUCCESS, "legacy UV endpoint trim");
    Require(fixed->EditParameterization(interval) == SM_SUCCESS, "legacy UV range restore");
    Require(fixed->EditEndPoint(target, TRUE, nullptr, SM_EE_PROJECT_UVTRIMCURVE_VERTEX_TO_SURFACE, FALSE) == SM_SUCCESS, "legacy fixed UV endpoint");
    const auto warped = step_import::detail::MeasureFinalTrim(*edge, *fixed, plane, 0, 1);
    Require(warped.maximumMm > .01, "restoring UV range does not restore paired parameters");
    unsigned charges = 0;
    auto rebuilt = step_import::detail::MatchProjectedUvEndpoints(
        std::move(raw),
        *edge,
        *fixed,
        plane,
        interval,
        .01,
        [&]
        {
            ++charges;
        }
    );
    Require(rebuilt.status == step_import::detail::DerivedUvStatus::Ready && rebuilt.curve, "paired UV candidate preserves source parameters");
    Require(rebuilt.rawPair && rebuilt.rawPair->maximumMm < 1e-12 && rebuilt.matchedPair, "raw and endpoint-matched evidence are distinct");
    // Only Bernstein B0 changes: lifted difference = displacement*(1-t)^3.
    // Its complete interval bound is displacement, independently of sampling.
    Require(rebuilt.matchedPair->maximumMm <= displacement + 1e-12, "analytic whole-interval endpoint displacement bound");
    Require(rebuilt.curve->GetNaturalInterval().AreEqual(interval, 0), "no UV parameter rescaling");
    Require(rebuilt.curve->GetOwner() == nullptr && charges > 0, "caller-owned candidate with charged work");
    for (unsigned sample = 0; sample <= 16; ++sample)
    {
        const double t = double(sample) / 16;
        SmPoint3d original, actual;
        Require(
            edge->EvaluatePoint(t, original) == SM_SUCCESS && rebuilt.curve->EvaluatePoint(t, actual) == SM_SUCCESS,
            "paired analytic curve samples"
        );
        Near(original.x, .03 * t + 7.97 * t * t * t, 1e-12, "defining curve is immutable");
        Near(actual.x - original.x, displacement * (1 - t) * (1 - t) * (1 - t), 1e-12, "matched UV retains original interior parameters");
    }
    auto strict = step_import::detail::MatchProjectedUvEndpoints(
        make(2),
        *edge,
        *fixed,
        plane,
        interval,
        displacement / 2,
        []
        {
        }
    );
    Require(
        !strict.curve && strict.status == step_import::detail::DerivedUvStatus::MatchedGapExceeded,
        "endpoint closure cannot enlarge the physical budget"
    );
    auto badProjection = step_import::detail::MatchProjectedUvEndpoints(
        make(2, false, .02),
        *edge,
        *fixed,
        plane,
        interval,
        .01,
        []
        {
        }
    );
    Require(
        !badProjection.curve && badProjection.status == step_import::detail::DerivedUvStatus::RawGapExceeded,
        "inaccurate raw projection cannot be normalized into acceptance"
    );
    for (unsigned stop = 1; stop <= 6; ++stop)
    {
        unsigned calls = 0;
        bool cancelled = false;
        try
        {
            step_import::detail::MatchProjectedUvEndpoints(
                make(2),
                *edge,
                *fixed,
                plane,
                interval,
                .01,
                [&]
                {
                    if (++calls == stop)
                    {
                        throw std::runtime_error("uv-operation-cancel");
                    }
                }
            );
        }
        catch (const std::runtime_error& error)
        {
            cancelled = std::string_view(error.what()) == "uv-operation-cancel";
        }
        Require(cancelled, "cancellation propagates from every charged matching operation");
    }
    auto rationalEdge = make(3, true), rationalFixed = make(2, true);
    Require(rationalEdge->EvaluatePoint(cut, target) == SM_SUCCESS, "rational endpoint target");
    shortened = SmExtent1d(cut, 1);
    Require(
        rationalFixed->Trim(shortened) == SM_SUCCESS && rationalFixed->EditParameterization(interval) == SM_SUCCESS,
        "rational legacy parameter warp"
    );
    auto rational = step_import::detail::MatchProjectedUvEndpoints(
        make(2, true),
        *rationalEdge,
        *rationalFixed,
        plane,
        interval,
        .01,
        []
        {
        }
    );
    Require(rational.curve && rational.status == step_import::detail::DerivedUvStatus::Ready, "rational endpoint matching");
    SmPoint3d rationalFirst, rationalLast;
    double firstWeight = 0, lastWeight = 0;
    rational.curve->GetEnds(rationalFirst, rationalLast, &firstWeight, &lastWeight);
    Near(firstWeight, 2, 0, "fresh rational start weight is retained");
    Near(lastWeight, 5, 0, "fresh rational end weight is retained");
    Require(rational.curve->EvaluatePoint(0, rationalFirst) == SM_SUCCESS, "rational matched endpoint evaluation");
    Near(rationalFirst.DistanceBetween(target), 0, 1e-12, "homogeneous endpoint uses the fresh weight, not the trimmed weight");
}
struct ReloadedNative
{
    std::unique_ptr<SmContext> context;
    std::unique_ptr<SmBrep> brep;
    std::vector<std::unique_ptr<SmCurve>> curves;
    std::vector<std::unique_ptr<SmBrep>> pointAdapters;
    std::vector<std::unique_ptr<SmSurface>> supportingSurfaces;
};
ReloadedNative ReloadNative(
    const std::filesystem::path& archive,
    const std::vector<step_import::WireframeView>& wireframes = {},
    const std::vector<step_import::SupportingGeometryView>& supporting = {}
)
{
    ReloadedNative result;
    result.context = std::make_unique<SmContext>();
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    SmObjsDelete<SmCurve*> deleteCurves(&curves);
    SmObjsDelete<SmSurface*> deleteSurfaces(&surfaces);
    SmObjsDelete<SmBrep*> deleteBreps(&breps);
    Require(
        SmBrepData::ReadPartFromFile(*result.context, archive.c_str(), curves, surfaces, trees, breps, SM_BINARY) == SM_SUCCESS,
        "native archive reload"
    );
    std::set<std::size_t> curveIndices, pointIndices, surfaceIndices;
    const auto registerPrimitives = [&](const auto& info)
    {
        for (const auto& curve : info.curves)
        {
            curveIndices.insert(curve.nativeIndex);
        }
        for (const auto& point : info.points)
        {
            pointIndices.insert(point.nativeIndex);
        }
    };
    for (const auto& view : wireframes)
    {
        registerPrimitives(view.Info());
    }
    for (const auto& view : supporting)
    {
        const auto info = view.Info();
        registerPrimitives(info);
        for (const auto& frame : info.frames)
        {
            surfaceIndices.insert(frame.nativeSurfaceIndex);
        }
        for (const auto& plane : info.planes)
        {
            surfaceIndices.insert(plane.nativeSurfaceIndex);
        }
    }
    Require(
        breps.GetSize() == 1 + pointIndices.size() && curves.GetSize() == curveIndices.size() && surfaces.GetSize() == surfaceIndices.size() &&
            trees.GetSize() == 0,
        "archive complete selected root inventory"
    );
    result.brep.reset(breps[0]);
    breps[0] = nullptr;
    for (ULONG i = 0; i < curves.GetSize(); ++i)
    {
        result.curves.emplace_back(curves[i]);
        curves[i] = nullptr;
    }
    for (ULONG i = 0; i < surfaces.GetSize(); ++i)
    {
        result.supportingSurfaces.emplace_back(surfaces[i]);
        surfaces[i] = nullptr;
    }
    for (ULONG i = 1; i < breps.GetSize(); ++i)
    {
        result.pointAdapters.emplace_back(breps[i]);
        breps[i] = nullptr;
    }
    SmAssertArray pointers, deep;
    Require(result.brep->ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "reloaded pointers");
    Require(result.brep->AssertValid(&deep, SM_LEVEL_2, SM_WALK) && deep.GetSize() == 0, "reloaded level2");
    const auto verifyPrimitives = [&](const auto& view)
    {
        const auto info = view.Info();
        for (const auto& point : info.points)
        {
            SmTArray<SmVertex*> vertices;
            auto& adapter = *result.pointAdapters.at(point.nativeIndex);
            adapter.GetVertices(vertices);
            Require(vertices.GetSize() == 1, "reloaded standalone source point");
            const auto actual = vertices[0]->GetPoint();
            Near(actual.DistanceBetween(SmPoint3d(point.pointMm[0], point.pointMm[1], point.pointMm[2])), 0, 1e-9, "archive point position");
        }
        for (std::size_t index = 0; index < info.curves.size(); ++index)
        {
            const auto& curve = info.curves[index];
            const auto& read = *result.curves.at(curve.nativeIndex);
            Require(
                read.GetNaturalInterval().GetMin() == curve.parameterDomain[0] && read.GetNaturalInterval().GetMax() == curve.parameterDomain[1],
                "reloaded linked native parameter interval"
            );
            // Additional read/evaluate observations, not a continuous error
            // proof. The production mixed writer also compares complete nets.
            for (unsigned sample = 0; sample <= 8; ++sample)
            {
                const auto parameter = std::lerp(curve.parameterDomain[0], curve.parameterDomain[1], double(sample) / 8);
                const auto expected = view.EvaluateCurve(index, parameter);
                SmPoint3d actual;
                Require(read.EvaluatePoint(parameter, actual) == SM_SUCCESS, "reloaded linked curve evaluation");
                Near(actual.DistanceBetween(SmPoint3d(expected[0], expected[1], expected[2])), 0, 1e-9, "reloaded linked curve sample");
            }
        }
    };
    for (const auto& view : wireframes)
    {
        verifyPrimitives(view);
    }
    const auto verifyFrame = [&](std::size_t index, const step_import::CoordinateFrame& frame)
    {
        const auto plane = dynamic_cast<const SmPlane*>(result.supportingSurfaces.at(index).get());
        Require(plane, "reloaded canonical plane/frame adapter type");
        const auto& position = plane->GetPosition();
        const auto compare = [&](const auto& actual, const step_import::Point& expected, double tolerance)
        {
            Near(actual.x, expected[0], tolerance, "reloaded supporting coordinate x");
            Near(actual.y, expected[1], tolerance, "reloaded supporting coordinate y");
            Near(actual.z, expected[2], tolerance, "reloaded supporting coordinate z");
        };
        compare(position.GetOriginRef(), frame.originMm, 1e-9);
        compare(position.GetXAxisRef(), frame.x, 1e-12);
        compare(position.GetYAxisRef(), frame.y, 1e-12);
        Require(plane->GetUVScale().x == 1 && plane->GetUVScale().y == 1, "reloaded plane UV units");
    };
    for (const auto& view : supporting)
    {
        verifyPrimitives(view);
        const auto info = view.Info();
        for (const auto& frame : info.frames)
        {
            verifyFrame(frame.nativeSurfaceIndex, frame.frame);
        }
        for (std::size_t i = 0; i < info.planes.size(); ++i)
        {
            const auto& plane = info.planes[i];
            verifyFrame(plane.nativeSurfaceIndex, plane.frame);
            const auto domain = result.supportingSurfaces.at(plane.nativeSurfaceIndex)->GetNaturalUVDomain();
            Require(
                domain.GetMin().x == plane.parameterDomain[0] && domain.GetMin().y == plane.parameterDomain[1] &&
                    domain.GetMax().x == plane.parameterDomain[2] && domain.GetMax().y == plane.parameterDomain[3],
                "reloaded supporting chart unchanged"
            );
            const auto expected = view.EvaluatePlane(i, 2, 3);
            SmPoint3d actual;
            Require(
                result.supportingSurfaces.at(plane.nativeSurfaceIndex)->EvaluatePoint(SmPoint2d(2, 3), actual) == SM_SUCCESS,
                "reloaded supporting plane evaluation"
            );
            Near(actual.DistanceBetween(SmPoint3d(expected[0], expected[1], expected[2])), 0, 1e-9, "supporting sample after reload");
        }
    }
    return result;
}
double VolumeWithImportPolicy(const SmBrep& brep, const std::array<step_import::Point, 2>& bounds)
{
    // Use the legacy mass query as a separate numerical observation, retaining
    // the import origin and requested relative accuracy. The fused import now
    // integrates fewer moment groups with a stricter per-face area scale, so
    // these paths need not choose identical quadrature nodes. Coefficients and
    // installed UV are compared independently below.
    step_import::Point origin;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        origin[axis] = std::midpoint(bounds[0][axis], bounds[1][axis]);
    }
    double area = 0, volume = 0, mass = 0;
    SmPoint3d centroid;
    SmVector3d moments, products;
    Require(
        SmApiBrepComputeMassProperties(&brep, 1e-4, 1, SmPoint3d(origin[0], origin[1], origin[2]), area, volume, mass, centroid, moments, products) ==
            SM_SUCCESS,
        "matching import measurement policy"
    );
    return volume;
}
void CompareCurveCoefficients(const SmCurve& a, const SmCurve& b, bool definingCurve = true)
{
    // Native archive reload may canonicalize a derived linear UV spline to SmLine;
    // allow only that lossless UV representation change, never a 3D promotion.
    const bool sameClass = a.GetType() == b.GetType() || (!definingCurve && a.GetType() == SmBSplineCurve_TYPE && b.GetType() == SmLine_TYPE);
    if (!sameClass || a.GetDegree() != b.GetDegree())
    {
        throw std::runtime_error(
            "archive curve class/degree/dim " + std::to_string(a.GetType()) + "/" + std::to_string(a.GetDegree()) + "/" + std::to_string(a.GetDim()) +
            " -> " + std::to_string(b.GetType()) + "/" + std::to_string(b.GetDegree()) + "/" + std::to_string(b.GetDim())
        );
    }
    const auto first = dynamic_cast<const SmBSplineCurve*>(&a), second = dynamic_cast<const SmBSplineCurve*>(&b);
    Require(first && second, "admitted exact native curve net");
    SmTArray<SmPoint3d> ca, cb;
    SmTArray<double> wa, wb, ka, kb;
    Require(first->GetControlPolygon(ca, wa) == SM_SUCCESS && second->GetControlPolygon(cb, wb) == SM_SUCCESS, "archive curve coefficients");
    Require(first->GetKnotsAll(ka) == SM_SUCCESS && second->GetKnotsAll(kb) == SM_SUCCESS, "archive curve knots");
    Require(ca.GetSize() == cb.GetSize() && wa.GetSize() == wb.GetSize() && ka.GetSize() == kb.GetSize(), "archive curve net shape");
    for (ULONG i = 0; i < ca.GetSize(); ++i)
    {
        Near(ca[i].DistanceBetween(cb[i]), 0, 1e-9, "archive defining curve control point");
    }
    for (ULONG i = 0; i < wa.GetSize(); ++i)
    {
        Near(wa[i], wb[i], 1e-12, "archive curve weight");
    }
    for (ULONG i = 0; i < ka.GetSize(); ++i)
    {
        Require(ka[i] == kb[i], "archive curve knot identity");
    }
}
struct ArchiveClassChanges
{
    std::size_t extrusionSurfaces = 0, planeSurfaces = 0, linearUv = 0;
};
ArchiveClassChanges CompareNativeDefinitions(SmBrep& a, SmBrep& b)
{
    // Numerical integration depends on transient caches and is not an identity
    // test. Compare the actual exact native definitions as well: classes,
    // degrees, all control coefficients/weights/knots and installed UV nets.
    SmBrepCompareResult topology;
    ArchiveClassChanges changes;
    topology.dVertexTolerance = 1e-9;
    Require(a.CompareBreps(&b, topology) == SM_SUCCESS, "archive topology comparison call");
    if (topology.lFaceCountA != topology.lFaceCountB || topology.lEdgeCountA != topology.lEdgeCountB ||
        topology.lVertexCountA != topology.lVertexCountB || topology.lCurveTypeMismatches != 0 || topology.dMaxVertexGap > 1e-9)
    {
        throw std::runtime_error(
            "archive topology/classes/vertices: surface=" + std::to_string(topology.lSurfaceTypeMismatches) +
            " curve=" + std::to_string(topology.lCurveTypeMismatches) + " vertex=" + std::to_string(topology.dMaxVertexGap)
        );
    }
    SmTArray<SmEdge*> ea, eb;
    a.GetEdges(ea);
    b.GetEdges(eb);
    for (ULONG i = 0; i < ea.GetSize(); ++i)
    {
        Require(
            ea[i]->GetInterval().GetMin() == eb[i]->GetInterval().GetMin() && ea[i]->GetInterval().GetMax() == eb[i]->GetInterval().GetMax(),
            "archive edge interval"
        );
        CompareCurveCoefficients(*ea[i]->GetCurve(), *eb[i]->GetCurve());
    }
    SmTArray<SmFace*> fa, fb;
    a.GetFaces(fa);
    b.GetFaces(fb);
    for (ULONG i = 0; i < fa.GetSize(); ++i)
    {
        const auto first = dynamic_cast<SmBSplineSurface*>(fa[i]->GetSurface());
        const auto second = dynamic_cast<SmBSplineSurface*>(fb[i]->GetSurface());
        if (first && second && first->GetType() != second->GetType())
        {
            // The reader recognizes exact extrusion/plane subclasses. Class
            // labels can differ only if EVERY coefficient, weight, degree and
            // knot below remains identical, preserving evaluator parameters.
            Require(
                first->GetType() == SmBSplineSurface_TYPE && (second->GetType() == SmSurfOfExtrusion_TYPE || second->GetType() == SmPlane_TYPE),
                "unqualified archive surface reclassification"
            );
            if (second->GetType() == SmPlane_TYPE)
            {
                ++changes.planeSurfaces;
            }
            else
            {
                ++changes.extrusionSurfaces;
            }
        }
        Require(first && second, "admitted exact native surface net");
        Require(
            first->GetDegree(SM_SP_U) == second->GetDegree(SM_SP_U) && first->GetDegree(SM_SP_V) == second->GetDegree(SM_SP_V),
            "archive surface degrees"
        );
        ULONG au = 0, av = 0, bu = 0, bv = 0;
        double *ca = nullptr, *cb = nullptr;
        Require(
            first->GetControlPointsPointer(au, av, ca) == SM_SUCCESS && second->GetControlPointsPointer(bu, bv, cb) == SM_SUCCESS && au == bu &&
                av == bv,
            "archive surface net shape"
        );
        for (ULONG k = 0; k < au * av * 4; ++k)
        {
            if (ca[k] != cb[k])
            {
                Near(ca[k], cb[k], k % 4 == 3 ? 1e-12 : 1e-9, "archive surface coefficient");
            }
        }
        double *ka = nullptr, *la = nullptr, *kb = nullptr, *lb = nullptr;
        Require(
            first->GetKnotsPointers(au, av, ka, la) == SM_SUCCESS && second->GetKnotsPointers(bu, bv, kb, lb) == SM_SUCCESS && au == bu && av == bv,
            "archive surface knots shape"
        );
        for (ULONG k = 0; k < au; ++k)
        {
            Require(ka[k] == kb[k], "archive surface U knot identity");
        }
        for (ULONG k = 0; k < av; ++k)
        {
            Require(la[k] == lb[k], "archive surface V knot identity");
        }
        SmTArray<SmEdgeuse*> ua, ub;
        fa[i]->GetUpwardEdgeuses(ua);
        fb[i]->GetUpwardEdgeuses(ub);
        Require(ua.GetSize() == ub.GetSize(), "archive UV inventory");
        for (ULONG k = 0; k < ua.GetSize(); ++k)
        {
            Require(ua[k]->GetUVTrimCurve() && ub[k]->GetUVTrimCurve(), "archive UV present");
            CompareCurveCoefficients(*ua[k]->GetUVTrimCurve(), *ub[k]->GetUVTrimCurve(), false);
            changes.linearUv += ua[k]->GetUVTrimCurve()->GetType() != ub[k]->GetUVTrimCurve()->GetType();
        }
    }
    return changes;
}
void RationalContract(const std::filesystem::path& source, const std::filesystem::path& directory)
{
    std::ifstream input(source, std::ios::binary);
    std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    const auto replace = [&](std::string_view old, std::string_view value)
    {
        const auto position = text.find(old);
        Require(position != std::string::npos, "independent rational fixture carrier");
        text.replace(position, old.size(), value);
    };
    replace(
        "AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}",
        "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}"
    );
    replace("#200=DIRECTION('',(1.,0.,0.));\n", "");
    replace("#201=VECTOR('',#200,10.);\n", "");
    replace(
        "LINE('',#100,#201)",
        "(BOUNDED_CURVE() B_SPLINE_CURVE(1,(#100,#101),.POLYLINE_FORM.,.F.,.F.) "
        "B_SPLINE_CURVE_WITH_KNOTS((2,2),(0.,1.),.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() "
        "RATIONAL_B_SPLINE_CURVE((2.,5.)) REPRESENTATION_ITEM('weighted line'))"
    );
    replace(
        "PLANE('',#513)",
        "(BOUNDED_SURFACE() B_SPLINE_SURFACE(1,1,((#104,#107),(#105,#106)),.PLANE_SURF.,.F.,.F.,.F.) "
        "B_SPLINE_SURFACE_WITH_KNOTS((2,2),(2,2),(0.,1.),(0.,1.),.UNSPECIFIED.) GEOMETRIC_REPRESENTATION_ITEM() "
        "RATIONAL_B_SPLINE_SURFACE(((1.25,1.25),(1.25,1.25))) REPRESENTATION_ITEM('weighted plane') SURFACE())"
    );
    replace("'body',(#21),#12", "'body',(#21,#513),#12");
    auto options = step_import::Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = step_import::Quality::GeneralDevelopment();
    const auto result = step_import::ImportBytes(text, options);
    Require(result.status == step_import::Status::Accepted && result.model, "weighted analytic box import");
    Near(*result.model->MaterialVolumeMm3(), 6000, 1.2, "independent weighted box physical volume");
    bool surfaceChecked = false;
    for (const auto& face : result.model->Faces())
    {
        if (face.SourceId() == "517")
        {
            const auto p = face.EvaluateSurface(0.5, 0.5);
            Near(p[0], 7, 1e-9, "uniform weights cancel without reparameterizing U");
            Near(p[1], 7, 1e-9, "uniform weights cancel without reparameterizing V");
            Near(p[2], 35, 1e-9, "source Euclidean surface controls");
            surfaceChecked = true;
        }
    }
    Require(surfaceChecked, "source rational face association");
    Require(
        std::any_of(
            result.report.parameterMappings.begin(),
            result.report.parameterMappings.end(),
            [](const auto& mapping)
            {
                return mapping.kind == "ExactUniformWeightCancellation" && mapping.sourceGeometryId == "514" && mapping.sourceUseId == "517";
            }
        ),
        "exact weight cancellation provenance"
    );
    auto copy = result.model->CopyForModeling();
    Require(copy.model && copy.status == step_import::Status::Accepted, "rational independent copy");
    copy.model->Edit(
        [&](SmBrep& brep)
        {
            SmTArray<SmEdge*> edges;
            brep.GetEdges(edges);
            bool found = false;
            for (ULONG i = 0; i < edges.GetSize(); ++i)
            {
                auto curve = dynamic_cast<SmBSplineCurve*>(edges[i]->GetCurve());
                if (!curve || !curve->IsRational())
                {
                    continue;
                }
                SmTArray<SmPoint3d> controls;
                SmTArray<double> weights;
                Require(curve->GetControlPolygon(controls, weights) == SM_SUCCESS && weights.GetSize() == 2, "native source weights");
                Require(weights[0] == 2 && weights[1] == 5, "nonuniform weights must not become ones");
                SmPoint3d point;
                Require(curve->EvaluatePoint(0.5, point) == SM_SUCCESS, "rational evaluator");
                Near(point.x, 64. / 7., 1e-9, "independent weighted midpoint (not affine midpoint)");
                Near(point.y, -3, 1e-9, "weighted Euclidean ordinate");
                found = true;
            }
            Require(found, "rational edge survives native topology construction");
        }
    );
    std::filesystem::create_directories(directory);
    const auto archive = directory / "rational-box.smb";
    result.model->WriteNative(archive);
    auto reloaded = ReloadNative(archive);
    copy.model->Edit(
        [&](SmBrep& brep)
        {
            CompareNativeDefinitions(brep, *reloaded.brep);
        }
    );
}
void OpaqueHeaderContract(const std::filesystem::path& source)
{
    std::ifstream input(source, std::ios::binary);
    std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    const std::string schema = "AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}";
    text.replace(text.find(schema), schema.size(), "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}");
    const auto begin = text.find('\'', text.find("FILE_NAME("));
    const auto end = text.find('\'', begin + 1);
    const std::string name = "'legacy-\xc9\xa2\xc8\xc8.stp'";
    text.replace(begin, end + 1 - begin, name);
    auto options = step_import::Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = step_import::Quality::GeneralDevelopment();
    options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
    options.headerNamePolicy = step_source::HeaderNamePolicy::PreserveOpaqueBytes;
    auto outcome = step_import::ImportBytes(text, options);
    Require(outcome.status == step_import::Status::Accepted && outcome.model, "opaque header native import");
    auto sourceOwner = outcome.report.source;
    auto modelOwner = outcome.model;
    outcome = {};
    const auto& value = sourceOwner->Header()[1].parameters[0];
    Require(value.kind == step_source::ValueKind::OpaqueString && sourceOwner->Raw(value.span) == name, "owning raw HEADER name");
    Require(sourceOwner->Bytes() == text, "metadata preservation must not transcode the STEP carrier");
    Near(*modelOwner->MaterialVolumeMm3(), 6000, 1e-5, "opaque metadata leaves exact box volume");
    Require(modelOwner->CopyForModeling().model != nullptr, "opaque-source native copy after outcome release");
    auto projectionCopy = modelOwner->CopyForModeling();
    projectionCopy.model->Edit(
        [&](SmBrep& brep)
        {
            SmTArray<SmFace*> faces;
            brep.GetFaces(faces);
            Require(faces.GetSize() > 0, "projection owner test face");
            auto face = faces[0];
            SmTArray<SmEdgeuse*> uses;
            face->GetUpwardEdgeuses(uses);
            Require(uses.GetSize() > 0, "projection owner test edgeuse");
            const auto oldCurve = uses[0]->GetUVTrimCurve();
            const auto oldOwner = face->GetSurface()->GetOwner();
            bool cancelled = false;
            try
            {
                step_import::detail::RebuildDerivedUv(
                    *uses[0],
                    .001,
                    []
                    {
                        throw std::runtime_error("projection-cancel");
                    }
                );
            }
            catch (const std::runtime_error& error)
            {
                cancelled = std::string_view(error.what()) == "projection-cancel";
            }
            Require(cancelled && face->GetSurface()->GetOwner() == oldOwner, "cancelled projection restores surface association");
            Require(uses[0]->GetUVTrimCurve() == oldCurve, "cancelled projection leaves installed trim untouched");
            auto candidate = step_import::detail::RebuildDerivedUv(
                *uses[0],
                .001,
                []
                {
                }
            );
            Require(candidate.status == step_import::detail::DerivedUvStatus::Ready && candidate.curve, "complete unpublished projection candidate");
            Require(
                candidate.curve->GetOwner() == nullptr && candidate.curve.get() != oldCurve,
                "new projection is independently owned until installation"
            );
            Require(
                face->GetSurface()->GetOwner() == oldOwner && uses[0]->GetUVTrimCurve() == oldCurve,
                "successful candidate generation does not install or transfer topology"
            );
        }
    );
}
void OpaqueDataContract(const std::filesystem::path& source)
{
    std::ifstream input(source, std::ios::binary);
    std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    const auto position = text.find("'P0-PART'");
    Require(position != std::string::npos, "public product identifier fixture");
    const std::string raw = "'unknown-\x80\xfe'";
    text.replace(position, std::string_view("'P0-PART'").size(), raw);
    auto options = step_import::Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = step_import::Quality::GeneralDevelopment();
    options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
    options.dataStringPolicy = step_source::DataStringPolicy::PreserveOpaqueBytes;
    auto result = step_import::ImportBytes(text, options);
    Require(result.status == step_import::Status::Accepted && result.model, "opaque DATA native import");
    Near(*result.model->MaterialVolumeMm3(), 6000, 1e-5, "unknown product text preserves analytic box");
    auto snapshot = result.report.generalSource->snapshot;
    const auto product = std::find_if(
                             snapshot->PartDefinition().roles.begin(),
                             snapshot->PartDefinition().roles.end(),
                             [](const auto& role)
                             {
                                 return role.name == "product";
                             }
    )->recordIndex;
    auto field = snapshot->Field(product, "id");
    result = {};
    Require(field.Value().kind == step_source::ValueKind::OpaqueString && field.Value().text.empty(), "text is unavailable, not decoded");
    Require(snapshot->Source().Raw(field.Value().span) == raw && snapshot->Source().Bytes() == text, "owning original DATA bytes");
    auto cancelled = options;
    cancelled.execution.control = std::make_shared<step_import::ImportControl>();
    cancelled.execution.progress = [&](step_import::Progress progress)
    {
        if (progress.phase == step_import::Phase::NativeReady)
        {
            cancelled.execution.control->RequestCancel();
        }
    };
    const auto stopped = step_import::ImportBytes(text, cancelled);
    Require(
        stopped.status == step_import::Status::Cancelled && !stopped.model && stopped.report.candidateDiscarded,
        "opaque source does not bypass final cancellation or candidate cleanup"
    );
    auto limited = options;
    limited.nativeLimits.reportBytes = 1;
    const auto failed = step_import::ImportBytes(text, limited);
    Require(
        !failed.model && failed.report.json.empty() && failed.report.candidateDiscarded,
        "opaque source cannot publish a model without its complete report"
    );
    auto invalid = options;
    invalid.sourcePolicy = step_semantics::SourcePolicy::Strict;
    bool rejected = false;
    try
    {
        step_import::ImportBytes(text, invalid);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected, "opaque DATA opt-in cannot relax strict import");
}
// Exercise the private measurement adapter against an independently reloaded
// analytic box, deliberately reversing the source/native face correspondence.
void MeasurementContract(ReloadedNative native)
{
    using namespace step_import::detail;
    ModelData model;
    model.context = std::move(native.context);
    model.brep = std::move(native.brep);
    SmTArray<SmFace*> faces;
    model.brep->GetFaces(faces);
    Require(faces.GetSize() == 6, "measurement analytic box");
    auto* up = faces[0]->GetUpwardFaceuse();
    auto* region = up->GetShell()->GetRegion();
    model.material = region->IsVoid() ? up->GetMate()->GetShell()->GetRegion() : region;
    for (ULONG i = faces.GetSize(); i > 0; --i)
    {
        NativeFaceData face;
        face.face = faces[i - 1];
        model.faces.push_back(std::move(face));
    }
    std::vector<std::size_t> queried;
    GeneralSolidIntegrals integrals;
    Require(
        IntegrateGeneralSolid(
            model,
            { 7, 7, 20 },
            [&](std::size_t index)
            {
                queried.push_back(index);
            },
            integrals
        ) == SM_SUCCESS,
        "fused analytic box integration"
    );
    Near(integrals.areaMm2, 2200, 1e-6, "independent fused box area");
    Near(integrals.volumeMm3, 6000, 1e-5, "independent fused box volume");
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        Near(integrals.centroidMm[axis], step_import::Point{ 7, 7, 20 }[axis], 1e-7, "independent fused box centroid");
    }
    Require(queried.size() == model.faces.size() && integrals.faces.size() == model.faces.size(), "one checkpoint per face");
    for (std::size_t i = 0; i < model.faces.size(); ++i)
    {
        Require(queried[i] == model.faces.size() - i - 1, "native traversal with source-order result identity");
        double area = 0;
        SmPoint3d centroid;
        Require(SmApiFaceComputeCentroid(model.faces[i].face, 1e-4, area, centroid) == SM_SUCCESS, "independent legacy face query");
        const auto& measured = integrals.faces[i];
        Near(std::abs(measured.areaMm2), area, 1e-8, "complete fused face area");
        const double expected[] = { centroid.x, centroid.y, centroid.z };
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            Near(
                measured.areaOriginMm[axis] + measured.areaFirstMomentMm3[axis] / measured.areaMm2,
                expected[axis],
                1e-8,
                "source-associated local face centroid"
            );
        }
    }
    // Interrupt every fused face boundary: no partially accumulated output may
    // escape, and no later face may run after the caller throws cancellation.
    for (std::size_t stop = 0; stop < model.faces.size(); ++stop)
    {
        GeneralSolidIntegrals untouched;
        untouched.areaMm2 = -37;
        std::size_t visited = 0;
        bool stopped = false;
        struct Stop
        {
        };
        try
        {
            IntegrateGeneralSolid(
                model,
                { 7, 7, 20 },
                [&](std::size_t)
                {
                    if (visited++ == stop)
                    {
                        throw Stop{};
                    }
                },
                untouched
            );
        }
        catch (const Stop&)
        {
            stopped = true;
        }
        Require(
            stopped && visited == stop + 1 && untouched.areaMm2 == -37 && untouched.faces.empty(),
            "cancelled fused traversal does not assign partial output"
        );
    }
}
void LifecycleContract(const std::filesystem::path& source, const std::filesystem::path& directory)
{
    auto options = step_import::Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = step_import::Quality::GeneralDevelopment();
    auto outcome = step_import::ImportFile(source, options);
    Require(outcome.status == step_import::Status::Accepted && bool(outcome.model), "owning general import");
    auto model = outcome.model;
    Near(*model->MaterialVolumeMm3(), 6000, 1e-5, "independent box volume");
    const auto closest = model->Closest({ 20, 7, 20 });
    Near(closest.distanceMm, 8, 1e-6, "independent boundary query");
    Near(closest.pointMm[0], 12, 1e-6, "independent boundary coordinate");
    Require(model->ClassifyMaterial({ 7, 7, 20 }) == step_import::MaterialPointClass::Inside, "independent interior query");
    auto copy = model->CopyForModeling();
    Require(copy.status == step_import::Status::Accepted && bool(copy.model), "independent editable copy");
    copy.model->Edit(
        [](SmBrep& brep)
        {
            double volume = 0;
            Require(SmApiBrepComputeVolume(&brep, 1e-4, volume) == SM_SUCCESS, "copy volume query");
            Near(volume, 6000, 1e-5, "copy geometry");
        }
    );
    std::filesystem::create_directories(directory);
    const auto archive = directory / "general-box.smb";
    model->WriteNative(archive);
    auto reloaded = ReloadNative(archive);
    double volume = 0;
    Require(SmApiBrepComputeVolume(reloaded.brep.get(), 1e-4, volume) == SM_SUCCESS, "reloaded volume query");
    Near(volume, 6000, 1e-5, "archive geometry preserved");
    MeasurementContract(std::move(reloaded));
    Require(!outcome.report.json.empty(), "successful import requires its complete report");
    for (const auto limit : { 3U, 30U, 80U })
    {
        auto limited = options;
        limited.nativeLimits.nativeCalls = limit;
        const auto failed = step_import::ImportFile(source, limited);
        Require(failed.status == step_import::Status::ResourceLimit && !failed.model, "private candidate resource cleanup");
    }
    // Sweep every checkpoint, including all fused face queries, without
    // freezing an implementation-specific count or order. Interrupted imports
    // retain their full failure report and never publish a partial model.
    const auto calls = outcome.report.nativeCalls;
    Require(calls > 3, "general measured calls");
    for (std::size_t boundary = 1; boundary < calls; ++boundary)
    {
        auto limited = options;
        limited.nativeLimits.nativeCalls = boundary;
        const auto failed = step_import::ImportFile(source, limited);
        Require(
            failed.status == step_import::Status::ResourceLimit && !failed.model && failed.report.nativeCalls == boundary &&
                !failed.report.json.empty(),
            "every native query is charged before execution"
        );
        auto cancelled = options;
        cancelled.execution.control = std::make_shared<step_import::ImportControl>();
        cancelled.execution.progress = [&](step_import::Progress progress)
        {
            if (progress.phase == step_import::Phase::BeforeNativeCall && progress.nativeCalls == boundary)
            {
                cancelled.execution.control->RequestCancel();
            }
        };
        const auto stopped = step_import::ImportFile(source, cancelled);
        Require(
            stopped.status == step_import::Status::Cancelled && !stopped.model && stopped.report.nativeCalls == boundary &&
                !stopped.report.json.empty(),
            "every native query has a cancellation boundary and complete failure report"
        );
        if (boundary >= calls - model->Info().faces - model->Info().edges)
        {
            Require(
                failed.report.candidateDiscarded && stopped.report.candidateDiscarded,
                "late measurement interruption discards the private candidate"
            );
        }
    }
    auto retained = outcome.model->Face();
    const auto identity = retained.SourceId();
    outcome = {};
    model.reset();
    Require(!identity.empty() && std::isfinite(retained.Closest({ 20, 7, 20 }).distanceMm), "owning face view lifetime");
}
void InterpretationContract(const std::filesystem::path& fixture)
{
    using step_semantics::SchemaInterpretation;
    std::ifstream input(fixture, std::ios::binary);
    std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    const std::string old = std::string(step_semantics::SchemaName) + " {1 0 10303 403 2 1 2}";
    const auto start = text.find(old);
    Require(start != std::string::npos, "synthetic interpretation fixture header");
    text.replace(start, old.size(), "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1 1}");
    const std::string oldClaim = "'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011";
    const auto claim = text.find(oldClaim);
    Require(claim != std::string::npos, "synthetic interpretation fixture APD");
    text.replace(claim, oldClaim.size(), "'automotive_design',2010");
    auto options = step_import::Options::PlanarFixture();
    options.generalBrep = true;
    options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
    options.quality = step_import::Quality::GeneralDevelopment();
    options.schemaInterpretation = SchemaInterpretation::UnregisteredAp214As2010;
    auto result = step_import::ImportBytes(text, options);
    Require(result.status == step_import::Status::AcceptedUnderInterpretation && bool(result.model), "distinct interpreted model publication");
    Near(*result.model->MaterialVolumeMm3(), 6000, 1e-5, "independent interpreted box volume");
    auto source = result.model->Source();
    Require(
        source->AppliedInterpretation() == options.schemaInterpretation && source->InterpretationDiagnostics().size() == 1 &&
            source->InterpretationDiagnostics()[0].source.key == "UnsupportedSchemaBinding" &&
            source->SchemaIdentity().match == step_semantics::SchemaMatch::UnregisteredVersion && source->SchemaIdentity().registryId.empty() &&
            source->Source().Bytes() == text,
        "owning assumption and original identity"
    );
    auto view = result.model->Face();
    result = {};
    source.reset();
    Require(
        view.AppliedSourceInterpretation() == options.schemaInterpretation && std::isfinite(view.Closest({ 20, 7, 20 }).distanceMm),
        "owning view retains interpretation after report/model owner release"
    );

    // Interpretation changes qualification only. Every late publication
    // boundary must still release its unpublished model on cancellation.
    for (const auto phase :
         { step_import::Phase::NativeReady, step_import::Phase::BeforeReport, step_import::Phase::ReportReady, step_import::Phase::BeforeCommit })
    {
        auto cancelled = options;
        cancelled.execution.control = std::make_shared<step_import::ImportControl>();
        cancelled.execution.progress = [&](step_import::Progress progress)
        {
            if (progress.phase == phase)
            {
                cancelled.execution.control->RequestCancel();
            }
        };
        const auto stopped = step_import::ImportBytes(text, cancelled);
        Require(
            stopped.status == step_import::Status::Cancelled && !stopped.model && stopped.report.candidateDiscarded && !stopped.report.json.empty(),
            "interpreted publication cancellation remains atomic"
        );
    }
    auto invalid = options;
    invalid.schemaInterpretation = static_cast<SchemaInterpretation>(99);
    bool rejected = false;
    try
    {
        step_import::ImportBytes("not a carrier", invalid);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected, "unknown interpretation policy rejected before capture");
}
void DevelopmentContract(
    const std::filesystem::path& source,
    const std::filesystem::path& directory,
    bool study,
    bool opaqueHeaderName,
    bool opaqueDataText,
    bool interpret,
    bool restrictSupport = false,
    bool workStudy = false
)
{
    auto options = step_import::Options::PlanarFixture();
    options.generalBrep = true;
    options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
    if (opaqueHeaderName)
    {
        options.headerNamePolicy = step_source::HeaderNamePolicy::PreserveOpaqueBytes;
    }
    if (opaqueDataText)
    {
        options.dataStringPolicy = step_source::DataStringPolicy::PreserveOpaqueBytes;
    }
    options.quality = study ? step_import::Quality::GeneralToleranceStudy() : step_import::Quality::GeneralDevelopment();
    if (workStudy)
    {
        options.quality = step_import::Quality::GeneralWorkToleranceStudy();
    }
    options.restrictUnusedSupportDomains = restrictSupport;
    options.sourceLimits = { 67108864, 1000000, 2000000, 2000000, 128, 1048576, 16777216, 268435456, 1024 };
    options.semanticLimits.dependencyVisits = 10000000;
    options.semanticLimits.preservedDiagnostics = 16384;
    options.nativeLimits.nativeCalls = 100000;
    options.nativeLimits.reportBytes = 268435456;
    if (interpret)
    {
        options.schemaInterpretation = step_semantics::SchemaInterpretation::UnregisteredAp214As2010;
    }
    auto result = step_import::ImportFile(source, options);
    std::filesystem::create_directories(directory);
    std::ofstream report(directory / "import.json", std::ios::binary);
    report << result.report.json;
    report.close();
    const bool assumed = result.model && result.model->Source()->AppliedInterpretation() != step_semantics::SchemaInterpretation::DeclaredOnly;
    Require(
        result.status == (assumed ? step_import::Status::AcceptedUnderInterpretation : step_import::Status::Accepted) && bool(result.model),
        "development native import"
    );
    const auto info = result.model->Info();
    step_import::Point outside = info.boundsMm[0];
    const double span = std::hypot(info.boundsMm[1][0] - outside[0], info.boundsMm[1][1] - outside[1], info.boundsMm[1][2] - outside[2]);
    outside[0] -= span + 1;
    const auto closest = result.model->Closest(outside);
    const double distance = std::hypot(closest.pointMm[0] - outside[0], closest.pointMm[1] - outside[1], closest.pointMm[2] - outside[2]);
    Near(closest.distanceMm, distance, 1e-6, "development whole-boundary query distance");
    Require(result.model->ClassifyMaterial(outside) == step_import::MaterialPointClass::Outside, "development exterior classification");
    const auto view = result.model->Face();
    const auto domain = view.ParameterDomain();
    const auto evaluation = view.EvaluateSurface((domain[0] + domain[2]) / 2, (domain[1] + domain[3]) / 2);
    for (const auto value : evaluation)
    {
        Require(std::isfinite(value), "development surface evaluation");
    }
    auto editable = result.model->CopyBrepForModeling();
    Require(editable.status == step_import::Status::Accepted && bool(editable.model), "development independent modeling copy");
    double copiedVolume = 0;
    editable.model->Edit(
        [&](SmBrep& brep)
        {
            copiedVolume = VolumeWithImportPolicy(brep, info.boundsMm);
        }
    );
    const auto expected = *result.model->MaterialVolumeMm3();
    // Both integrators request relative 1e-4. Their 2e-4 comparison screen is
    // not a certified error bound and must not be mistaken for a geometry
    // tolerance; copy/archive defining coefficients and UV are still checked.
    Near(copiedVolume, expected, std::max(1e-6, expected * 2e-4), "copy numerical volume agreement");
    const auto archive = directory / "model.smb";
    result.model->WriteNative(archive);
    const auto wireframes = result.model->Wireframes();
    for (const auto& wireframe : wireframes)
    {
        const auto group = wireframe.Info();
        for (const auto& point : group.points)
        {
            Near(wireframe.Closest(point.pointMm).distanceMm, 0, 1e-9, "development whole-set point query");
        }
        if (!group.curves.empty())
        {
            const auto curveDomain = group.curves[0].parameterDomain;
            const auto sample = wireframe.EvaluateCurve(0, std::midpoint(curveDomain[0], curveDomain[1]));
            Near(wireframe.Closest(sample).distanceMm, 0, 1e-5, "development whole-set curve query");
        }
    }
    const auto supporting = result.model->SupportingGeometry();
    for (const auto& supportingView : supporting)
    {
        const auto supportingInfo = supportingView.Info();
        for (std::size_t i = 0; i < supportingInfo.planes.size(); ++i)
        {
            auto point = supportingInfo.planes[i].frame.originMm;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                point[axis] += 1.25 * supportingInfo.planes[i].frame.z[axis];
            }
            Near(supportingView.ClosestPlane(i, point).distanceMm, 1.25, 1e-7, "development supporting plane query");
        }
    }
    auto loaded = ReloadNative(archive, wireframes, supporting);
    SmTArray<SmFace*> faces;
    SmTArray<SmEdge*> edges;
    SmTArray<SmVertex*> vertices;
    loaded.brep->GetFaces(faces);
    loaded.brep->GetEdges(edges);
    loaded.brep->GetVertices(vertices);
    Require(faces.GetSize() == info.faces && edges.GetSize() == info.edges && vertices.GetSize() == info.vertices, "archive topology preserved");
    const double volume = VolumeWithImportPolicy(*loaded.brep, info.boundsMm);
    // The native integrator requests relative 1e-4, not bitwise deterministic
    // quadrature after cache-free reload. Coefficient identity is checked below;
    // this separate measure comparison allows the two numerical integrations.
    Near(volume, expected, std::max(1e-6, expected * 2e-4), "archive numerical volume agreement");
    ArchiveClassChanges changes;
    editable.model->Edit(
        [&](SmBrep& copy)
        {
            changes = CompareNativeDefinitions(copy, *loaded.brep);
        }
    );
    SmPoint3d point;
    double queryDistance = 0;
    Require(
        SmApiBrepClosestPoint(loaded.brep.get(), SmPoint3d(outside[0], outside[1], outside[2]), point, queryDistance) == SM_SUCCESS,
        "reloaded boundary query"
    );
    Near(queryDistance, distance, 1e-5, "archive boundary distance preserved");
    std::ofstream checks(directory / "checks.json", std::ios::binary);
    checks << std::setprecision(17) << "{\"status\":\"Passed\"";
    if (assumed)
    {
        checks << ",\"source_interpretation\":\"UnregisteredAp214As2010\",\"source_binding_verified\":false";
    }
    checks
        << ",\"source_sha256\":\"" << result.report.sourceSha256 << "\",\"quality_id\":\"" << options.quality.id << "\",\"faces\":" << info.faces
        << ",\"edges\":" << info.edges << ",\"vertices\":" << info.vertices << ",\"volume_mm3\":" << expected
        << ",\"copied_volume_mm3\":" << copiedVolume << ",\"reloaded_volume_mm3\":" << volume << ",\"outside_distance_mm\":" << distance
        << ",\"reloaded_distance_mm\":" << queryDistance << ",\"archive_spline_to_extrusion_surfaces\":" << changes.extrusionSurfaces
        << ",\"archive_spline_to_linear_uv\":" << changes.linearUv << ",\"archive_spline_to_plane_surfaces\":" << changes.planeSurfaces
        << ",\"defining_coefficients_and_uv_checked\":true,\"native_pointer_reports\":0,\"native_level2_reports\":0,\"reloaded_pointer_reports\":0,\"reloaded_level2_reports\":0";
    if (!wireframes.empty())
    {
        checks << ",\"linked_sets\":" << wireframes.size() << ",\"linked_curves\":" << loaded.curves.size()
               << ",\"linked_points\":" << loaded.pointAdapters.size()
               << ",\"linked_archive_inventory_and_queries_checked\":true,\"linked_curve_reload_samples_per_curve\":9";
    }
    if (!supporting.empty())
    {
        std::size_t frames = 0, planes = 0;
        for (const auto& supportingView : supporting)
        {
            frames += supportingView.Info().frames.size();
            planes += supportingView.Info().planes.size();
        }
        checks << ",\"supporting_groups\":" << supporting.size() << ",\"supporting_frames\":" << frames << ",\"supporting_planes\":" << planes
               << ",\"supporting_archive_and_queries_checked\":true";
    }
    checks << "}\n";
    Require(bool(checks), "development check receipt");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        if (argc >= 5 && std::string_view(argv[1]) == "--development-file")
        {
            bool header = false, data = false, interpret = false, restrictSupport = false, workStudy = false;
            for (int i = 5; i < argc; ++i)
            {
                const std::string_view flag(argv[i]);
                Require(
                    flag == "--preserve-opaque-header-name" || flag == "--preserve-opaque-data-strings" ||
                        flag == "--interpret-unregistered-ap214-2010" || flag == "--restrict-unused-support-domains" ||
                        flag == "--work-tolerance-study",
                    "unknown explicit source flag"
                );
                if (flag == "--interpret-unregistered-ap214-2010")
                {
                    interpret = true;
                }
                else if (flag == "--restrict-unused-support-domains")
                {
                    restrictSupport = true;
                }
                else if (flag == "--work-tolerance-study")
                {
                    workStudy = true;
                }
                else
                {
                    (flag == "--preserve-opaque-header-name" ? header : data) = true;
                }
            }
            DevelopmentContract(argv[2], argv[3], std::string_view(argv[4]) == "study", header, data, interpret, restrictSupport, workStudy);
            return 0;
        }
        Require(argc == 3, "fixture and local output directory required");
        {
            auto invalid = step_import::Options::PlanarFixture();
            invalid.generalBrep = true;
            invalid.quality = step_import::Quality::GeneralWorkToleranceStudy();
            invalid.quality.maximumWorkToleranceMm = step_import::Fraction(3, 100);
            bool refused = false;
            try
            {
                step_import::ImportBytes({}, invalid);
            }
            catch (const std::invalid_argument&)
            {
                refused = true;
            }
            Require(refused, "frozen work-study identity cannot hide different budgets");
        }
        GaugeContract();
        FinalTrimContract();
        DerivedUvEndpointContract();
        RationalContract(argv[1], argv[2]);
        OpaqueHeaderContract(argv[1]);
        OpaqueDataContract(argv[1]);
        InterpretationContract(argv[1]);
        LifecycleContract(argv[1], argv[2]);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
