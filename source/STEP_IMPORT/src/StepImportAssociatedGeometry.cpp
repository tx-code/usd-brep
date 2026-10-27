// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportAssociatedGeometry.h"

#include "StepImportCurvePointConstruction.h"
#include "StepImportGeometryConstruction.h"
#include "StepImportNativeData.h"
#include "StepImportUnits.h"

#include <SmPlane.h>
#include <cfenv>
namespace step_import::detail
{
namespace
{
using namespace construction;
class AssociatedBuilder : private GeometryConstruction
{
public:

    explicit AssociatedBuilder(const GeometryConstruction& inputs) : GeometryConstruction(inputs)
    {
        Locate();
    }
    AssociatedBuilder(std::shared_ptr<const step_semantics::Snapshot> source, const Options& options, Report& report, Execution& execution)
        : GeometryConstruction(std::move(source), options, report, execution)
    {
        Locate();
    }
    Result<void> Check()
    {
        if (supportingRepresentation)
        {
            for (const auto geometry : Refs(*supportingRepresentation, "items"))
            {
                if (!Is(geometry, "CARTESIAN_POINT") && !Is(geometry, "AXIS2_PLACEMENT_3D") && !Is(geometry, "PLANE") &&
                    !SupportsDetachedCurve(*m_source, geometry))
                {
                    return tl::unexpected(Fail("GeneralSupportingGeometryUnsupported", geometry, Status::Unsupported));
                }
            }
        }
        if (wireframeRepresentation)
        {
            for (const auto set : Refs(*wireframeRepresentation, "items"))
            {
                if (Is(set, "AXIS2_PLACEMENT_3D"))
                {
                    continue;
                }
                for (const auto geometry : Refs(set, "elements"))
                {
                    if (!Is(geometry, "CARTESIAN_POINT") && !SupportsDetachedCurve(*m_source, geometry))
                    {
                        return tl::unexpected(Fail("GeneralWireframeGeometryUnsupported", geometry, Status::Unsupported));
                    }
                }
            }
        }
        return {};
    }
    Result<void> Populate(ModelData& model)
    {
        if (!model.context || model.source != m_source || !m_context || m_context != model.context.get())
        {
            throw std::logic_error("AssociatedGeometryContextMismatch");
        }
        m_model = &model;
        // One definition registry serves both roles; shared source geometry is stored
        // once in this owner, never duplicated onto an arbitrary material body.
        CurvePointDefinitions definitions(*this);
        if (auto r = BuildWireframes(definitions); !r)
        {
            return tl::unexpected(std::move(r.error()));
        }
        if (auto r = BuildSupportingGeometry(definitions); !r)
        {
            return tl::unexpected(std::move(r.error()));
        }
        model.curvePointDefinitions = std::move(definitions).Finish();
        return {};
    }
    Result<std::shared_ptr<ModelData>> MakePartOwner()
    {
        if (!wireframeRepresentation && !supportingRepresentation)
        {
            return std::shared_ptr<ModelData>{};
        }
        if (auto r = Check(); !r)
        {
            return tl::unexpected(std::move(r.error()));
        }
        if (m_options.faceOrientationRepair || m_options.cylindricalBandRepair || m_options.conicalApexRepair)
        {
            return tl::unexpected(Fail("CollectionAssociatedRepairUnestablished", Role("representation"), Status::Unsupported));
        }
        if (!std::numeric_limits<double>::is_iec559 || std::fegetround() != FE_TONEAREST)
        {
            return tl::unexpected(Fail("UnsupportedNativeNumericEnvironment", Role("representation"), Status::Indeterminate));
        }
        auto units = ResolveLengthScale(
            *m_source,
            m_source->UnitContext().lengthUnit,
            [&]
            {
                return m_execution.Check(Phase::BeforeNativeCall);
            }
        );
        if (!units)
        {
            return tl::unexpected(std::move(units.error()));
        }
        scale = Q(units->scaleMm);
        if (!std::isfinite(scale) || !(scale > 0))
        {
            return tl::unexpected(Fail("GeneralUnitScaleRange", Role("representation"), Status::Indeterminate));
        }
        auto model = std::make_shared<ModelData>();
        model->source = m_source;
        model->queryTolerance = Q(m_options.quality.queryMm);
        model->queryBudget = m_options.quality.queryMm;
        model->definitionToleranceMm = Q(m_options.quality.definitionMm);
        model->info.shapeRole = "AssociatedPartGeometry";
        model->lengthUnits = UnitNormalization{ Id(m_source->UnitContext().lengthUnit), units->scaleMm, {}, std::move(units->definitions) };
        m_report.units = model->lengthUnits;
        if (auto r = Step(Role("representation")); !r)
        {
            return tl::unexpected(std::move(r.error()));
        }
        model->context = std::make_unique<SmContext>();
        m_context = model->context.get();
        if (auto r = Populate(*model); !r)
        {
            return tl::unexpected(std::move(r.error()));
        }
        return model;
    }

private:

