// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportWireframeModel.h"

#include "StepImportCurvePointConstruction.h"
#include "StepImportGeometryConstruction.h"
#include "StepImportInterval.h"
#include "StepImportNativeData.h"
#include "StepImportTerminalSpline.h"
#include "StepImportUnits.h"

#include <SmAssertArray.h>
#include <cfenv>
#include <limits>

namespace step_import::detail
{
namespace
{
using namespace construction;
class WireframeBuilder : private GeometryConstruction
{
public:

    WireframeBuilder(std::shared_ptr<const step_semantics::Snapshot> source, const Options& options, Report& report, Execution& execution)
        : GeometryConstruction(std::move(source), options, report, execution), model(std::make_shared<ModelData>())
    {
        model->source = m_source;
        model->queryTolerance = Q(options.quality.queryMm);
        model->queryBudget = options.quality.queryMm;
        model->definitionToleranceMm = Q(options.quality.definitionMm);
        model->info.shapeRole = "WireframeModel";
    }
    Result<NativeCandidate> Run()
    {
        const auto& part = m_source->PartDefinition();
        if (part.shapeRole != "WireframeModel" || !part.shapes.empty())
        {
            throw std::logic_error("Standalone wire construction requires its qualified source role");
        }
        const auto root = std::find_if(
            part.roles.begin(),
            part.roles.end(),
            [](const auto& role)
            {
                return role.name == "representation";
            }
        );
        if (root == part.roles.end())
        {
            throw std::logic_error("Standalone wire source root missing");
        }
        const auto representation = root->recordIndex;
        if (!std::numeric_limits<double>::is_iec559 || std::fegetround() != FE_TONEAREST)
        {
            return tl::unexpected(Fail("UnsupportedNativeNumericEnvironment", representation, Status::Indeterminate));
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
        if (auto checked = Need(std::isfinite(scale) && scale > 0, "GeneralUnitScaleRange", representation); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        m_report.units = UnitNormalization{ Id(m_source->UnitContext().lengthUnit), units->scaleMm, {}, std::move(units->definitions) };
        model->lengthUnits = m_report.units;
        m_report.numericalIntegration.reset(); // No face integral or material quantity exists for this source role.
        m_report.kernelPath = "TypedWireframeDefinition/OwnedCurvePointDefinitions";
        std::vector<Index> sets;
        for (const auto item : Refs(representation, "items"))
        {
            if (auto checked = m_execution.Check(Phase::BeforeNativeCall); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            if (Is(item, "AXIS2_PLACEMENT_3D"))
            {
                continue;
            }
            if (!Is(item, "GEOMETRIC_CURVE_SET"))
            {
                return tl::unexpected(Fail("GeneralWireframeItemUnsupported", item, Status::Unsupported));
            }
            sets.push_back(item);
            for (const auto geometry : Refs(item, "elements"))
            {
                if (!Is(geometry, "CARTESIAN_POINT") && !SupportsDetachedCurve(*m_source, geometry))
                {
                    return tl::unexpected(Fail("GeneralWireframeGeometryUnsupported", geometry, Status::Unsupported));
                }
            }
        }
        if (sets.empty())
        {
            return tl::unexpected(Fail("GeneralWireEmptyDefinition", representation));
        }
        if (auto step = Step(representation); !step)
        {
            return tl::unexpected(std::move(step.error()));
        }
        model->context = std::make_unique<SmContext>();
        m_context = model->context.get();
        // Local construction ownership unwinds before the ModelData context.
        // Finish moves it only into this unpublished definition; instances never
        // mutate it. GCS membership does not invent connected wire topology.
        CurvePointDefinitions definitions(*this);
        WireframeEvidence evidence;
        for (const auto set : sets)
        {
            WireframeInfo group;
            group.representationId = Id(representation);
            group.setId = Id(set); // Direct SDR root: no associated SRR identity.
            for (const auto geometry : Refs(set, "elements"))
            {
                if (auto checked = m_execution.Check(Phase::BeforeNativeCall); !checked)
                {
                    return tl::unexpected(std::move(checked.error()));
                }
                if (auto built = definitions.Append(SourceRecord{ geometry }, group, evidence); !built)
                {
                    return tl::unexpected(std::move(built.error()));
                }
            }
            if (group.curves.empty() && group.points.empty())
            {
                return tl::unexpected(Fail("GeneralWireEmptySet", set));
            }
            model->wireframes.push_back(std::move(group));
        }
        model->curvePointDefinitions = std::move(definitions).Finish();
        // Finite coordinates alone cannot bound source decimal/unit roundoff.
        // Certify shared points once and retain the bound in each owning view.
        std::vector<double> pointBounds;
        for (const auto& point : model->curvePointDefinitions.Points())
        {
            const auto* sourceRecord = m_source->Source().Find(point.sourceId);
            if (!sourceRecord)
            {
                throw std::logic_error("Owned wire point lost its source identity");
            }
            const auto record = std::size_t(sourceRecord - m_source->Source().Records().data());
            const auto& values = F(record, "coordinates").children;
            if (values.size() != 3)
            {
                return tl::unexpected(Fail("GeneralWirePointDimension", record));
            }
            const terminal_spline::IntegerRatio unit{ std::to_string(units->scaleMm.Numerator()), std::to_string(units->scaleMm.Denominator()) };
            auto bound = terminal_spline::SourcePointDifferenceUpper(
                { values[0].text, values[1].text, values[2].text },
                unit,
                point.pointMm,
                { m_options.sourceLimits.values, 4096, 1234 },
                [&](std::size_t count)
                {
                    return m_execution.SpendExactGeometryWork(m_report, count, record);
                }
            );
            if (!bound)
            {
                return tl::unexpected(std::move(bound.error()));
            }
            if (*bound > Enclose(m_options.quality.definitionMm).low)
            {
                return tl::unexpected(Fail("GeneralWirePointDefinitionUnestablished", record, Status::Indeterminate));
            }
            pointBounds.push_back(*bound);
        }
        for (auto& group : model->wireframes)
        {
            for (auto& point : group.points)
            {
                point.definitionErrorUpperMm = pointBounds.at(point.nativeIndex);
            }
        }
        // Curves have no topology owner. Run their actual deep checks after
        // construction and bounding consumers; do not claim BRep pointer/region
        // validation or accept only because a line strip can be drawn.
        std::size_t reports = 0;
        for (std::size_t i = 0; i < model->curvePointDefinitions.CurveCount(); ++i)
        {
            const auto& curve = model->curvePointDefinitions.CurveAt(CurveIndex{ i });
            if (auto step = Step(representation); !step)
            {
                return tl::unexpected(std::move(step.error()));
            }
            SmAssertArray deep;
            const bool valid = curve.AssertValid(&deep, SM_LEVEL_2, SM_WALK) != FALSE;
            reports += deep.GetSize();
            m_report.deepReports = reports;
            if (!valid || deep.GetSize())
            {
                return tl::unexpected(Fail("GeneralWireLevel2Validation", representation));
            }
            if (curve.GetOwner() || curve.GetContext() != m_context || curve.GetDim() != 3)
            {
                return tl::unexpected(Fail("GeneralWireOwnerMismatch", representation));
            }
        }
        m_report.deepReports = reports;
        m_report.wireframes = model->wireframes;
        m_report.tolerances.push_back({ "Postcheck", "MeasuredWireframeSourceTrimResidual", Id(representation), evidence.SourceResidualMm() });
        m_report.tolerances.push_back({ "Postcheck", "MeasuredWireframeDefinitionEndpointError", Id(representation), evidence.EndpointResidualMm() });
        return std::move(model);
    }

private:

    NativeCandidate model;
};
} // namespace
Result<NativeCandidate> BuildWireframeModel(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const Options& options,
    Report& report,
    Execution& execution
)
{
    if (!source)
    {
        throw std::invalid_argument("Wireframe construction requires complete source ownership");
    }
    return WireframeBuilder(std::move(source), options, report, execution).Run();
}
} // namespace step_import::detail
