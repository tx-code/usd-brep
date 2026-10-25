// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPcurveConstruction.h"

#include "StepImportGeometryConstruction.h"
#include "StepImportRevolutionConstruction.h"
#include "StepImportSourceParameterUnits.h"
#include "StepImportSplineSurfaceConstruction.h"

#include <SmBSplineSurface.h>
#include <SmCrvOnSurf.h>
#include <SmCylinder.h>
#include <SmPlane.h>
#include <SmSTEPSurface.h>
#include <limits>

namespace step_import::detail::construction
{
namespace
{
class PcurveBuilder : private GeometryConstruction
{
public:

    explicit PcurveBuilder(const GeometryConstruction& inputs) : GeometryConstruction(inputs)
    {
    }

    Result<PcurveDefinition> Make(Index pcurve, Index use)
    {
        // AP214E3_2010.exp PCURVE.reference_to_curve/DEFINITIONAL_REPRESENTATION
        // have already passed the owning source-layer contract. Native parameter
        // admission and numerical-evidence limits live in STEP_IMPORT/README.md,
        // "以源 pcurve 为优先表示的三维边"; this is not full AP validation.
        const auto basis = Ref(pcurve, "basis_surface");
        const auto representation = Ref(pcurve, "reference_to_curve");
        const auto parameterCurve = Refs(representation, "items").at(0);
        if (!Is(parameterCurve, "B_SPLINE_CURVE_WITH_KNOTS") || Is(parameterCurve, "RATIONAL_B_SPLINE_CURVE"))
        {
            // Rational parameter curves need a physical lift-error bound for
            // coefficient roundtrip, not the millimetre test for 3D controls.
            return tl::unexpected(Fail("GeneralPcurveParameterCurveUnsupported", parameterCurve, Status::Unsupported));
        }
        SmTArray<SmPoint3d> cp;
        for (const auto record : Refs(parameterCurve, "control_points_list"))
        {
            const auto& values = F(record, "coordinates").children;
            if (values.size() != 2)
            {
                return tl::unexpected(Fail("GeneralPcurveParameterDimension", record));
            }
            auto x = Number(values[0]), y = Number(values[1]);
            if (!x)
            {
                return tl::unexpected(std::move(x.error()));
            }
            if (!y)
            {
                return tl::unexpected(std::move(y.error()));
            }
            cp.Add(SmPoint3d(*x, *y, 0));
        }
        double uScale = 1, vScale = 1, uOffset = 0, vOffset = 0;
        const bool plane = Is(basis, "PLANE"), cylinder = Is(basis, "CYLINDRICAL_SURFACE"), revolution = Is(basis, "SURFACE_OF_REVOLUTION"),
                   spline = Is(basis, "B_SPLINE_SURFACE_WITH_KNOTS");
        if (!plane && !cylinder && !revolution && !spline)
        {
            return tl::unexpected(Fail("GeneralPcurveBasisUnsupported", basis, Status::Unsupported));
        }
        if (plane)
        {
            uScale = vScale = scale;
        }
        if (cylinder)
        {
            vScale = scale;
        }
        if (cylinder || revolution)
        {
            const auto angle = SourceSiAngleToRadians(m_source->UnitContext());
            if (!angle || !std::isfinite(*angle) || !(*angle > 0))
            {
                return tl::unexpected(Fail("GeneralPcurveAngleUnitUnestablished", basis, Status::Unsupported));
            }
            uScale = *angle * 180 / std::numbers::pi;
        }
        SmExtent2d domain;
        for (ULONG i = 0; i < cp.GetSize(); ++i)
        {
            cp[i].x *= uScale;
            cp[i].y *= vScale;
            if (!std::isfinite(cp[i].x) || !std::isfinite(cp[i].y))
            {
                return tl::unexpected(Fail("GeneralPcurveCoordinateRange", parameterCurve, Status::Unsupported));
            }
            domain.AddPoint2d(SmPoint2d(cp[i].x, cp[i].y));
        }
        double angularStart = 0;
        if (cylinder || revolution)
        {
            // One constant integral turn preserves the entire defining curve.
            // Do not wrap individual controls: that changes the source curve.
            const double turns = std::floor((domain.GetMin().x + 180) / 360);
            uOffset = -360 * turns;
            if (!std::isfinite(turns) || std::abs(turns) > 1000000)
            {
                return tl::unexpected(Fail("GeneralPcurveAngularRangeUnestablished", pcurve, Status::Unsupported));
            }
            const double low = domain.GetMin().x + uOffset, high = domain.GetMax().x + uOffset;
            angularStart = std::min(0., low);
            if (angularStart < -360 || high > angularStart + 360)
            {
                return tl::unexpected(Fail("GeneralPcurveAngularHullUnestablished", pcurve, Status::Unsupported));
            }
        }
        std::unique_ptr<SmSurface> surface;
        if (plane || cylinder)
        {
            auto frame = Placement(Ref(basis, "position"));
            if (!frame)
            {
                return tl::unexpected(std::move(frame.error()));
            }
            if (auto operation = Step(basis); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (plane)
            {
                // Computational support window contains the complete positive
                // B-spline control hull. No point or defining plane is moved.
                const auto lo = domain.GetMin(), hi = domain.GetMax();
                const auto padding = std::max(
                    1.,
                    64 * std::numeric_limits<double>::epsilon() * (1 + std::abs(lo.x) + std::abs(lo.y) + std::abs(hi.x) + std::abs(hi.y))
                );
                surface.reset(new (*m_context) SmPlane(
                    P(frame->o),
                    D(frame->x),
                    D(frame->y),
                    SmVector2d(1, 1),
                    SmExtent2d(lo.x - padding, lo.y - padding, hi.x + padding, hi.y + padding),
                    m_context
                ));
            }
            else
            {
                auto radius = N(basis, "radius");
                if (!radius)
                {
                    return tl::unexpected(std::move(radius.error()));
                }
                const double bottom = domain.GetMin().y - 1, top = domain.GetMax().y + 1;
                if (!std::isfinite(bottom) || !std::isfinite(top) || !(top > bottom) || !std::isfinite(*radius * scale) || !(*radius * scale > 0))
                {
                    return tl::unexpected(Fail("GeneralPcurveCylinderWindowUnestablished", basis, Status::Unsupported));
                }
                vOffset = -bottom;
                surface.reset(new (*m_context) SmCylinder(
                    P(Add(frame->o, Mul(frame->z, bottom))),
                    D(frame->x),
                    D(frame->y),
                    *radius * scale,
                    angularStart,
                    angularStart + 360,
                    top - bottom,
                    FALSE,
                    FALSE,
                    TRUE,
                    m_context
                ));
            }
        }
        else
        {
            auto made = revolution ? MakeSourceRevolution(*this, basis, use, angularStart) : MakeSourceSplineSurface(*this, basis, use);
            if (!made)
            {
                return tl::unexpected(std::move(made.error()));
            }
            surface = std::move(*made);
        }
        if (!surface)
        {
            return tl::unexpected(Fail("GeneralPcurveBasisAllocation", basis));
        }
        const auto allowed = surface->GetSTEPUVDomain();
        domain = {};
        for (ULONG i = 0; i < cp.GetSize(); ++i)
        {
            cp[i].x += uOffset;
            cp[i].y += vOffset;
            const SmPoint2d point(cp[i].x, cp[i].y);
            // Hull inclusion is deliberately conservative. A legitimate curve
            // whose hull exits the support needs a stronger domain proof; never
            // rely on the kernel silently clamping its defining parameter data.
            if (!allowed.ContainsPoint2d(point))
            {
                return tl::unexpected(Fail("GeneralPcurveBasisDomainUnestablished", pcurve, Status::Indeterminate));
            }
            domain.AddPoint2d(point);
        }
        Bounds box{ Point{ INFINITY, INFINITY, INFINITY }, Point{ -INFINITY, -INFINITY, -INFINITY } };
        const auto controlSurface = dynamic_cast<const SmBSplineSurface*>(surface.get());
        if (!controlSurface)
        {
            return tl::unexpected(Fail("GeneralPcurveBasisBoundsUnestablished", basis, Status::Unsupported));
        }
        SmTArray<SmPoint3d> surfacePoints;
        SmTArray<double> surfaceWeights;
        if (auto operation = Step(basis); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        ULONG uCount = 0, vCount = 0;
        if (auto good = Good(controlSurface->GetControlPointNet(uCount, vCount, surfacePoints, surfaceWeights)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (!surfacePoints.GetSize() || (surfaceWeights.GetSize() && surfaceWeights.GetSize() != surfacePoints.GetSize()))
        {
            return tl::unexpected(Fail("GeneralPcurveBasisBoundsUnestablished", basis, Status::Unsupported));
        }
        // The full positive support net is a conservative 3D box. The implicit
        // curve's cached fitted bbox is not source boundary coverage evidence.
        for (ULONG i = 0; i < surfacePoints.GetSize(); ++i)
        {
            if (surfaceWeights.GetSize() && (!(surfaceWeights[i] > 0) || !std::isfinite(surfaceWeights[i])))
            {
                return tl::unexpected(Fail("GeneralPcurveBasisBoundsUnestablished", basis, Status::Unsupported));
            }
            const auto point = P(surfacePoints[i]);
            for (Index k = 0; k < 3; ++k)
            {
                if (!std::isfinite(point[k]))
                {
                    return tl::unexpected(Fail("GeneralPcurveBasisBoundsUnestablished", basis, Status::Unsupported));
                }
                box[0][k] = std::min(box[0][k], point[k]);
                box[1][k] = std::max(box[1][k], point[k]);
            }
        }
        if (cylinder)
        {
            SmAxis2Placement frame;
            double radius = 0;
            if (auto good = Good(static_cast<SmCylinder*>(surface.get())->GetCanonical(frame, radius)); !good)
            {
                return tl::unexpected(std::move(good.error()));
            }
            const auto origin = P(frame.GetOriginRef());
            const auto x = frame.GetXAxisRef(), y = frame.GetYAxisRef(), z = frame.GetZAxis();
            const V axis{ z.x, z.y, z.z }, first{ x.x, x.y, x.z }, second{ y.x, y.y, y.z };
            // The finite native support has padding; the defining curve does
            // not. Bound the entire circular sweep over its UV control hull's
            // axial interval, rather than extending the source boundary to the
            // artificial support window. |x*cos(u)+y*sin(u)| <= |x|+|y|.
            for (Index k = 0; k < 3; ++k)
            {
                const double a = origin[k] + domain.GetMin().y * axis[k], b = origin[k] + domain.GetMax().y * axis[k];
                const double extent = std::abs(radius) * (std::abs(first[k]) + std::abs(second[k]));
                box[0][k] = std::min(a, b) - extent;
                box[1][k] = std::max(a, b) + extent;
            }
        }
        auto knots = Numbers(parameterCurve, "knots");
        if (!knots)
        {
            return tl::unexpected(std::move(knots.error()));
        }
        auto multiplicities = Multiplicities(parameterCurve, "knot_multiplicities");
        if (!multiplicities)
        {
            return tl::unexpected(std::move(multiplicities.error()));
        }
        auto degree = Degree(parameterCurve, "degree");
        if (!degree)
        {
            return tl::unexpected(std::move(degree.error()));
        }
        if (auto operation = Step(parameterCurve); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        SmBSplineCurve* raw = nullptr;
        const auto status = SmBSplineCurve::CreateCanonical(
            *m_context,
            2,
            *degree,
            cp,
            SM_CF_UNSPECIFIED,
            *multiplicities,
            *knots,
            SM_KT_UNSPECIFIED,
            nullptr,
            nullptr,
            raw
        );
        std::unique_ptr<SmCurve> uv(raw);
        if (auto good = Good(status); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (!raw)
        {
            return tl::unexpected(Fail("GeneralPcurveParameterAllocation", parameterCurve));
        }
        SmTArray<SmPoint3d> actual;
        SmTArray<double> weights, actualKnots;
        if (auto good = Good(raw->GetControlPolygon(actual, weights)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (auto good = Good(raw->GetKnotsAll(actualKnots)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (actual.GetSize() != cp.GetSize() || raw->GetDim() != 2 || raw->GetDegree() != *degree || raw->IsRational())
        {
            return tl::unexpected(Fail("GeneralPcurveParameterDefinitionChanged", parameterCurve));
        }
        for (ULONG i = 0; i < cp.GetSize(); ++i)
        {
            if (actual[i].x != cp[i].x || actual[i].y != cp[i].y || actual[i].z != 0)
            {
                return tl::unexpected(Fail("GeneralPcurveParameterDefinitionChanged", parameterCurve));
            }
        }
        ULONG at = 0;
        for (ULONG i = 0; i < knots->GetSize(); ++i)
        {
            for (ULONG j = 0; j < (*multiplicities)[i]; ++j)
            {
                if (at >= actualKnots.GetSize() || actualKnots[at++] != (*knots)[i])
                {
                    return tl::unexpected(Fail("GeneralPcurveSourceKnotsChanged", parameterCurve));
                }
            }
        }
        if (at != actualKnots.GetSize())
        {
            return tl::unexpected(Fail("GeneralPcurveSourceKnotsChanged", parameterCurve));
        }
        // Both definitions are prepared with explicit owners. Adopt; do not use
        // legacy constructor-copy flags or substitute a fitted 3D definition.
        std::unique_ptr<SmSurface> wrapped(new (*m_context) SmSTEPSurface(*surface, TRUE));
        surface.release();
        wrapped->SetContext(m_context);
        std::unique_ptr<SmCurve> lifted(new (*m_context) SmCrvOnSurf(*uv, *wrapped, &allowed, 3, m_context));
        uv.release();
        wrapped.release();
        if (revolution && m_options.quality.pcurveCarrierWorkToleranceMm)
        {
            // Selected before construction: this is a numerical work zone on
            // the owned exact composition, not pole snapping, source repair,
            // a topology tolerance change, or a retry after failed validation.
            const auto requested = Q(*m_options.quality.pcurveCarrierWorkToleranceMm);
            auto* composition = static_cast<SmCrvOnSurf*>(lifted.get());
            if (auto operation = Step(pcurve); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto good = Good(composition->SetSupportWorkTolerance(requested)); !good)
            {
                return tl::unexpected(std::move(good.error()));
            }
            const double effective = SmTol::GetZoneTol3d(composition->GetBaseSurface());
            if (!std::isfinite(effective) || effective <= 0 || effective > requested)
            {
                return tl::unexpected(Fail("GeneralPcurveCarrierPrecisionNotApplied", pcurve));
            }
            m_report.pcurveCarrierPrecisions.push_back({ Id(pcurve), Id(basis), Id(use), requested, effective });
        }
        ParameterMapping mapping;
        mapping.kind = "SourcePcurveComposition";
        mapping.sourceGeometryId = Id(pcurve);
        mapping.sourceBasisGeometryId = Id(basis);
        mapping.sourceUseId = Id(use);
        mapping.equation = "native 3D C(t)=basis_STEP(A*source_uv(t)+b); source t/knots unchanged; source UV retained in immutable snapshot";
        mapping.parameters = { { "source_uv_to_native_step_scale", { uScale, vScale } },
                               { "source_uv_to_native_step_offset", { uOffset, vOffset } },
                               { "source_parameter_interval", { raw->GetNaturalInterval().GetMin(), raw->GetNaturalInterval().GetMax() } } };
        m_report.parameterMappings.push_back(std::move(mapping));
        if (auto claimed = m_execution.ClaimTerminalDomainPcurve(Id(pcurve)); !claimed)
        {
            return tl::unexpected(std::move(claimed.error()));
        }
        return PcurveDefinition{ std::move(lifted), box };
    }
};
} // namespace
Result<PcurveDefinition> MakeSourcePcurve(const GeometryConstruction& inputs, std::size_t pcurve, std::size_t use)
{
    return PcurveBuilder(inputs).Make(pcurve, use);
}
} // namespace step_import::detail::construction
