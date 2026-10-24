// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportRevolutionConstruction.h"

#include "StepImportGeometryConstruction.h"
#include "StepImportSourceParameterUnits.h"

#include <SmSurfOfRevolution.h>
#include <vector>

namespace step_import::detail::construction
{
namespace
{
class RevolutionBuilder : private GeometryConstruction
{
public:

    explicit RevolutionBuilder(const GeometryConstruction& inputs) : GeometryConstruction(inputs)
    {
    }

    Result<std::unique_ptr<SmSurface>> Make(Index geometry, Index use, double angularStart)
    {
        auto interpreted = TryTerminalDomainSurface(*this, geometry, use, angularStart);
        if (!interpreted)
        {
            return tl::unexpected(std::move(interpreted.error()));
        }
        if (*interpreted)
        {
            return std::move(**interpreted);
        }
        if (!std::isfinite(angularStart) || angularStart < -360 || angularStart > 0)
        {
            return tl::unexpected(Fail("GeneralRevolutionAngularChartUnestablished", geometry, Status::Unsupported));
        }
        const auto generatorRecord = Ref(geometry, "swept_curve");
        if (!Is(generatorRecord, "B_SPLINE_CURVE_WITH_KNOTS"))
        {
            // Unbounded/trimmed/composite generators need their own exact domain
            // contract. Never substitute a fitted or arbitrarily clipped spline.
            return tl::unexpected(Fail("GeneralRevolutionGeneratorUnsupported", generatorRecord, Status::Unsupported));
        }
        const auto placement = Ref(geometry, "axis_position");
        auto originResult = Position(Ref(placement, "location"));
        if (!originResult)
        {
            return tl::unexpected(std::move(originResult.error()));
        }
        const auto origin = *originResult;
        V axis{ 0, 0, 1 }; // AP214 AXIS1_PLACEMENT.z DERIVE; no invented source DIRECTION.
        if (F(placement, "axis").kind != step_source::ValueKind::Unset)
        {
            auto direction = Direction(Ref(placement, "axis"));
            if (!direction)
            {
                return tl::unexpected(std::move(direction.error()));
            }
            axis = *direction;
        }
        const auto angleToRadians = SourceSiAngleToRadians(m_source->UnitContext());
        // ISO 10303-42 surface_of_revolution: numerical angular parameters use
        // basis-surface plane-angle units. Reuse the qualified SI angle policy
        // of TrimmedConic; PCURVE's parameter context cannot supply that scale.
        if (!angleToRadians || !std::isfinite(*angleToRadians) || !(*angleToRadians > 0))
        {
            return tl::unexpected(Fail("GeneralRevolutionAngleUnitUnestablished", geometry, Status::Unsupported));
        }
        auto curveResult = MakeSplineCurve(generatorRecord, use);
        if (!curveResult)
        {
            return tl::unexpected(std::move(curveResult.error()));
        }
        auto curve = std::move(*curveResult);
        // The shared factory returns SmBSplineCurve (including rational input),
        // retaining source knot parameters rather than edge [0,1] normalization.
        auto* spline = static_cast<SmBSplineCurve*>(curve.get());
        if (!spline)
        {
            return tl::unexpected(Fail("GeneralRevolutionGeneratorAllocation", geometry));
        }
        ULONG count = 0;
        double* net = nullptr;
        if (auto status = Good(spline->GetControlPointsPointer(count, net)); !status)
        {
            return tl::unexpected(std::move(status.error()));
        }
        if (!net || !count)
        {
            return tl::unexpected(Fail("GeneralRevolutionGeneratorDefinitionMissing", generatorRecord));
        }
        const std::vector<double> before(net, net + 4 * count);
        const auto degree = spline->GetDegree();
        const auto type = spline->GetType();
        const auto interval = spline->GetNaturalInterval();
        const auto stepInterval = spline->GetSTEPInterval();
        SmTArray<double> knots;
        if (auto status = Good(spline->GetKnotsAll(knots)); !status)
        {
            return tl::unexpected(std::move(status.error()));
        }
        auto sourceKnots = Numbers(generatorRecord, "knots");
        if (!sourceKnots)
        {
            return tl::unexpected(std::move(sourceKnots.error()));
        }
        auto sourceMultiplicities = Multiplicities(generatorRecord, "knot_multiplicities");
        if (!sourceMultiplicities)
        {
            return tl::unexpected(std::move(sourceMultiplicities.error()));
        }
        ULONG at = 0;
        for (ULONG i = 0; i < sourceKnots->GetSize(); ++i)
        {
            for (ULONG j = 0; j < (*sourceMultiplicities)[i]; ++j)
            {
                if (at >= knots.GetSize() || knots[at++] != (*sourceKnots)[i])
                {
                    return tl::unexpected(Fail("GeneralRevolutionSourceKnotsChanged", generatorRecord));
                }
            }
        }
        if (at != knots.GetSize() || interval.GetMin() != stepInterval.GetMin() || interval.GetMax() != stepInterval.GetMax())
        {
            return tl::unexpected(Fail("GeneralRevolutionSourceParametersUnestablished", generatorRecord));
        }
        SmTArray<SmPoint3d> polygon;
        SmTArray<double> weights;
        if (auto status = Good(spline->GetControlPolygon(polygon, weights)); !status)
        {
            return tl::unexpected(std::move(status.error()));
        }
        double radiusBound = 0;
        for (ULONG i = 0; i < polygon.GetSize(); ++i)
        {
            radiusBound = std::max(radiusBound, Norm(Sub(P(polygon[i]), origin)));
        }

        SmSurfOfRevolution* raw = nullptr;
        if (auto operation = Step(geometry); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const auto status = SmSurfOfRevolution::CreateCanonical(*m_context, spline, P(origin), D(axis), raw, FALSE);
        std::unique_ptr<SmSurfOfRevolution> surface(raw);
        // CreateCanonical adopts the generator before MakeNurb, which can fail.
        // Release the independent owner even on that error path; the returned
        // candidate then reclaims both definitions exactly once.
        if (surface && spline->GetOwner() == surface.get())
        {
            curve.release();
        }
        if (auto good = Good(status); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (!surface || curve)
        {
            return tl::unexpected(Fail("GeneralRevolutionGeneratorOwnership", geometry));
        }

        if (angularStart != 0)
        {
            // Select a full periodic computational chart for source pcurves.
            // V remains the complete original generator interval. The native
            // adjust operation can touch its generator, so the checks below
            // must still prove all generator coefficients/knots/domains equal.
            if (auto operation = Step(geometry); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto good = Good(
                    surface->AdjustSTEPUVDomain(SmExtent2d(angularStart, stepInterval.GetMin(), angularStart + 360, stepInterval.GetMax()))
                );
                !good)
            {
                return tl::unexpected(std::move(good.error()));
            }
        }

        SmBSplineCurve* retained = nullptr;
        SmPoint3d actualOrigin;
        SmVector3d actualAxis;
        SmBoolean swapped = TRUE;
        if (auto good = Good(surface->GetCanonical(retained, actualOrigin, actualAxis, &swapped)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (retained != spline || swapped || retained->GetType() != type || retained->GetDegree() != degree ||
            retained->GetNaturalInterval().GetMin() != interval.GetMin() || retained->GetNaturalInterval().GetMax() != interval.GetMax() ||
            retained->GetSTEPInterval().GetMin() != stepInterval.GetMin() || retained->GetSTEPInterval().GetMax() != stepInterval.GetMax())
        {
            return tl::unexpected(Fail("GeneralRevolutionGeneratorParametersChanged", geometry));
        }
        const auto stepDomain = surface->GetSTEPUVDomain();
        if (stepDomain.GetUInterval().GetMin() != angularStart || stepDomain.GetUInterval().GetMax() != angularStart + 360 ||
            stepDomain.GetVInterval().GetMin() != stepInterval.GetMin() || stepDomain.GetVInterval().GetMax() != stepInterval.GetMax())
        {
            return tl::unexpected(Fail("GeneralRevolutionSurfaceParametersChanged", geometry));
        }
        ULONG afterCount = 0;
        double* after = nullptr;
        if (auto good = Good(retained->GetControlPointsPointer(afterCount, after)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        SmTArray<double> afterKnots;
        if (auto good = Good(retained->GetKnotsAll(afterKnots)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (!after || afterCount != count || afterKnots.GetSize() != knots.GetSize())
        {
            return tl::unexpected(Fail("GeneralRevolutionGeneratorDefinitionChanged", geometry));
        }
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            if (before[i] != after[i])
            {
                return tl::unexpected(Fail("GeneralRevolutionGeneratorDefinitionChanged", geometry));
            }
        }
        for (ULONG i = 0; i < knots.GetSize(); ++i)
        {
            if (knots[i] != afterKnots[i])
            {
                return tl::unexpected(Fail("GeneralRevolutionGeneratorDefinitionChanged", geometry));
            }
        }

        // Positive rational control convex hull bounds radius. Bound the effect
        // of native axis/origin roundtrip independently of working tolerances;
        // this does not certify the later source/native UV or trim agreement.
        const double axisError = Norm(Sub(V{ actualAxis.x, actualAxis.y, actualAxis.z }, axis));
        const double originError = Norm(Sub(P(actualOrigin), origin));
        const double errorBound = 2 * radiusBound * axisError + 2 * originError;
        if (!std::isfinite(errorBound) || errorBound > Q(m_options.quality.definitionMm))
        {
            return tl::unexpected(Fail("GeneralRevolutionAxisRoundtripUnestablished", geometry, Status::Indeterminate));
        }

        ParameterMapping mapping;
        mapping.kind = "RevolutionSourceParameters";
        mapping.sourceGeometryId = Id(geometry);
        mapping.sourceBasisGeometryId = Id(generatorRecord);
        mapping.sourceUseId = Id(use);
        mapping.equation =
            "source revolution u in qualified basis-surface angle units -> native STEP u degrees; source v -> unchanged generator STEP parameter; ordinary native UV is rational, not angle";
        mapping.parameters = { { "generator_source_parameter_interval", { stepInterval.GetMin(), stepInterval.GetMax() } },
                               { "source_angle_to_native_degrees", { *angleToRadians * 180 / std::numbers::pi } },
                               { "native_angular_chart_degrees", { angularStart, angularStart + 360 } },
                               { "axis_origin_mm", { origin.begin(), origin.end() } },
                               { "axis_unit_direction", { axis.begin(), axis.end() } },
                               { "native_axis_roundtrip_bound_mm", { errorBound } } };
        m_report.parameterMappings.push_back(std::move(mapping));
        return std::unique_ptr<SmSurface>(std::move(surface));
    }
};
} // namespace
Result<std::unique_ptr<SmSurface>> MakeSourceRevolution(const GeometryConstruction& inputs, std::size_t surface, std::size_t use, double angularStart)
{
    return RevolutionBuilder(inputs).Make(surface, use, angularStart);
}
} // namespace step_import::detail::construction
