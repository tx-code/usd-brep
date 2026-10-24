// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportCircleVertexCorrection.h"
#include "StepImportExecution.h"
#include "StepImportInterval.h"
#include "StepImportVertexProjectionProof.h"

#include <SmAxis2Placement.h>
#include <SmBSplineSurface.h>
#include <SmCylinder.h>
#include <SmPlane.h>
#include <SmSphere.h>
#include <SmTorus.h>
#include <algorithm>

namespace step_import::detail
{
namespace
{
Failure Refusal(std::size_t face)
{
    return { Status::Indeterminate,
             "CircleVertexSurfaceProtectionUnestablished",
             "Complete original-model nonparticipating surface clearance is unestablished",
             face,
             "Repair" };
}
template <class T>
Point P(const T& p)
{
    return { p.x, p.y, p.z };
}
Result<Bounds> Hull(const SmSurface& surface, std::size_t face, Execution& execution, Report& report)
{
    const auto spline = dynamic_cast<const SmBSplineSurface*>(&surface);
    if (!spline)
    {
        return tl::unexpected(Refusal(face));
    }
    if (auto call = execution.ChargeNative(report, face); !call)
    {
        return tl::unexpected(std::move(call.error()));
    }
    ULONG u = 0, v = 0;
    double* data = nullptr;
    if (spline->GetControlPointsPointer(u, v, data) != SM_SUCCESS || !u || !v || !data ||
        std::size_t(u) > std::numeric_limits<std::size_t>::max() / std::size_t(v) / 4)
    {
        return tl::unexpected(Refusal(face));
    }
    Bounds box{ Point{ INFINITY, INFINITY, INFINITY }, Point{ -INFINITY, -INFINITY, -INFINITY } };
    for (std::size_t i = 0; i < std::size_t(u) * std::size_t(v); ++i)
    {
        if (auto work = execution.SpendCylindricalBandProof(); !work)
        {
            return tl::unexpected(std::move(work.error()));
        }
        const double weight = spline->IsRational() ? data[i * 4 + 3] : 1.;
        if (!(weight > 0) || !std::isfinite(weight))
        {
            return tl::unexpected(Refusal(face));
        }
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            // Native rational poles are homogeneous. Outward division encloses
            // each Euclidean pole; positive weights enclose the entire surface
            // in their hull, including interiors that boundary samples miss.
            const auto coordinate = Exact(data[i * 4 + axis]) / Exact(weight);
            box[0][axis] = std::min(box[0][axis], coordinate.low);
            box[1][axis] = std::max(box[1][axis], coordinate.high);
        }
    }
    return box;
}
Result<double> AnalyticLower(const Point& point, const SmSurface& surface, std::size_t face, Execution& execution, Report& report)
{
    const auto plane = dynamic_cast<const SmPlane*>(&surface);
    const auto cylinder = dynamic_cast<const SmCylinder*>(&surface);
    const auto sphere = dynamic_cast<const SmSphere*>(&surface);
    const auto torus = dynamic_cast<const SmTorus*>(&surface);
    if (!plane && !cylinder && !sphere && !torus)
    {
        return 0.; // A positive-weight surface hull remains a valid bound.
    }
    if (auto call = execution.ChargeNative(report, face); !call)
    {
        return tl::unexpected(std::move(call.error()));
    }
    SmAxis2Placement frame;
    double radius = 0, minor = 0;
    const auto status = plane    ? plane->GetCanonical(frame) :
                        cylinder ? cylinder->GetCanonical(frame, radius) :
                        sphere   ? sphere->GetCanonical(frame, radius) :
                                   torus->GetCanonical(frame, radius, minor);
    if (status != SM_SUCCESS)
    {
        return tl::unexpected(Refusal(face));
    }
    const CircleProjectionGeometry circle{ P(frame.GetOrigin()), P(frame.GetZAxis()), radius };
    return plane    ? PlaneDistanceLower(point, circle.centerMm, circle.normal) :
           cylinder ? CylinderDistanceLower(point, circle) :
           sphere   ? SphereDistanceLower(point, circle.centerMm, radius) :
                      TorusDistanceLower(point, circle, minor);
}
} // namespace
Result<void> ProtectCircleVerticesFromSurfaces(
    const step_semantics::Snapshot& source,
    const Options& options,
    std::span<const CircleVertexSurfaceView> surfaces,
    std::vector<CircleVertexCorrectionEvidence>& evidence,
    Execution& execution,
    Report& report
)
{
    try
    {
        for (const auto& face : surfaces)
        {
            if (!face.nativeSurface)
            {
                return tl::unexpected(Refusal(face.faceRecord));
            }
            std::optional<Bounds> hull;
            for (auto& edit : evidence)
            {
                if (auto work = execution.SpendCylindricalBandProof(); !work)
                {
                    return tl::unexpected(std::move(work.error()));
                }
                const auto vertex = source.Source().Find(edit.edit.vertexId);
                if (!vertex)
                {
                    return tl::unexpected(Refusal(face.faceRecord));
                }
                const auto record = std::size_t(vertex - source.Source().Records().data());
                if (std::find(face.sourceVertexRecords.begin(), face.sourceVertexRecords.end(), record) != face.sourceVertexRecords.end())
                {
                    continue; // Participating faces are checked by final trim/vertex postchecks.
                }
                if (!hull)
                {
                    auto box = Hull(*face.nativeSurface, face.faceRecord, execution, report);
                    if (!box)
                    {
                        return tl::unexpected(std::move(box.error()));
                    }
                    hull = *box;
                }
                auto boxDistance = BoxDistanceLower(edit.originalMm, *hull);
                auto analytic = AnalyticLower(edit.originalMm, *face.nativeSurface, face.faceRecord, execution, report);
                if (!boxDistance || !analytic)
                {
                    return tl::unexpected(!boxDistance ? std::move(boxDistance.error()) : std::move(analytic.error()));
                }
                // Canonical analytic supports contain the retained face. Keep
                // an independent definition-error margin; the positive-weight
                // native hull supplies another conservative, finite bound.
                const double distance = std::max(*boxDistance, std::max(0., Down(*analytic - Enclose(options.quality.definitionMm).high)));
                edit.localProtectionLowerMm = std::min(edit.localProtectionLowerMm, distance);
                const double limit = Down(edit.localProtectionLowerMm / 100.);
                if (!(edit.localProtectionLowerMm > 0) || edit.displacementUpperMm > limit || edit.originalGapUpperMm > limit ||
                    Enclose(options.quality.maximumWorkToleranceMm).high > limit)
                {
                    return tl::unexpected(Refusal(face.faceRecord));
                }
            }
        }
        for (const auto& edit : evidence)
        {
            if (auto protection = execution.TightenCircleVertexProtection(edit.localProtectionLowerMm); !protection)
            {
                return tl::unexpected(std::move(protection.error()));
            }
        }
        return {};
    }
    catch (const std::overflow_error&)
    {
        return tl::unexpected(Refusal(surfaces.empty() ? 0 : surfaces.front().faceRecord));
    }
    catch (const std::domain_error&)
    {
        return tl::unexpected(Refusal(surfaces.empty() ? 0 : surfaces.front().faceRecord));
    }
}
} // namespace step_import::detail
