// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportGeometryConstruction.h"
#include "StepImportInterval.h"
#include "StepImportPcurveConstruction.h"
#include "StepImportPcurveLineWindow.h"
#include "StepImportSplineSurfaceConstruction.h"

#include <SmBSplineSurface.h>
#include <SmCrvOnSurf.h>
#include <SmSTEPSurface.h>
#include <limits>

namespace step_import::detail::construction
{
namespace
{
std::string RatioText(const PcurveLineWindowRatio& ratio)
{
    return "(" + ratio.numerator + "/" + ratio.denominator + ")";
}
class LinePcurveBuilder : private GeometryConstruction
{
public:

    explicit LinePcurveBuilder(const GeometryConstruction& inputs) : GeometryConstruction(inputs)
    {
    }
    Result<PcurveDefinition> Make(Index pcurve, Index use, bool definingMaster)
    {
        const auto basis = Ref(pcurve, "basis_surface");
        const auto parameterCurve = Refs(Ref(pcurve, "reference_to_curve"), "items").at(0);
        // Pinned AP214E3_2010.exp (71ab140f...), LINE/VECTOR: the
        // parameter is scaled by magnitude/|direction_ratios|. Using raw
        // ratios for the geometric generator avoids rounding its direction
        // through an unnecessary square root; the source t map stays explicit.
        // Only a finite plain B-spline carrier currently supplies a complete
        // admissible work window. Infinite planes/cylinders need a use-domain
        // contract rather than an arbitrary large segment.
        if (!Is(basis, "B_SPLINE_SURFACE_WITH_KNOTS"))
        {
            return tl::unexpected(Fail("GeneralPcurveLineBasisUnsupported", basis, Status::Unsupported));
        }
        if (m_options.terminalDomainInterpretation || m_options.targetSurfacePrecision || m_options.normalizePeriodicFaces ||
            m_options.restrictUnusedSupportDomains || m_options.faceOrientationRepair || m_options.cylindricalBandRepair)
        {
            return tl::unexpected(Fail("GeneralPcurveLineOptionCombinationUnsupported", pcurve, Status::Unsupported));
        }
        const auto vector = Ref(parameterCurve, "dir");
        const auto point = Ref(parameterCurve, "pnt"), direction = Ref(vector, "orientation");
        const auto& coordinates = F(point, "coordinates").children;
        const auto& ratios = F(direction, "direction_ratios").children;
        if (coordinates.size() != 2 || ratios.size() != 2)
        {
            return tl::unexpected(Fail("GeneralPcurveParameterDimension", parameterCurve));
        }
        auto magnitude = N(vector, "magnitude");
        if (!magnitude)
        {
            return tl::unexpected(std::move(magnitude.error()));
        }
        if (!(*magnitude > 0))
        {
            return tl::unexpected(Fail("GeneralPcurveLineMagnitudeUnestablished", vector, Status::Indeterminate));
        }
        auto surfaceResult = MakeSourceSplineSurface(*this, basis, use);
        if (!surfaceResult)
        {
            return tl::unexpected(std::move(surfaceResult.error()));
        }
        auto surface = std::move(*surfaceResult);
        if (!surface)
        {
            return tl::unexpected(Fail("GeneralPcurveBasisAllocation", basis));
        }
        if (auto operation = Step(basis); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const auto allowed = surface->GetSTEPUVDomain();
        const std::array<double, 4> rectangle{ allowed.GetMin().x, allowed.GetMin().y, allowed.GetMax().x, allowed.GetMax().y };
        const SplineChartCallbacks nativeCallbacks{ [&]
                                                    {
                                                        return Step(use);
                                                    },
                                                    [&](std::size_t work)
                                                    {
                                                        return m_execution.SpendExactGeometryWork(m_report, work, use);
                                                    } };
        const PcurveLineWindowCallbacks exactCallbacks{ [&](std::size_t work)
                                                        {
                                                            return m_execution.SpendExactGeometryWork(m_report, work, use);
                                                        },
                                                        [&]
                                                        {
                                                            return m_execution.Check(Phase::BeforeNativeCall);
                                                        } };
        const ExactDecimalPcurveLineGenerator generator{ { coordinates[0].text, coordinates[1].text }, { ratios[0].text, ratios[1].text } };
        // Per-operation capacities stay below the request-wide exact-work
        // ledger. These are computational/storage bounds, not new mm defaults.
        const PcurveLineWindowLimits limits{ 4096, m_options.nativeLimits.exactWorkUnits, 32768, 1234 };
        auto planned = PlanPcurveLineWindow(generator, rectangle, limits, exactCallbacks);
        if (!planned)
        {
            if (!planned.error().record)
            {
                planned.error().record = parameterCurve;
            }
            return tl::unexpected(std::move(planned.error()));
        }
        if (!*planned)
        {
            return tl::unexpected(Fail("GeneralPcurveLineFiniteWindowUnestablished", pcurve, Status::Indeterminate));
        }
        const auto& window = **planned;
        SmTArray<SmPoint3d> cp;
        for (const auto& endpoint : window.uvEnds)
        {
            cp.Add(SmPoint3d(endpoint[0], endpoint[1], 0));
        }
        SmTArray<double> knots;
        knots.Add(0);
        knots.Add(1);
        SmTArray<ULONG> multiplicities;
        multiplicities.Add(2);
        multiplicities.Add(2);
        SmBSplineCurve* raw = nullptr;
        if (auto operation = Step(parameterCurve); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const auto created =
            SmBSplineCurve::CreateCanonical(*m_context, 2, 1, cp, SM_CF_UNSPECIFIED, multiplicities, knots, SM_KT_UNSPECIFIED, nullptr, nullptr, raw);
        std::unique_ptr<SmCurve> uv(raw);
        if (auto good = Good(created); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (!raw)
        {
            return tl::unexpected(Fail("GeneralPcurveParameterAllocation", parameterCurve));
        }
        const double definitionAllowance = Enclose(m_options.quality.definitionMm).low;
        auto lift = ProvePcurveLineLift(*surface, *uv, window.uvEnds, window.maximumRoundingUpperUv, definitionAllowance, nativeCallbacks);
        if (!lift)
        {
            if (!lift.error().record)
            {
                lift.error().record = pcurve;
            }
            return tl::unexpected(std::move(lift.error()));
        }
        const auto* controlSurface = dynamic_cast<const SmBSplineSurface*>(surface.get());
        if (!controlSurface)
        {
            return tl::unexpected(Fail("GeneralPcurveBasisBoundsUnestablished", basis, Status::Unsupported));
        }
        SmTArray<SmPoint3d> points;
        SmTArray<double> weights;
        ULONG nu = 0, nv = 0;
        if (auto operation = Step(basis); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto good = Good(controlSurface->GetControlPointNet(nu, nv, points, weights)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (!points.GetSize() || (weights.GetSize() && weights.GetSize() != points.GetSize()))
        {
            return tl::unexpected(Fail("GeneralPcurveBasisBoundsUnestablished", basis));
        }
        Bounds box{ Point{ INFINITY, INFINITY, INFINITY }, Point{ -INFINITY, -INFINITY, -INFINITY } };
        for (ULONG i = 0; i < points.GetSize(); ++i)
        {
            const auto value = P(points[i]);
            for (Index k = 0; k < 3; ++k)
            {
                if (!std::isfinite(value[k]))
                {
                    return tl::unexpected(Fail("GeneralPcurveBasisBoundsUnestablished", basis));
                }
                box[0][k] = std::min(box[0][k], value[k]);
                box[1][k] = std::max(box[1][k], value[k]);
            }
        }
        const auto* boundSurface = surface.get();
        const auto* boundUv = uv.get();
        if (auto operation = Step(pcurve); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        std::unique_ptr<SmSurface> wrapper(new (*m_context) SmSTEPSurface(*surface, TRUE));
        surface.release();
        wrapper->SetContext(m_context);
        std::unique_ptr<SmCurve> composition(new (*m_context) SmCrvOnSurf(*uv, *wrapper, &allowed, 3, m_context));
        uv.release();
        wrapper.release();
        ParameterMapping map;
        map.kind = "SourcePcurveLineWindow";
        map.sourceGeometryId = Id(parameterCurve);
        map.sourceBasisGeometryId = Id(basis);
        map.sourceUseId = Id(use);
        map.equation =
            "native_tau in [0,1]; raw line generator q=" + RatioText(window.generatorWindowExact[0]) + "+" + RatioText(window.generatorSpanExact) +
            "*native_tau; source_t=q*sqrt(source_Dx^2+source_Dy^2)/source_VECTOR_magnitude; "
            "complete LINE/stored-binary-carrier-rectangle intersection, no inward crop; source decimal P/D and units retained in immutable snapshot; "
            "native endpoint conversion bounded continuously in mm on the stored carrier";
        map.parameters = { { "native_parameter_interval", { 0, 1 } },
                           { "raw_generator_interval_rounded", { window.generatorWindowRounded[0], window.generatorWindowRounded[1] } },
                           { "raw_generator_rounded_interval_collapsed", { window.generatorRoundingCollapsed ? 1. : 0. } },
                           { "native_uv_domain", { rectangle.begin(), rectangle.end() } },
                           { "maximum_uv_endpoint_rounding", { window.maximumRoundingUpperUv[0], window.maximumRoundingUpperUv[1] } },
                           { "maximum_lift_error_mm", { (*lift)->MaximumMm() } } };
        if (const auto derivatives = (*lift)->DerivativeBoundsMmPerUv())
        {
            map.parameters.push_back({ "whole_domain_derivative_bounds_mm_per_uv", { (*derivatives)[0], (*derivatives)[1] } });
        }
        const auto mappingIndex = m_report.parameterMappings.size();
        m_report.parameterMappings.push_back(map);
        if (definingMaster)
        {
            m_pcurveLines->bindings.push_back(
                { use,
                  std::make_shared<const PcurveLineWindow>(window),
                  *lift,
                  boundSurface,
                  boundUv,
                  static_cast<const SmCrvOnSurf*>(composition.get()),
                  std::move(map),
                  mappingIndex }
            );
        }
        ParameterMapping compositionMap;
        compositionMap.kind = "SourcePcurveComposition";
        compositionMap.sourceGeometryId = Id(pcurve);
        compositionMap.sourceBasisGeometryId = Id(basis);
        compositionMap.sourceUseId = Id(use);
        compositionMap.equation = "native C(tau)=basis_STEP(native_uv(tau)); source LINE parameter follows SourcePcurveLineWindow, not tau identity";
        compositionMap.parameters = { { "source_uv_to_native_step_scale", { 1, 1 } },
                                      { "source_uv_to_native_step_offset", { 0, 0 } },
                                      { "native_parameter_interval", { 0, 1 } } };
        m_report.parameterMappings.push_back(std::move(compositionMap));
        return PcurveDefinition{ std::move(composition), box, false };
    }
};
} // namespace
Result<PcurveDefinition> MakeSourceLinePcurve(const GeometryConstruction& inputs, std::size_t pcurve, std::size_t use, bool definingMaster)
{
    return LinePcurveBuilder(inputs).Make(pcurve, use, definingMaster);
}
} // namespace step_import::detail::construction