    ModelData* m_model = nullptr;
    std::optional<Index> wireframeRepresentation, supportingRepresentation;
    Index Role(std::string_view name) const
    {
        for (const auto& r : m_source->PartDefinition().roles)
        {
            if (r.name == name)
            {
                return r.recordIndex;
            }
        }
        throw std::logic_error("AssociatedSourceRoleMissing");
    }
    void Locate()
    {
        for (const auto& r : m_source->PartDefinition().roles)
        {
            if (r.name == "wireframe_representation")
            {
                wireframeRepresentation = r.recordIndex;
            }
            if (r.name == "construction_representation")
            {
                supportingRepresentation = r.recordIndex;
            }
        }
    }
    [[nodiscard]] Result<void> BuildWireframes(CurvePointDefinitions& definitions)
    {
        if (!wireframeRepresentation)
        {
            return {};
        }
        WireframeEvidence evidence;
        for (const auto set : Refs(*wireframeRepresentation, "items"))
        {
            if (!Is(set, "GEOMETRIC_CURVE_SET"))
            {
                continue;
            }
            WireframeInfo group;
            group.representationId = Id(*wireframeRepresentation);
            group.relationshipId = Id(Role("wireframe_shape_relationship"));
            group.setId = Id(set);
            for (const auto geometry : Refs(set, "elements"))
            {
                if (auto operation = m_execution.Check(Phase::BeforeNativeCall); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (auto operation = definitions.Append(SourceRecord{ geometry }, group, evidence); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
            }
            if (auto businessCheck = Need(!group.curves.empty() || !group.points.empty(), "GeneralWireEmptySet", set); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            m_model->wireframes.push_back(std::move(group));
        }
        m_report.wireframes = m_model->wireframes;
        m_report.tolerances.push_back(
            { "Postcheck", "MeasuredWireframeSourceTrimResidual", Id(*wireframeRepresentation), evidence.SourceResidualMm() }
        );
        m_report.tolerances.push_back(
            { "Postcheck", "MeasuredWireframeDefinitionEndpointError", Id(*wireframeRepresentation), evidence.EndpointResidualMm() }
        );
        return {};
    }
    [[nodiscard]] Result<void> BuildSupportingGeometry(CurvePointDefinitions& definitions)
    {
        if (!supportingRepresentation)
        {
            return {};
        }
        SupportingGeometryInfo group;
        group.representationId = Id(*supportingRepresentation);
        group.relationshipId = Id(Role("construction_relationship"));
        SupportingEvidence evidence;
        for (const auto geometry : Refs(*supportingRepresentation, "items"))
        {
            if (auto operation = m_execution.Check(Phase::BeforeNativeCall); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            auto appended = definitions.Append(SourceRecord{ geometry }, group, evidence);
            if (!appended)
            {
                return tl::unexpected(std::move(appended.error()));
            }
            if (*appended == SupportingAppendResult::ExistingPlacement)
            {
                const bool plane = Is(geometry, "PLANE");
                const auto position = plane ? Ref(geometry, "position") : geometry;
                auto frameResult = Placement(position);
                if (!frameResult)
                {
                    return tl::unexpected(std::move(frameResult.error()));
                }
                const auto& frame = *frameResult;
                // STEP PLANE is unbounded. CreateCanonical retains its analytic
                // axes and native sentinel domain, without a finite source trim.
                // For a direct placement this same native storage is explicitly
                // an archive frame adapter, not an added surface or locus.
                SmPlane* raw = nullptr;
                if (auto operation = Step(geometry); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                const auto status = SmPlane::CreateCanonical(*m_model->context, frame.Native(), raw);
                std::unique_ptr<SmPlane> owned(raw);
                if (auto nativeStatusCheck = Good(status); !nativeStatusCheck)
                {
                    return tl::unexpected(std::move(nativeStatusCheck.error()));
                }
                if (auto businessCheck = Need(bool(owned), "GeneralSupportingPlaneAllocation", geometry); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                const auto& actual = owned->GetPosition();
                const auto axesEqual = [&](const auto& value, const V& expected)
                {
                    return value.x == expected[0] && value.y == expected[1] && value.z == expected[2];
                };
                if (auto businessCheck = Need(
                        axesEqual(actual.GetOriginRef(), frame.o) && axesEqual(actual.GetXAxisRef(), frame.x) &&
                            axesEqual(actual.GetYAxisRef(), frame.y),
                        "GeneralSupportingFrameChanged",
                        geometry
                    );
                    !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                const CoordinateFrame coordinates{ frame.o, frame.x, frame.y, frame.z };
                const auto index = m_model->supportingSurfaces.size();
                if (plane)
                {
                    const auto domain = owned->GetNaturalUVDomain();
                    group.planes.push_back(
                        { Id(geometry),
                          Id(position),
                          index,
                          coordinates,
                          { domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y } }
                    );
                }
                else
                {
                    group.frames.push_back({ Id(geometry), index, coordinates });
                }
                m_model->supportingSurfaces.push_back(std::move(owned));
            }
        }
        m_model->supportingGeometry.push_back(std::move(group));
        m_report.supportingGeometry = m_model->supportingGeometry;
        if (const auto sourceConsistency = evidence.SourceResidualMm())
        {
            m_report.tolerances.push_back({ "Postcheck", "MeasuredSupportingSourceTrimResidual", Id(*supportingRepresentation), *sourceConsistency });
        }
        if (const auto definitionEndpoints = evidence.EndpointResidualMm())
        {
            m_report.tolerances.push_back(
                { "Postcheck", "MeasuredSupportingDefinitionEndpointError", Id(*supportingRepresentation), *definitionEndpoints }
            );
        }
        return {};
    }
};
} // namespace
bool HasAssociatedGeometry(const step_semantics::Snapshot& source)
{
    for (const auto& r : source.PartDefinition().roles)
    {
        if (r.name == "wireframe_representation" || r.name == "construction_representation")
        {
            return true;
        }
    }
    return false;
}
Result<void> CheckAssociatedGeometry(const construction::GeometryConstruction& inputs)
{
    return AssociatedBuilder(inputs).Check();
}
Result<void> BuildAssociatedGeometry(const construction::GeometryConstruction& inputs, ModelData& model)
{
    return AssociatedBuilder(inputs).Populate(model);
}
Result<std::shared_ptr<ModelData>> BuildPartAssociatedGeometry(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const Options& options,
    Report& report,
    Execution& execution
)
{
    return AssociatedBuilder(std::move(source), options, report, execution).MakePartOwner();
}
} // namespace step_import::detail
