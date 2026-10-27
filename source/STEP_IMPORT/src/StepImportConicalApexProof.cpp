// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportConicalApexProof.h"

#include "StepImportInterval.h"
#include "StepImportNativeStatus.h"
#include "StepImportVertexProjectionProof.h"

#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmCircle.h>
#include <SmCone.h>
#include <SmCylinder.h>
#include <SmLine.h>
#include <SmPlane.h>
#include <SmSphere.h>
#include <SmTorus.h>
#include <algorithm>

namespace step_import::detail
{
namespace
{
Failure Refusal(const char* key = "ConicalApexProtectionUnestablished")
{
    return { Status::Indeterminate, key, "Explicit conical point correction evidence is unestablished", {}, "Repair" };
}
template <class T>
Point P(const T& p)
{
    return { p.x, p.y, p.z };
}
Result<Bounds> Hull(
    const SmBSplineCurve* curve,
    const SmBSplineSurface* surface,
    const ConicalApexProofLimits& limits,
    const ConicalApexProofCallbacks& cb
)
{
    if (auto call = cb.beforeNative(); !call)
    {
        return tl::unexpected(std::move(call.error()));
    }
    ULONG u = 0, v = 1;
    double* data = nullptr;
    const auto status = curve ? curve->GetControlPointsPointer(u, data) : surface->GetControlPointsPointer(u, v, data);
    if (auto native = CheckNativeStatus(status, NativeStatusOrigin::GeneralConstruction); !native)
    {
        return tl::unexpected(std::move(native.error()));
    }
    if (!u || !v || !data)
    {
        return tl::unexpected(Refusal("ConicalApexHullDefinition"));
    }
    if (u > limits.maximumControls / v)
    {
        return tl::unexpected(
            Failure{ Status::ResourceLimit, "ConicalApexHullControlLimit", "Explicit repair hull control budget exhausted", {}, "Repair" }
        );
    }
    Bounds box{ Point{ INFINITY, INFINITY, INFINITY }, Point{ -INFINITY, -INFINITY, -INFINITY } };
    const bool rational = curve ? bool(curve->IsRational()) : bool(surface->IsRational());
    for (std::size_t i = 0; i < std::size_t(u) * v; ++i)
    {
        if (auto work = cb.chargeWork(1); !work)
        {
            return tl::unexpected(std::move(work.error()));
        }
        const double weight = rational ? data[4 * i + 3] : 1.;
        if (!(std::isfinite(weight) && weight > 0))
        {
            return tl::unexpected(Refusal());
        }
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const auto value = Exact(data[4 * i + axis]) / Exact(weight);
            box[0][axis] = std::min(box[0][axis], value.low);
            box[1][axis] = std::max(box[1][axis], value.high);
        }
    }
    return box;
}
Result<double> CurveLower(const Point& point, const SmCurve& curve, const ConicalApexProofLimits& limits, const ConicalApexProofCallbacks& cb)
{
    if (const auto line = dynamic_cast<const SmLine*>(&curve))
    {
        return LineDistanceLower(point, P(line->GetLinePoint()), P(line->GetLineVector()));
    }
    if (const auto circle = dynamic_cast<const SmCircle*>(&curve))
    {
        if (auto call = cb.beforeNative(); !call)
        {
            return tl::unexpected(std::move(call.error()));
        }
        SmAxis2Placement frame;
        double radius = 0;
        if (auto status = CheckNativeStatus(circle->GetCanonical(frame, radius), NativeStatusOrigin::GeneralConstruction); !status)
        {
            return tl::unexpected(std::move(status.error()));
        }
        return CircleDistanceLower(point, { P(frame.GetOrigin()), P(frame.GetZAxis()), radius });
    }
    const auto spline = dynamic_cast<const SmBSplineCurve*>(&curve);
    if (!spline)
    {
        return tl::unexpected(Refusal("ConicalApexCurveProtectionScope"));
    }
    auto hull = Hull(spline, nullptr, limits, cb);
    if (!hull)
    {
        return tl::unexpected(std::move(hull.error()));
    }
    return BoxDistanceLower(point, *hull);
}
Result<double> SurfaceLower(
    const Point& point,
    const SmSurface& surface,
    double sufficientMm,
    const ConicalApexProofLimits& limits,
    const ConicalApexProofCallbacks& cb
)
{
    const auto plane = dynamic_cast<const SmPlane*>(&surface);
    const auto cylinder = dynamic_cast<const SmCylinder*>(&surface);
    const auto sphere = dynamic_cast<const SmSphere*>(&surface);
    const auto torus = dynamic_cast<const SmTorus*>(&surface);
    double analyticLower = 0;
    if (plane || cylinder || sphere || torus)
    {
        if (auto call = cb.beforeNative(); !call)
        {
            return tl::unexpected(std::move(call.error()));
        }
        SmAxis2Placement frame;
        double radius = 0, minor = 0;
        const auto status = plane    ? plane->GetCanonical(frame) :
                            cylinder ? cylinder->GetCanonical(frame, radius) :
                            sphere   ? sphere->GetCanonical(frame, radius) :
                                       torus->GetCanonical(frame, radius, minor);
        if (auto checked = CheckNativeStatus(status, NativeStatusOrigin::GeneralConstruction); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        const CircleProjectionGeometry canonical{ P(frame.GetOrigin()), P(frame.GetZAxis()), radius };
        auto distance = plane    ? PlaneDistanceLower(point, canonical.centerMm, canonical.normal) :
                        cylinder ? CylinderDistanceLower(point, canonical) :
                        sphere   ? SphereDistanceLower(point, canonical.centerMm, radius) :
                                   TorusDistanceLower(point, canonical, minor);
        if (!distance)
        {
            return tl::unexpected(std::move(distance.error()));
        }
        analyticLower = std::max(0., Down(*distance - Enclose(limits.definitionMm).high));
        if (analyticLower >= sufficientMm)
        {
            return analyticLower;
        }
    }
    const auto spline = dynamic_cast<const SmBSplineSurface*>(&surface);
    if (!spline)
    {
        return tl::unexpected(Refusal("ConicalApexSurfaceProtectionScope"));
    }
    auto hull = Hull(nullptr, spline, limits, cb);
    if (!hull)
    {
        return tl::unexpected(std::move(hull.error()));
    }
    auto distance = BoxDistanceLower(point, *hull);
    if (!distance)
    {
        return tl::unexpected(std::move(distance.error()));
    }
    return std::max(analyticLower, std::max(0., Down(*distance - Enclose(limits.definitionMm).high)));
}
} // namespace
Result<std::optional<ConicalApexPointProof>> ProveConicalApexPointCorrection(
    const Point& original,
    const SmCone& cone,
    const SmCircle& ring,
    std::span<const Point> others,
    std::span<const SmCurve* const> curves,
    std::span<const SmSurface* const> surfaces,
    const ConicalApexProofLimits& limits,
    const ConicalApexProofCallbacks& cb
)
{
    if (!cb.beforeNative || !cb.chargeWork || !limits.maximumControls || limits.maximumDisplacementMm.Numerator() <= 0 ||
        limits.maximumWorkToleranceMm.Numerator() <= 0 || limits.definitionMm.Numerator() <= 0)
    {
        return tl::unexpected(Refusal("ConicalApexProofConfiguration"));
    }
    try
    {
        if (auto call = cb.beforeNative(); !call)
        {
            return tl::unexpected(std::move(call.error()));
        }
        SmTArray<SmPoint3d> poles;
        ULONG approximate = SM_SS_NONE;
        const auto mask = cone.GetSingularities(&poles, nullptr, &approximate);
        if ((mask != SM_SS_VMIN && mask != SM_SS_VMAX) || mask != approximate || poles.GetSize() != 4)
        {
            return tl::unexpected(Refusal("ConicalApexUniqueNativePoleUnestablished"));
        }
        const auto target = P(poles[mask == SM_SS_VMIN ? 1 : 3]);
        if (target == original)
        {
            return std::optional<ConicalApexPointProof>{};
        }
        const double movement = NormUpper(Subtract(Exact(target), Exact(original)));
        if (movement > Enclose(limits.maximumDisplacementMm).low)
        {
            return tl::unexpected(Refusal("ConicalApexDisplacementLimit"));
        }
        if (auto call = cb.beforeNative(); !call)
        {
            return tl::unexpected(std::move(call.error()));
        }
        SmAxis2Placement frame;
        double radius = 0;
        if (auto checked = CheckNativeStatus(ring.GetCanonical(frame, radius), NativeStatusOrigin::GeneralConstruction); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        auto height = PlaneDistanceLower(original, P(frame.GetOrigin()), P(frame.GetZAxis()));
        if (!height)
        {
            return tl::unexpected(std::move(height.error()));
        }
        const double margin = Enclose(limits.definitionMm).high;
        double protection = std::max(0., Down(std::min(radius, *height) - margin));
        const double workMaximum = Enclose(limits.maximumWorkToleranceMm).high;
        const double needed = Up(100 * std::max(movement, workMaximum));
        const auto protect = [&](double lower) -> Result<void>
        {
            protection = std::min(protection, lower);
            if (!(protection > 0) || movement > Down(protection / 100) || workMaximum > Down(protection / 100))
            {
                return tl::unexpected(Refusal());
            }
            return {};
        };
        if (auto check = protect(protection); !check)
        {
            return tl::unexpected(std::move(check.error()));
        }
        for (const auto& other : others)
        {
            if (auto work = cb.chargeWork(1); !work)
            {
                return tl::unexpected(std::move(work.error()));
            }
            auto distance = BoxDistanceLower(original, Bounds{ other, other });
            if (!distance)
            {
                return tl::unexpected(std::move(distance.error()));
            }
            if (auto check = protect(std::max(0., Down(*distance - margin))); !check)
            {
                return tl::unexpected(std::move(check.error()));
            }
        }
        for (const auto* curve : curves)
        {
            if (!curve)
            {
                return tl::unexpected(Refusal());
            }
            if (auto work = cb.chargeWork(1); !work)
            {
                return tl::unexpected(std::move(work.error()));
            }
            auto lower = CurveLower(original, *curve, limits, cb);
            if (!lower)
            {
                return tl::unexpected(std::move(lower.error()));
            }
            if (auto check = protect(std::max(0., Down(*lower - margin))); !check)
            {
                return tl::unexpected(std::move(check.error()));
            }
        }
        for (const auto* surface : surfaces)
        {
            if (!surface)
            {
                return tl::unexpected(Refusal());
            }
            if (auto work = cb.chargeWork(1); !work)
            {
                return tl::unexpected(std::move(work.error()));
            }
            auto lower = SurfaceLower(original, *surface, needed, limits, cb);
            if (!lower)
            {
                return tl::unexpected(std::move(lower.error()));
            }
            if (auto check = protect(*lower); !check)
            {
                return tl::unexpected(std::move(check.error()));
            }
        }
        return std::optional<ConicalApexPointProof>{ ConicalApexPointProof{ original, target, movement, protection } };
    }
    catch (const std::overflow_error&)
    {
        return tl::unexpected(Refusal("ConicalApexProofNumericCapacity"));
    }
    catch (const std::domain_error&)
    {
        return tl::unexpected(Refusal("ConicalApexProofNumericDomain"));
    }
}
} // namespace step_import::detail
