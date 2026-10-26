// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportTerminalDomainConstruction.h"

#include "StepImportGeometryConstruction.h"
#include "StepImportRevolutionFrameProof.h"
#include "StepImportSourceParameterUnits.h"
#include "StepImportTerminalSpline.h"
#include "StepImportUnits.h"

#include <SmBSplineSurface.h>
#include <SmSurfOfRevolution.h>
#include <cstdint>
#include <map>

namespace step_import::detail::construction
{
namespace ts = terminal_spline;
struct TerminalDomainCache
{
    struct Entry
    {
        std::size_t plan = 0;
        ts::Source source;
        ts::Prepared prepared;
        std::array<std::string, 4> originalDomain;
        std::optional<ts::BinaryNet> verifiedNet;
        std::optional<double> verifiedError;
        bool revolution = false;
        bool originalDomainGuard = false;
    };
    // Numeric definitions/proofs only: no native object may outlive a candidate
    // context through this shared construction cache. Helpers copy the holder.
    std::map<std::size_t, Entry> entries;
};
std::shared_ptr<TerminalDomainCache> MakeTerminalDomainCache()
{
    return std::make_shared<TerminalDomainCache>();
}
namespace
{
class Builder : private GeometryConstruction
{
public:

    explicit Builder(const GeometryConstruction& inputs) : GeometryConstruction(inputs)
    {
    }
    [[nodiscard]] Result<std::optional<std::unique_ptr<SmSurface>>> Make(Index geometry, Index use, double angularStart)
    {
        if (!m_options.terminalDomainInterpretation)
        {
            return std::optional<std::unique_ptr<SmSurface>>{};
        }
        const auto& request = *m_options.terminalDomainInterpretation;
        const auto selected = std::find_if(
            request.surfaces.begin(),
            request.surfaces.end(),
            [&](const auto& plan)
            {
                return plan.surfaceId == Id(geometry);
            }
        );
        if (selected == request.surfaces.end())
        {
            return std::optional<std::unique_ptr<SmSurface>>{};
        }
        const auto planIndex = std::size_t(selected - request.surfaces.begin());
        const auto charge = [&](std::size_t units)
        {
            return m_execution.SpendTerminalDomainWork(units);
        };
        const ts::Limits limits{ request.maximumProofControls, 4096, 1234 };
        auto found = m_terminalDomainCache->entries.find(geometry);
        if (found == m_terminalDomainCache->entries.end())
        {
            auto initialized = Prepare(geometry, planIndex, *selected, limits, charge);
            if (!initialized)
            {
                return tl::unexpected(std::move(initialized.error()));
            }
            found = m_terminalDomainCache->entries.emplace(geometry, std::move(*initialized)).first;
        }
        auto& entry = found->second;
        const auto& canonical = entry.prepared.target;
        std::unique_ptr<SmSurface> result;
        SmBSplineCurve* generator = nullptr;
        SmPoint3d actualOrigin;
        SmVector3d actualAxis;
        std::array<std::string, 3> sourceAxis{ "0", "0", "1" }, sourceOrigin;
        Point parsedAxis{ 0, 0, 1 };
        std::array<Point, 3> actualFrame;
        std::optional<double> angleToRadians;
        if (entry.revolution)
        {
            auto resolvedAngle = AngleToRadians();
            if (!resolvedAngle)
            {
                return tl::unexpected(std::move(resolvedAngle.error()));
            }
            angleToRadians = *resolvedAngle;
            if (!angleToRadians || !std::isfinite(*angleToRadians) || !(*angleToRadians > 0))
            {
                return tl::unexpected(Fail("GeneralRevolutionAngleUnitUnestablished", geometry, Status::Unsupported));
            }
            auto curve = ConstructCurve(canonical, geometry);
            if (!curve)
            {
                return tl::unexpected(std::move(curve.error()));
            }
            const auto placement = Ref(geometry, "axis_position");
            auto origin = Position(Ref(placement, "location"));
            if (!origin)
            {
                return tl::unexpected(std::move(origin.error()));
            }
            V axis{ 0, 0, 1 };
            if (F(placement, "axis").kind != step_source::ValueKind::Unset)
            {
                const auto directionRecord = Ref(placement, "axis");
                const auto& ratios = F(directionRecord, "direction_ratios").children;
                auto rawAxis = Triple(F(directionRecord, "direction_ratios"));
                if (!rawAxis)
                {
                    return tl::unexpected(std::move(rawAxis.error()));
                }
                parsedAxis = *rawAxis;
                sourceAxis = { ratios[0].text, ratios[1].text, ratios[2].text };
                auto direction = Direction(directionRecord);
                if (!direction)
                {
                    return tl::unexpected(std::move(direction.error()));
                }
                axis = *direction;
            }
            if (!std::isfinite(angularStart) || angularStart < -360 || angularStart > 0)
            {
                return tl::unexpected(Fail("TerminalDomainAngularChartUnestablished", geometry, Status::Unsupported));
            }
            if (auto operation = Step(geometry); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            SmSurfOfRevolution* raw = nullptr;
            const auto status = SmSurfOfRevolution::CreateCanonical(*m_context, curve->get(), P(*origin), D(axis), raw, FALSE);
            std::unique_ptr<SmSurfOfRevolution> surface(raw);
            if (surface && (*curve)->GetOwner() == surface.get())
            {
                curve->release();
            }
            if (auto good = Good(status); !good)
            {
                return tl::unexpected(std::move(good.error()));
            }
            if (!surface || *curve)
            {
                return tl::unexpected(Fail("TerminalDomainGeneratorOwnership", geometry));
            }
            const auto& k = canonical.basis.knotsU;
            const double begin = k[canonical.basis.degreeU], end = k[canonical.basis.countU];
            if (angularStart != 0)
            {
                if (auto operation = Step(geometry); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (auto good = Good(surface->AdjustSTEPUVDomain(SmExtent2d(angularStart, begin, angularStart + 360, end))); !good)
                {
                    return tl::unexpected(std::move(good.error()));
                }
            }
            SmBoolean swapped = TRUE;
            if (auto good = Good(surface->GetCanonical(generator, actualOrigin, actualAxis, &swapped)); !good)
            {
                return tl::unexpected(std::move(good.error()));
            }
            if (!generator || swapped)
            {
                return tl::unexpected(Fail("TerminalDomainAxisRoundtripUnestablished", geometry, Status::Indeterminate));
            }
            const auto& point = F(Ref(placement, "location"), "coordinates").children;
            sourceOrigin = { point[0].text, point[1].text, point[2].text };
            const auto& x = surface->GetXAxis();
            const auto& y = surface->GetYAxis();
            actualFrame = { Point{ x.x, x.y, x.z }, Point{ y.x, y.y, y.z }, Point{ actualAxis.x, actualAxis.y, actualAxis.z } };
            result = std::move(surface);
        }
        else
        {
            auto surface = ConstructSurface(canonical, geometry);
            if (!surface)
            {
                return tl::unexpected(std::move(surface.error()));
            }
            result = std::move(*surface);
        }
        // Certify the ACTUAL final net, including any native construction/chart
        // work. No Euclidean divide/multiply is allowed to hide stored error.
        auto actual = entry.revolution ? Read(*generator, geometry) : Read(*static_cast<SmBSplineSurface*>(result.get()), geometry);
        if (!actual)
        {
            return tl::unexpected(std::move(actual.error()));
        }
        if (!entry.verifiedNet || !Same(*entry.verifiedNet, *actual))
        {
            const auto q = m_options.quality.definitionMm;
            ts::IntegerRatio allowance{ std::to_string(q.Numerator()),
                                        std::to_string(static_cast<std::uint64_t>(q.Denominator()) * (entry.revolution ? 2 : 1)) };
            auto proof = ts::Certify(entry.prepared.reference, *actual, allowance, limits, charge);
            if (!proof)
            {
                return tl::unexpected(std::move(proof.error()));
            }
            if (!proof->established || !proof->maximumMm)
            {
                return tl::unexpected(Fail("TerminalDomainWholeNewDomainProofUnestablished", geometry, Status::Indeterminate));
            }
            entry.verifiedNet = *actual;
            entry.verifiedError = *proof->maximumMm;
        }
        double maximum = *entry.verifiedError;
        if (entry.revolution)
        {
            auto frameBound = TerminalRevolutionFrameBound(
                sourceAxis,
                parsedAxis,
                sourceOrigin,
                entry.source.lengthToMm,
                P(actualOrigin),
                actualFrame,
                *actual,
                limits,
                charge
            );
            if (!frameBound)
            {
                return tl::unexpected(std::move(frameBound.error()));
            }
            // The generator certificate uses half of the ONE definition
            // allowance. The whole-new-domain frame bound must fit the rest.
            const double half = std::nextafter(Q(m_options.quality.definitionMm) / 2, 0.);
            const double frame = *frameBound;
            if (frame > half)
            {
                return tl::unexpected(Fail("TerminalDomainFrameBudgetUnestablished", geometry, Status::Indeterminate));
            }
            if (frame != 0)
            {
                maximum = std::nextafter(maximum + frame, INFINITY);
            }
        }
        TerminalDomainInterpretationAction action;
        action.sourceSurfaceId = Id(geometry);
        action.sourceUseId = Id(use);
        action.originalSourceDomain = entry.originalDomain;
        if (entry.revolution)
        {
            action.sourceGeneratorId = Id(Ref(geometry, "swept_curve"));
        }
        for (const auto& role : m_source->PartDefinition().roles)
        {
            if (role.name == "product_definition")
            {
                action.sourceDefinitionId = Id(role.recordIndex);
            }
        }
        const auto domain = result->GetSTEPUVDomain();
        action.targetNativeDomain = { domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y };
        action.wholeNewDomainErrorUpperMm = maximum;
        if (entry.originalDomainGuard)
        {
            action.originalDomainDiagnostic = "GeneralPcurveBasisDomainUnestablished";
        }
        if (auto recorded = m_execution.RecordTerminalDomainAction(entry.plan, std::move(action)); !recorded)
        {
            return tl::unexpected(std::move(recorded.error()));
        }
        ParameterMapping mapping;
        mapping.kind = entry.revolution ? "RevolutionSourceParameters" : "TerminalDomainSourceParameters";
        mapping.sourceGeometryId = Id(geometry);
        mapping.sourceUseId = Id(use);
        mapping.equation = "source parameter identity retained in explicitly interpreted terminal NURBS domain; source definition and UV immutable";
        mapping.parameters = { { "interpreted_native_domain", { domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y } },
                               { "whole_new_domain_error_upper_mm", { maximum } } };
        if (entry.revolution)
        {
            mapping.sourceBasisGeometryId = Id(Ref(geometry, "swept_curve"));
            mapping.equation += "; source revolution U in bound physical angle units -> native STEP degrees; native ordinary UV is rational";
            mapping.parameters.push_back({ "source_angle_to_native_degrees", { *angleToRadians * 180 / std::numbers::pi } });
        }
        m_report.parameterMappings.push_back(std::move(mapping));
        return std::optional<std::unique_ptr<SmSurface>>(std::move(result));
    }

private:

    [[nodiscard]] Result<ts::IntegerRatio> LengthScale()
    {
        if (const auto power = m_source->UnitContext().lengthPower10Mm)
        {
            if (*power >= 0)
            {
                return ts::IntegerRatio{ "1" + std::string(std::size_t(*power), '0'), "1" };
            }
            return ts::IntegerRatio{ "1", "1" + std::string(std::size_t(-*power), '0') };
        }
        auto converted = ResolveLengthScale(
            *m_source,
            m_source->UnitContext().lengthUnit,
            [&]
            {
                return m_execution.Check(Phase::BeforeNativeCall);
            }
        );
        if (!converted)
        {
            return tl::unexpected(std::move(converted.error()));
        }
        return ts::IntegerRatio{ std::to_string(converted->scaleMm.Numerator()), std::to_string(converted->scaleMm.Denominator()) };
    }
    [[nodiscard]] Result<ts::SourceAxis> Axis(
        Index geometry,
        const char* degree,
        const char* knots,
        const char* multiplicities,
        const std::optional<std::array<std::string, 2>>& requested
    )
    {
        auto p = Degree(geometry, degree);
        if (!p)
        {
            return tl::unexpected(std::move(p.error()));
        }
        auto m = Multiplicities(geometry, multiplicities);
        if (!m)
        {
            return tl::unexpected(std::move(m.error()));
        }
        ts::SourceAxis axis;
        axis.degree = *p;
        axis.requested = requested;
        for (const auto& value : F(geometry, knots).children)
        {
            axis.knots.push_back(value.text);
        }
        for (ULONG i = 0; i < m->GetSize(); ++i)
        {
            axis.multiplicities.push_back((*m)[i]);
        }
        return axis;
    }
    [[nodiscard]] Result<TerminalDomainCache::Entry> Prepare(
        Index geometry,
        Index planIndex,
        const TerminalDomainSurfacePlan& plan,
        const ts::Limits& limits,
        const ts::Charge& charge
    )
    {
        TerminalDomainCache::Entry entry;
        entry.plan = planIndex;
        entry.revolution = Is(geometry, "SURFACE_OF_REVOLUTION");
        if (!entry.revolution && !Is(geometry, "B_SPLINE_SURFACE_WITH_KNOTS"))
        {
            return tl::unexpected(Fail("TerminalDomainBasisUnsupported", geometry, Status::Unsupported));
        }
        if (entry.revolution && plan.u)
        {
            return tl::unexpected(Fail("TerminalDomainRevolutionUUnsupported", geometry, Status::Unsupported));
        }
        const auto defining = entry.revolution ? Ref(geometry, "swept_curve") : geometry;
        if (entry.revolution && !Is(defining, "B_SPLINE_CURVE_WITH_KNOTS"))
        {
            return tl::unexpected(Fail("TerminalDomainGeneratorUnsupported", defining, Status::Unsupported));
        }
        auto& source = entry.source;
        auto length = LengthScale();
        if (!length)
        {
            return tl::unexpected(std::move(length.error()));
        }
        source.lengthToMm = std::move(*length);
        auto u = Axis(
            defining,
            entry.revolution ? "degree" : "u_degree",
            entry.revolution ? "knots" : "u_knots",
            entry.revolution ? "knot_multiplicities" : "u_multiplicities",
            entry.revolution ? plan.v : plan.u
        );
        if (!u)
        {
            return tl::unexpected(std::move(u.error()));
        }
        source.u = std::move(*u);
        if (!entry.revolution)
        {
            auto v = Axis(defining, "v_degree", "v_knots", "v_multiplicities", plan.v);
            if (!v)
            {
                return tl::unexpected(std::move(v.error()));
            }
            source.v = std::move(*v);
        }
        const auto appendPoint = [&](Index point) -> Result<void>
        {
            const auto& values = F(point, "coordinates").children;
            if (values.size() != 3)
            {
                return tl::unexpected(Fail("TerminalDomainSourceControlDimension", point));
            }
            source.controls.push_back({ values[0].text, values[1].text, values[2].text });
            return {};
        };
        if (entry.revolution)
        {
            const auto points = Refs(defining, "control_points_list");
            source.countU = points.size();
            if (source.countU > limits.controlSlots)
            {
                return tl::unexpected(Fail("TerminalDomainSourceControlLimit", geometry, Status::ResourceLimit));
            }
            for (const auto point : points)
            {
                if (auto appended = appendPoint(point); !appended)
                {
                    return tl::unexpected(std::move(appended.error()));
                }
            }
            if (Is(defining, "RATIONAL_B_SPLINE_CURVE"))
            {
                for (const auto& value : F(defining, "weights_data").children)
                {
                    source.weights.push_back(value.text);
                }
            }
            entry.originalDomain = { "0", source.u.knots.front(), "FullTurn", source.u.knots.back() };
        }
        else
        {
            const auto& rows = F(defining, "control_points_list").children;
            source.countU = rows.size();
            source.countV = rows.at(0).children.size();
            if (!source.countV || source.countU > limits.controlSlots / source.countV)
            {
                return tl::unexpected(Fail("TerminalDomainSourceControlLimit", geometry, Status::ResourceLimit));
            }
            for (const auto& row : rows)
            {
                for (const auto& point : row.children)
                {
                    if (auto appended = appendPoint(m_source->ReferenceTarget(point)); !appended)
                    {
                        return tl::unexpected(std::move(appended.error()));
                    }
                }
            }
            if (Is(defining, "RATIONAL_B_SPLINE_SURFACE"))
            {
                for (const auto& row : F(defining, "weights_data").children)
                {
                    for (const auto& value : row.children)
                    {
                        source.weights.push_back(value.text);
                    }
                }
            }
            entry.originalDomain = { source.u.knots.front(), source.v->knots.front(), source.u.knots.back(), source.v->knots.back() };
        }
        std::array<std::vector<std::string>, 2> parameters;
        bool extending = false;
        for (const auto* data : { &source.u, source.v ? &*source.v : nullptr })
        {
            if (!data || !data->requested)
            {
                continue;
            }
            const std::array<std::string, 2> original{ data->knots.front(), data->knots.back() };
            const std::vector<std::string> wanted{ (*data->requested)[0], (*data->requested)[1] };
            auto inside = ts::CheckScalarCoverage(original, wanted, limits, charge);
            if (!inside)
            {
                return tl::unexpected(std::move(inside.error()));
            }
            extending = extending || !*inside;
        }
        if (!extending)
        {
            return tl::unexpected(Fail("TerminalDomainPlanNotExtending", geometry, Status::Unsupported));
        }
        // Named bindings remain the sole STEP field interpretation. Inverse
        // ownership/type/unit prerequisites were already checked by the source
        // layer; the selected plan must still name its actual basis and curves.
        for (const auto& id : plan.pcurveIds)
        {
            const auto record = m_source->Source().Find(id);
            if (!record)
            {
                return tl::unexpected(Fail("TerminalDomainPcurveUnavailable", geometry, Status::Unsupported));
            }
            const auto pcurve = std::size_t(record - m_source->Source().Records().data());
            if (!Is(pcurve, "PCURVE") || Ref(pcurve, "basis_surface") != geometry)
            {
                return tl::unexpected(Fail("TerminalDomainPcurveBasisMismatch", pcurve, Status::Unsupported));
            }
            const auto curve = Refs(Ref(pcurve, "reference_to_curve"), "items").at(0);
            if (!Is(curve, "B_SPLINE_CURVE_WITH_KNOTS") || Is(curve, "RATIONAL_B_SPLINE_CURVE"))
            {
                return tl::unexpected(Fail("TerminalDomainParameterCurveUnsupported", curve, Status::Unsupported));
            }
            for (const auto control : Refs(curve, "control_points_list"))
            {
                const auto& values = F(control, "coordinates").children;
                if (values.size() != 2)
                {
                    return tl::unexpected(Fail("TerminalDomainParameterDimension", control));
                }
                parameters[0].push_back(values[0].text);
                parameters[1].push_back(values[1].text);
                if (parameters[0].size() > limits.controlSlots)
                {
                    return tl::unexpected(Fail("TerminalDomainParameterControlLimit", geometry, Status::ResourceLimit));
                }
            }
        }
        for (std::size_t axis = entry.revolution ? 1 : 0; axis < 2; ++axis)
        {
            const auto& data = entry.revolution ? source.u : axis == 0 ? source.u : *source.v;
            const auto range = data.requested.value_or(std::array<std::string, 2>{ data.knots.front(), data.knots.back() });
            auto covered = ts::CheckScalarCoverage(range, parameters[axis], limits, charge);
            if (!covered)
            {
                return tl::unexpected(std::move(covered.error()));
            }
            if (!*covered)
            {
                return tl::unexpected(Fail("TerminalDomainPlanDoesNotCoverPcurves", geometry, Status::Unsupported));
            }
            const std::array<std::string, 2> oldRange{ data.knots.front(), data.knots.back() };
            auto oldCoverage = ts::CheckScalarCoverage(oldRange, parameters[axis], limits, charge);
            if (!oldCoverage)
            {
                return tl::unexpected(std::move(oldCoverage.error()));
            }
            entry.originalDomainGuard = entry.originalDomainGuard || !*oldCoverage;
        }
        auto prepared = ts::Extend(source, limits, charge);
        if (!prepared)
        {
            return tl::unexpected(std::move(prepared.error()));
        }
        entry.prepared = std::move(*prepared);
        return entry;
    }
    static void Knots(const std::vector<double>& expanded, SmTArray<double>& unique, SmTArray<ULONG>& mult)
    {
        for (const auto value : expanded)
        {
            if (!unique.GetSize() || unique[unique.GetSize() - 1] != value)
            {
                unique.Add(value);
                mult.Add(1);
            }
            else
            {
                ++mult[mult.GetSize() - 1];
            }
        }
    }
    [[nodiscard]] Result<std::unique_ptr<SmBSplineCurve>> ConstructCurve(const ts::Canonical& data, Index record)
    {
        SmTArray<SmPoint3d> points;
        SmTArray<double> weights, knots;
        SmTArray<ULONG> mult;
        for (const auto& p : data.controlsMm)
        {
            points.Add(P(p));
        }
        for (const auto w : data.weights)
        {
            weights.Add(w);
        }
        Knots(data.basis.knotsU, knots, mult);
        if (auto operation = Step(record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        SmBSplineCurve* raw = nullptr;
        const auto status = SmBSplineCurve::CreateCanonical(
            *m_context,
            3,
            data.basis.degreeU,
            points,
            SM_CF_UNSPECIFIED,
            mult,
            knots,
            SM_KT_UNSPECIFIED,
            weights.GetSize() ? &weights : nullptr,
            nullptr,
            raw
        );
        std::unique_ptr<SmBSplineCurve> result(raw);
        if (auto good = Good(status); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (!result)
        {
            return tl::unexpected(Fail("TerminalDomainCurveAllocation", record));
        }
        return result;
    }
    [[nodiscard]] Result<std::unique_ptr<SmSurface>> ConstructSurface(const ts::Canonical& data, Index record)
    {
        SmTArray<SmPoint3d> points;
        SmTArray<double> weights, uk, vk;
        SmTArray<ULONG> um, vm;
        for (const auto& p : data.controlsMm)
        {
            points.Add(P(p));
        }
        for (const auto w : data.weights)
        {
            weights.Add(w);
        }
        Knots(data.basis.knotsU, uk, um);
        Knots(data.basis.knotsV, vk, vm);
        if (auto operation = Step(record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        SmBSplineSurface* raw = nullptr;
        const auto status = SmBSplineSurface::CreateCanonical(
            *m_context,
            data.basis.degreeU,
            data.basis.degreeV,
            points,
            SM_SF_UNSPECIFIED,
            um,
            vm,
            uk,
            vk,
            SM_KT_UNSPECIFIED,
            weights.GetSize() ? &weights : nullptr,
            nullptr,
            raw
        );
        std::unique_ptr<SmSurface> result(raw);
        if (auto good = Good(status); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (!result)
        {
            return tl::unexpected(Fail("TerminalDomainSurfaceAllocation", record));
        }
        return result;
    }
    [[nodiscard]] Result<ts::BinaryNet> Read(SmBSplineCurve& curve, Index record)
    {
        if (auto operation = Step(record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        ts::BinaryNet result;
        result.basis.degreeU = curve.GetDegree();
        ULONG count = 0;
        double* controls = nullptr;
        SmTArray<double> knots;
        if (auto good = Good(curve.GetControlPointsPointer(count, controls)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (auto good = Good(curve.GetKnotsAll(knots)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (!controls)
        {
            return tl::unexpected(Fail("TerminalDomainStoredCurveMissing", record));
        }
        result.basis.countU = count;
        result.basis.knotsU.assign(knots.GetDataArray(), knots.GetDataArray() + knots.GetSize());
        for (ULONG i = 0; i < count; ++i)
        {
            result.homogeneous.push_back({ controls[4 * i], controls[4 * i + 1], controls[4 * i + 2], curve.IsRational() ? controls[4 * i + 3] : 1 });
        }
        return result;
    }
    [[nodiscard]] Result<ts::BinaryNet> Read(SmBSplineSurface& surface, Index record)
    {
        if (auto operation = Step(record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        ts::BinaryNet result;
        result.basis.degreeU = surface.GetDegree(SM_SP_U);
        result.basis.degreeV = surface.GetDegree(SM_SP_V);
        ULONG u = 0, v = 0, ku = 0, kv = 0;
        double *controls = nullptr, *uk = nullptr, *vk = nullptr;
        if (auto good = Good(surface.GetControlPointsPointer(u, v, controls)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (auto good = Good(surface.GetKnotsPointers(ku, kv, uk, vk)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (!controls || !uk || !vk)
        {
            return tl::unexpected(Fail("TerminalDomainStoredSurfaceMissing", record));
        }
        result.basis.countU = u;
        result.basis.countV = v;
        result.basis.knotsU.assign(uk, uk + ku);
        result.basis.knotsV.assign(vk, vk + kv);
        for (std::size_t i = 0; i < std::size_t(u) * v; ++i)
        {
            result.homogeneous.push_back(
                { controls[4 * i], controls[4 * i + 1], controls[4 * i + 2], surface.IsRational() ? controls[4 * i + 3] : 1 }
            );
        }
        return result;
    }
    static bool Same(const ts::BinaryNet& a, const ts::BinaryNet& b)
    {
        return a.basis.degreeU == b.basis.degreeU && a.basis.degreeV == b.basis.degreeV && a.basis.countU == b.basis.countU &&
               a.basis.countV == b.basis.countV && a.basis.knotsU == b.basis.knotsU && a.basis.knotsV == b.basis.knotsV &&
               a.homogeneous == b.homogeneous;
    }
};
} // namespace
Result<std::optional<std::unique_ptr<SmSurface>>> TryTerminalDomainSurface(
    const GeometryConstruction& inputs,
    std::size_t surface,
    std::size_t use,
    double angularStart
)
{
    return Builder(inputs).Make(surface, use, angularStart);
}
} // namespace step_import::detail::construction
