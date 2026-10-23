// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportCurvePointConstruction.h"
#include "StepImportGeometryConstruction.h"

#include <SmLine.h>
#include <map>
#include <utility>

namespace step_import::detail
{
class OwnedCurvePointDefinitions::CurveDefinition
{
public:

    // Only the construction implementation can name this private type. It
    // registers an owned curve with its checked parameter/measurement record;
    // callers cannot default-create or separately edit either half.
    CurveDefinition(std::unique_ptr<SmCurve> native, WireframeCurveInfo info, std::optional<double> source, std::optional<double> endpoints)
        : native(std::move(native)), info(std::move(info)), source(source), endpoints(endpoints)
    {
    }
    CurveDefinition(CurveDefinition&&) noexcept = default;
    CurveDefinition& operator=(CurveDefinition&&) noexcept = default;
    CurveDefinition(const CurveDefinition&) = delete;
    CurveDefinition& operator=(const CurveDefinition&) = delete;
    std::unique_ptr<SmCurve> native;
    WireframeCurveInfo info;
    std::optional<double> source, endpoints;
};
OwnedCurvePointDefinitions::OwnedCurvePointDefinitions() noexcept = default;
OwnedCurvePointDefinitions::~OwnedCurvePointDefinitions() = default;
OwnedCurvePointDefinitions::OwnedCurvePointDefinitions(OwnedCurvePointDefinitions&&) noexcept = default;
OwnedCurvePointDefinitions& OwnedCurvePointDefinitions::operator=(OwnedCurvePointDefinitions&&) noexcept = default;
std::size_t OwnedCurvePointDefinitions::CurveCount() const
{
    return m_curves.size();
}
const SmCurve& OwnedCurvePointDefinitions::CurveAt(CurveIndex index) const
{
    return *m_curves.at(index.index).native;
}
std::span<const WireframePointInfo> OwnedCurvePointDefinitions::Points() const
{
    return m_points;
}

using namespace construction;
class CurvePointDefinitions::Implementation : public GeometryConstruction
{
public:

    explicit Implementation(const GeometryConstruction& inputs) : GeometryConstruction(inputs)
    {
        if (!m_source || !m_context)
        {
            throw std::logic_error("CurvePointDefinitionsRequireConstructionContext");
        }
    }
    enum class Role
    {
        Wireframe,
        Supporting
    };
    OwnedCurvePointDefinitions m_owned;
    std::map<Index, CurveIndex> m_curves;
    std::map<Index, PointIndex> m_points;
    bool m_finished = false;

    void CheckOpen() const
    {
        if (m_finished)
        {
            throw std::logic_error("CurvePointDefinitionsAlreadyFinished");
        }
    }
    const WireframePointInfo& PointDefinition(Index record)
    {
        if (const auto found = m_points.find(record); found != m_points.end())
        {
            return m_owned.m_points.at(found->second.index);
        }
        WireframePointInfo point{ Id(record), m_owned.m_points.size(), Position(record) };
        m_owned.m_points.push_back(point);
        m_points.emplace(record, PointIndex{ point.nativeIndex });
        return m_owned.m_points.back();
    }
    [[nodiscard]] Result<const OwnedCurvePointDefinitions::CurveDefinition*> CurveDefinition(Index record, Role role)
    {
        if (const auto found = m_curves.find(record); found != m_curves.end())
        {
            return &m_owned.m_curves.at(found->second.index);
        }
        auto index = AddCurve(record, role);
        if (!index)
        {
            return tl::unexpected(std::move(index.error()));
        }
        m_curves.emplace(record, *index);
        return &m_owned.m_curves.at(index->index);
    }
    [[nodiscard]] Result<CurveIndex> AddCurve(Index record, Role role)
    {
        const bool trimmed = Is(record, "TRIMMED_CURVE");
        const auto basis = trimmed ? Ref(record, "basis_curve") : record;
        double sourceConsistency = 0, definitionEndpoints = 0;
        const auto hasPointCut = [&](std::string_view name)
        {
            const auto& cuts = F(record, name).children;
            return std::any_of(
                cuts.begin(),
                cuts.end(),
                [](const auto& cut)
                {
                    return cut.kind == step_source::ValueKind::Reference;
                }
            );
        };
        const bool measuredSourceTrim = trimmed && (hasPointCut("trim_1") || hasPointCut("trim_2"));
        WireframeCurveInfo info{ Id(record), Id(basis), m_owned.m_curves.size(), {}, !trimmed || B(record, "sense_agreement") };
        std::unique_ptr<SmCurve> curve;
        ParameterMapping mapping;
        mapping.sourceGeometryId = Id(record);
        mapping.sourceBasisGeometryId = Id(basis);
        mapping.sourceUseId = Id(record);
        mapping.targetCurveIndex = info.nativeIndex;
        std::optional<std::array<V, 2>> expectedEnds;
        if (Is(basis, "LINE"))
        {
            // ISO 10303-42:2003 4.4.40 trim parameters follow the basis LINE:
            // P(t)=origin+t*magnitude*normalized_direction. t is dimensionless;
            // scale the VECTOR length once, never scale t by the length unit.
            const auto origin = Position(Ref(basis, "pnt"));
            const auto vector = Ref(basis, "dir");
            auto directionResult = Direction(Ref(vector, "orientation"));
            if (!directionResult)
            {
                return tl::unexpected(std::move(directionResult.error()));
            }
            const auto& direction = *directionResult;
            const auto magnitude = N(vector, "magnitude") * scale;
            if (auto businessCheck = Need(std::isfinite(magnitude) && magnitude > 0, "GeneralWireLineMagnitude", record); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            const auto evaluate = [&](double t)
            {
                return Add(origin, Mul(direction, magnitude * t));
            };
            const auto preferred = F(record, "master_representation").text;
            const auto cut = [&](std::string_view field) -> Result<double>
            {
                std::optional<double> parameter, pointParameter;
                std::optional<V> point;
                for (const auto& value : F(record, field).children)
                {
                    if (value.kind == step_source::ValueKind::Reference)
                    {
                        point = Position(m_source->ReferenceTarget(value));
                        pointParameter = Dot(Sub(*point, origin), direction) / magnitude;
                        const auto gap = Norm(Sub(evaluate(*pointParameter), *point));
                        sourceConsistency = std::max(sourceConsistency, gap);
                        if (auto businessCheck = Need(
                                std::isfinite(gap) && gap <= Q(m_options.quality.consistencyMm),
                                "GeneralWireTrimPointOffBasis",
                                record
                            );
                            !businessCheck)
                        {
                            return tl::unexpected(std::move(businessCheck.error()));
                        }
                    }
                    else
                    {
                        parameter = Number(value.children.at(0));
                    }
                }
                if (parameter && point)
                {
                    const auto gap = Norm(Sub(evaluate(*parameter), *point));
                    sourceConsistency = std::max(sourceConsistency, gap);
                    if (auto businessCheck = Need(
                            std::isfinite(gap) && gap <= Q(m_options.quality.consistencyMm),
                            "GeneralTrimmedRepresentationsConflict",
                            record
                        );
                        !businessCheck)
                    {
                        return tl::unexpected(std::move(businessCheck.error()));
                    }
                }
                return preferred == "CARTESIAN" && pointParameter ? *pointParameter : parameter ? *parameter : pointParameter.value();
            };
            auto firstCut = cut("trim_1");
            if (!firstCut)
            {
                return tl::unexpected(std::move(firstCut.error()));
            }
            auto lastCut = cut("trim_2");
            if (!lastCut)
            {
                return tl::unexpected(std::move(lastCut.error()));
            }
            const auto first = *firstCut, last = *lastCut;
            if (auto businessCheck = Need(info.sourceForwardInNative ? last > first : first > last, "GeneralWireLineTrimOrder", record);
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            const auto low = std::min(first, last), high = std::max(first, last);
            expectedEnds = std::array{ evaluate(low), evaluate(high) };
            for (const auto& point : *expectedEnds)
            {
                if (auto businessCheck = Need(
                        std::all_of(
                            point.begin(),
                            point.end(),
                            [](double v)
                            {
                                return std::isfinite(v);
                            }
                        ),
                        "GeneralWireLineRange",
                        record
                    );
                    !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
            }
            SmLine* raw = nullptr;
            if (auto operation = Step(record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto status = SmLine::CreateLineSegment(*m_context, 3, P((*expectedEnds)[0]), P((*expectedEnds)[1]), raw);
            curve.reset(raw);
            if (auto nativeStatusCheck = Good(status); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            mapping.kind = role == Role::Supporting ? "SupportingTrimmedLine" : "WireframeTrimmedLine";
            mapping.equation = "basis_t=lerp(basis_range,(native_t-native_begin)/(native_end-native_begin)); source_s=sign*(basis_t-trim_1)";
            mapping.parameters = { { "basis_trim_parameters", { first, last } },
                                   { "basis_range", { low, high } },
                                   { "vector_magnitude_mm", { magnitude } } };
        }
        else if (Is(basis, "CIRCLE") || Is(basis, "ELLIPSE"))
        {
            auto frameResult = Placement(Ref(basis, "position"));
            if (!frameResult)
            {
                return tl::unexpected(std::move(frameResult.error()));
            }
            const auto& frame = *frameResult;
            const bool circle = Is(basis, "CIRCLE");
            const auto rx = N(basis, circle ? "radius" : "semi_axis_1") * scale;
            const auto ry = circle ? rx : N(basis, "semi_axis_2") * scale;
            if (auto businessCheck = Need(
                    std::isfinite(rx) && std::isfinite(ry) && rx > SM_EFF_ZERO && ry > SM_EFF_ZERO,
                    "GeneralWireConicRadiusRange",
                    record
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            constexpr double tau = 2 * std::numbers::pi;
            ConicTrim trim{ 0, tau, 0, tau, true, {} };
            if (trimmed)
            {
                auto checked = TrimmedConic(record, frame, rx, ry, sourceConsistency);
                if (!checked)
                {
                    return tl::unexpected(std::move(checked.error()));
                }
                trim = std::move(*checked);
            }
            double low = trim.low, high = trim.high;
            const auto shift = tau * std::floor(low / tau);
            low -= shift;
            high -= shift;
            if (high > tau)
            {
                low -= tau;
                high -= tau;
            }
            const auto degrees = trim.full ? FullPeriodDegrees(low) :
                                             std::array<double, 2>{ low * 180 / std::numbers::pi, high * 180 / std::numbers::pi };
            if (auto businessCheck = Need(degrees[1] > degrees[0] && degrees[0] >= -360 && degrees[1] <= 360, "GeneralWireConicAngleRange", record);
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            auto created = MakeConic(basis, frame, rx, ry, SmExtent1d(degrees[0], degrees[1]), true);
            if (!created)
            {
                return tl::unexpected(std::move(created.error()));
            }
            curve = std::move(*created);
            const auto evaluate = [&](double angle)
            {
                return Add(frame.o, Add(Mul(frame.x, rx * std::cos(angle)), Mul(frame.y, ry * std::sin(angle))));
            };
            expectedEnds = std::array{ evaluate(trim.low), evaluate(trim.high) };
            mapping.kind = role == Role::Supporting ? "SupportingConicRationalParameter" : "WireframeConicRationalParameter";
            mapping.equation =
                "basis_angle_rad=atan2(dot(P(native_t)-origin,y)/ry,dot(P(native_t)-origin,x)/rx),unwrapped_to_basis_range_rad; native_t_is_not_angle";
            mapping.parameters = { { "basis_trim_rad", { trim.first, trim.last } },     { "basis_range_rad", { trim.low, trim.high } },
                                   { "native_chart_range_rad", { low, high } },         { "radii_mm", { rx, ry } },
                                   { "origin_mm", { frame.o.begin(), frame.o.end() } }, { "x", { frame.x.begin(), frame.x.end() } },
                                   { "y", { frame.y.begin(), frame.y.end() } } };
            if (trim.angleScale)
            {
                mapping.parameters.push_back({ "angle_parameter_to_rad", { *trim.angleScale } });
            }
        }
        else
        {
            auto created = MakeSplineCurve(basis, record);
            if (!created)
            {
                return tl::unexpected(std::move(created.error()));
            }
            curve = std::move(*created);
            mapping.kind = role == Role::Supporting ? "SupportingSplineIdentity" : "WireframeSplineIdentity";
            mapping.equation = "source_t=native_t; entire_source_natural_interval; defining_coefficients_unchanged";
        }
        if (auto businessCheck = Need(bool(curve) && curve->GetDim() == 3, "GeneralWireCurveAllocation", record); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        const auto domain = curve->GetNaturalInterval();
        info.parameterDomain = { domain.GetMin(), domain.GetMax() };
        if (auto businessCheck = Need(
                std::isfinite(domain.GetMin()) && std::isfinite(domain.GetMax()) && domain.GetMax() > domain.GetMin(),
                "GeneralWireNativeDomain",
                record
            );
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (expectedEnds)
        {
            for (Index i = 0; i < 2; ++i)
            {
                SmPoint3d actual;
                if (auto operation = Step(record); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (auto nativeStatusCheck = Good(curve->EvaluatePoint(info.parameterDomain[i], actual)); !nativeStatusCheck)
                {
                    return tl::unexpected(std::move(nativeStatusCheck.error()));
                }
                const auto gap = Norm(Sub(P(actual), (*expectedEnds)[i]));
                definitionEndpoints = std::max(definitionEndpoints, gap);
                if (auto businessCheck = Need(
                        std::isfinite(gap) && gap <= Q(m_options.quality.definitionMm),
                        "GeneralWireDefinitionEndpointGap",
                        record
                    );
                    !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
            }
        }
        mapping.parameters.push_back({ "native_natural_interval", { domain.GetMin(), domain.GetMax() } });
        mapping.parameters.push_back({ "source_forward_in_native", { info.sourceForwardInNative ? 1. : -1. } });
        // Keep first-use mapping order, including the legacy role-specific kind.
        // Both writes belong to the unpublished request: an allocation failure
        // unwinds its owner, rather than publishing a partially registered item.
        m_report.parameterMappings.push_back(std::move(mapping));
        const CurveIndex index{ info.nativeIndex };
        m_owned.m_curves.emplace_back(
            std::move(curve),
            std::move(info),
            measuredSourceTrim ? std::optional<double>(sourceConsistency) : std::nullopt,
            expectedEnds ? std::optional<double>(definitionEndpoints) : std::nullopt
        );
        return index;
    }

    [[nodiscard]] Result<void> Wire(SourceRecord source, WireframeInfo& group, WireframeEvidence& evidence)
    {
        CheckOpen();
        const auto record = source.index;
        const bool first = group.curves.empty() && group.points.empty();
        Bounds bounds;
        if (Is(record, "CARTESIAN_POINT"))
        {
            const auto& point = PointDefinition(record);
            group.points.push_back(point);
            bounds = { point.pointMm, point.pointMm };
        }
        else
        {
            auto resolved = CurveDefinition(record, Role::Wireframe);
            if (!resolved)
            {
                return tl::unexpected(std::move(resolved.error()));
            }
            const auto& definition = **resolved;
            evidence.m_source = std::max(evidence.m_source, definition.source.value_or(0));
            evidence.m_endpoints = std::max(evidence.m_endpoints, definition.endpoints.value_or(0));
            group.curves.push_back(definition.info);
            SmExtent3d box;
            // This belongs to each source USE, including cache hits. Moving it
            // into definition construction would change native-call/cancel order.
            if (auto operation = Step(record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(definition.native->CalculateTightBoundingBox(
                    SmExtent1d(definition.info.parameterDomain[0], definition.info.parameterDomain[1]),
                    &box
                ));
                !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            bounds = { P(box.GetMin()), P(box.GetMax()) };
        }
        for (Index axis = 0; axis < 3; ++axis)
        {
            if (!(std::isfinite(bounds[0][axis]) && std::isfinite(bounds[1][axis]) && bounds[0][axis] <= bounds[1][axis]))
            {
                // The existing diagnostic identifies the containing source set.
                const auto* set = m_source->Source().Find(group.setId);
                if (!set)
                {
                    throw std::logic_error("CurvePointDefinitionsUnknownSourceSet");
                }
                const auto& records = m_source->Source().Records();
                return tl::unexpected(Fail("GeneralWireFiniteBounds", static_cast<Index>(set - records.data())));
            }
            group.boundsMm[0][axis] = first ? bounds[0][axis] : std::min(group.boundsMm[0][axis], bounds[0][axis]);
            group.boundsMm[1][axis] = first ? bounds[1][axis] : std::max(group.boundsMm[1][axis], bounds[1][axis]);
        }
        return {};
    }
    [[nodiscard]] Result<SupportingAppendResult> Support(SourceRecord source, SupportingGeometryInfo& group, SupportingEvidence& evidence)
    {
        CheckOpen();
        if (Is(source.index, "CARTESIAN_POINT"))
        {
            group.points.push_back(PointDefinition(source.index));
        }
        else if (Is(source.index, "AXIS2_PLACEMENT_3D") || Is(source.index, "PLANE"))
        {
            return SupportingAppendResult::ExistingPlacement;
        }
        else
        {
            auto resolved = CurveDefinition(source.index, Role::Supporting);
            if (!resolved)
            {
                return tl::unexpected(std::move(resolved.error()));
            }
            const auto& definition = **resolved;
            if (definition.source)
            {
                evidence.m_source = std::max(evidence.m_source.value_or(0), *definition.source);
            }
            if (definition.endpoints)
            {
                evidence.m_endpoints = std::max(evidence.m_endpoints.value_or(0), *definition.endpoints);
            }
            group.curves.push_back(definition.info);
        }
        return SupportingAppendResult::Appended;
    }
};
CurvePointDefinitions::CurvePointDefinitions(const GeometryConstruction& inputs) : m_implementation(std::make_unique<Implementation>(inputs))
{
}
CurvePointDefinitions::~CurvePointDefinitions() = default;
[[nodiscard]] Result<void> CurvePointDefinitions::Append(SourceRecord record, WireframeInfo& group, WireframeEvidence& evidence)
{
    if (auto operation = m_implementation->Wire(record, group, evidence); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    return {};
}
[[nodiscard]] Result<SupportingAppendResult> CurvePointDefinitions::Append(
    SourceRecord record,
    SupportingGeometryInfo& group,
    SupportingEvidence& evidence
)
{
    return m_implementation->Support(record, group, evidence);
}
OwnedCurvePointDefinitions CurvePointDefinitions::Finish() &&
{
    m_implementation->CheckOpen();
    m_implementation->m_finished = true;
    // No allocation, native operation or report mutation on successful transfer.
    // Rvalue qualification is not linear typing: CheckOpen rejects a second call.
    return std::move(m_implementation->m_owned);
}
} // namespace step_import::detail
