// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSemantics.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("Missing frozen fixture");
    }
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}
std::size_t Index(const step_semantics::Snapshot& snapshot, const char* id)
{
    return static_cast<std::size_t>(snapshot.Source().Find(id) - snapshot.Source().Records().data());
}
} // namespace

int main(int argc, char** argv)
{
    using namespace step_semantics;
    std::size_t groups = 0;
    try
    {
        Require(argc == 2, "Pass frozen corpus case directory");
        const std::filesystem::path corpus(argv[1]);
        auto raw = step_source::InspectBytes(Read(corpus / "S203-P01/input.step"), step_source::Limits::PlanarFixtures());
        auto result = Check(raw, Limits{});
        Require(result.status == Status::Checked && result.snapshot, "P01 semantic checks");
        Require(
            result.binding == "Passed" && result.types == "Passed" && result.part == "Passed" && result.siUnits == "Passed",
            "Successful semantic phases must all complete"
        );
        auto snapshot = result.snapshot;
        Require(snapshot->Source().Records().size() == 51, "whole source retained");
        const auto view = snapshot->Attribute(Index(*snapshot, "203"), 0, 1);
        Require(view.name == "edge_start" && view.declaredIn == "EDGE" && view.declaredType == "VERTEX", "inherited field identity");
        Require(view.value == &raw.source->Find("203")->components[0].parameters[1], "attribute view cloned source value");
        raw = {};
        result = {};
        Require(view.value->text == "110" && snapshot->Source().Find("203"), "owning semantic snapshot lifetime");
        ++groups;

        {
            auto input = Read(corpus / "S203-P04/input.step");
            const auto at = input.find("'2;1'");
            Require(at != std::string::npos, "carrier semantic fixture");
            input.replace(at, 5, "'1'");
            auto carrier = step_source::InspectBytes(
                input,
                step_source::Limits::PlanarFixtures(),
                step_source::HeaderNamePolicy::StrictAscii,
                step_source::DataStringPolicy::StrictAscii,
                step_source::CarrierInterpretation::Level1AsCorrectedEncoding
            );
            Require(bool(carrier.source), "source carrier capture");
            const auto* receipt = carrier.source->AppliedCarrierAssumption();
            carrier.carrierAssumption.reset();
            carrier.requestedCarrierInterpretation = step_source::CarrierInterpretation::DeclaredOnly;
            carrier.diagnostics.clear();
            bool p0Refused = false;
            try
            {
                Check(carrier, Limits{});
            }
            catch (const std::invalid_argument&)
            {
                p0Refused = true;
            }
            Require(p0Refused, "outcome edits cannot enable P0 on an assumed carrier");
            auto interpreted = Check(carrier, Limits{}, SourcePolicy::PreserveNonGeometric, GeometryScope::GeneralBrep);
            Require(
                interpreted.status == Status::CheckedWithDiagnostics && interpreted.snapshot,
                "typed source retains unverified carrier qualification"
            );
            carrier = {};
            Require(
                interpreted.snapshot->Source().AppliedCarrierAssumption() == receipt && interpreted.snapshot->Source().Bytes() == input,
                "semantic owner retains carrier evidence and bytes"
            );
            ++groups;
        }

        // Keep both catalogs alive: AP214's DERIVE override must not change an existing AP203 field view.
        auto conversion203 = Check(step_source::InspectBytes(Read(corpus / "S203-P11/input.step"), step_source::Limits::PlanarFixtures()), Limits{});
        auto conversionText = Read(corpus / "S203-P11/input.step");
        const std::string originalSchema = "AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}";
        conversionText.replace(conversionText.find(originalSchema), originalSchema.size(), "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}");
        conversionText.replace(conversionText.find("NAMED_UNIT(#17)"), std::string("NAMED_UNIT(#17)").size(), "NAMED_UNIT(*)");
        const auto orphanBegin = conversionText.find("#17=DIMENSIONAL_EXPONENTS");
        conversionText.erase(orphanBegin, conversionText.find('\n', orphanBegin) - orphanBegin + 1);
        auto conversion214 = Check(step_source::InspectBytes(conversionText, step_source::Limits::PlanarFixtures()), Limits{});
        Require(conversion203.snapshot && conversion214.snapshot, "Both source profiles must publish complete deferred snapshots");
        auto explicitDimensions = conversion203.snapshot->Field(Index(*conversion203.snapshot, "19"), "NAMED_UNIT", "dimensions");
        auto derivedDimensions = conversion214.snapshot->Field(Index(*conversion214.snapshot, "19"), "NAMED_UNIT", "dimensions");
        Require(
            conversion203.snapshot->RegisteredTypes().size() == 72 && conversion214.snapshot->RegisteredTypes().size() == 134,
            "Catalog identity leaked across source profiles"
        );
        Require(
            !explicitDimensions.Declaration().derivedSlot &&
                conversion203.snapshot->Source().Records()[explicitDimensions.ReferenceTarget()].id == "17",
            "AP203 explicit conversion dimensions changed after AP214 selection"
        );
        Require(
            derivedDimensions.Declaration().redeclaredBy == "CONVERSION_BASED_UNIT" &&
                derivedDimensions.Value().kind == step_source::ValueKind::Derived,
            "AP214 conversion dimensions lost the source-derived marker"
        );
        conversion214 = {};
        bool derivedReferenceRefused = false;
        try
        {
            derivedDimensions.ReferenceTarget();
        }
        catch (const std::invalid_argument&)
        {
            derivedReferenceRefused = true;
        }
        Require(
            derivedReferenceRefused && derivedDimensions.Value().kind == step_source::ValueKind::Derived,
            "Owning derived field must survive outcome release without inventing a reference"
        );
        ++groups;

        auto angleText = Read(corpus / "S203-P01/input.step");
        angleText.replace(angleText.find(originalSchema), originalSchema.size(), "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}");
        const std::string siAngle = "#11=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));";
        angleText.replace(
            angleText.find(siAngle),
            siAngle.size(),
            "#11=(CONVERSION_BASED_UNIT('source angle',#902) NAMED_UNIT(*) PLANE_ANGLE_UNIT());\n"
            "#901=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));\n"
            "#902=PLANE_ANGLE_MEASURE_WITH_UNIT(PLANE_ANGLE_MEASURE(+0.0174532925199433E+0),#901);"
        );
        auto angleRaw = step_source::InspectBytes(angleText, step_source::Limits::PlanarFixtures());
        auto angleResult = Check(angleRaw, Limits{});
        Require(angleResult.snapshot && angleResult.unitDependencies == "Passed", "AP214 plane-angle source dependencies");
        auto angleValue = angleResult.snapshot->Field(Index(*angleResult.snapshot, "902"), "value_component");
        auto angleUnit = angleResult.snapshot->Field(Index(*angleResult.snapshot, "902"), "unit_component");
        const auto radianIndex = Index(*angleResult.snapshot, "901");
        Require(
            &angleValue.Value() == &angleRaw.source->Find("902")->components[0].parameters[0],
            "Inherited angle value must borrow the immutable original source slot"
        );
        angleRaw = {};
        angleResult = {};
        Require(
            angleValue.Value().text == "PLANE_ANGLE_MEASURE" && angleValue.Value().children.front().text == "+0.0174532925199433E+0" &&
                angleUnit.ReferenceTarget() == radianIndex,
            "Owning angle fields must retain the exact source lexeme and unit identity after caller release"
        );
        ++groups;

        // Synthetic legacy carrier: N916's required description must coexist
        // with the old optional N2635 slot in independent owning field views.
        auto legacyText = Read(corpus / "S203-P04/input.step");
        legacyText.replace(legacyText.find(originalSchema), originalSchema.size(), "CONFIG_CONTROL_DESIGN");
        const std::string legacyClaim = "'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011";
        legacyText.replace(legacyText.find(legacyClaim), legacyClaim.size(), "'config_control_design',2000");
        for (const auto& [from, to] : { std::pair{ "'P0 analytic fixture',$,", "'P0 analytic fixture',''," },
                                        std::pair{ "FORMATION('A',$,", "FORMATION('A',''," },
                                        std::pair{ "DEFINITION('D1',$,", "DEFINITION('D1',''," },
                                        std::pair{ "SHAPE('',$,", "SHAPE('',''," } })
        {
            const auto at = legacyText.find(from);
            Require(at != std::string::npos, "Synthetic N916 carrier description slot");
            legacyText.replace(at, std::char_traits<char>::length(from), to);
        }
        auto legacyRaw = step_source::InspectBytes(legacyText, step_source::Limits::PlanarFixtures());
        auto legacyResult = Check(legacyRaw, Limits{}, SourcePolicy::Strict, GeometryScope::GeneralBrep);
        Require(
            legacyResult.snapshot && legacyResult.snapshot->SelectedProfile().kind == ProfileKind::Ap203N916Source,
            "Explicit general scope selects the independent modified N916 implementation"
        );
        auto legacyDescription = legacyResult.snapshot->Field(Index(*legacyResult.snapshot, "4"), "description");
        auto oldDescription = snapshot->Field(Index(*snapshot, "4"), "description");
        Require(&legacyDescription.Value() == &legacyRaw.source->Find("4")->components[0].parameters[2], "N916 field borrows original source bytes");
        legacyRaw = {};
        legacyResult = {};
        Require(
            !legacyDescription.Declaration().optional && legacyDescription.Value().kind == step_source::ValueKind::String &&
                legacyDescription.Value().text.empty() && oldDescription.Declaration().optional &&
                oldDescription.Value().kind == step_source::ValueKind::Unset,
            "Legacy field lifetime and requiredness must not mutate an existing N2635 descriptor"
        );
        ++groups;

        auto presentationText = Read(corpus / "S203-P01/input.step");
        presentationText.replace(presentationText.find(originalSchema), originalSchema.size(), "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}");
        const std::string sdrLink = "SHAPE_DEFINITION_REPRESENTATION(#8,#20)";
        presentationText.replace(presentationText.find(sdrLink), sdrLink.size(), "SHAPE_DEFINITION_REPRESENTATION(#8,#910)");
        presentationText.insert(presentationText.find("ENDSEC;", presentationText.find("DATA;")), R"STEP(
#900=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('display',(#901),#12);
#901=STYLED_ITEM('source style',(#902),#21);
#902=PRESENTATION_STYLE_ASSIGNMENT((#903));
#903=SURFACE_STYLE_USAGE(.BOTH.,#904);
#904=SURFACE_SIDE_STYLE('side',(#905));
#905=SURFACE_STYLE_FILL_AREA(#906);
#906=FILL_AREA_STYLE('fill',(#907));
#907=FILL_AREA_STYLE_COLOUR('colour',#908);
#908=COLOUR_RGB('RGB',0.25,0.5,0.75);
#910=SHAPE_REPRESENTATION('source root',(#133),#12);
#911=SHAPE_REPRESENTATION_RELATIONSHIP('source link',$,#910,#20);
)STEP");
        auto presentationRaw = step_source::InspectBytes(presentationText, step_source::Limits::PlanarFixtures());
        auto presentationResult = Check(presentationRaw, Limits{});
        Require(presentationResult.status == Status::Checked && presentationResult.snapshot, "AP214 presentation source graph");
        auto presentation = presentationResult.snapshot;
        {
            auto curveStyleText = presentationText;
            const std::string assignment = "PRESENTATION_STYLE_ASSIGNMENT((#903))";
            curveStyleText.replace(curveStyleText.find(assignment), assignment.size(), "PRESENTATION_STYLE_ASSIGNMENT((#903,#960))");
            curveStyleText.insert(curveStyleText.find("ENDSEC;", curveStyleText.find("DATA;")), R"STEP(
#960=CURVE_STYLE('outline',#961,#962,#908);
#961=DRAUGHTING_PRE_DEFINED_CURVE_FONT('dashed');
#962=MEASURE_WITH_UNIT(POSITIVE_LENGTH_MEASURE(+0.250E0),#10);
)STEP");
            auto source = step_source::InspectBytes(curveStyleText, step_source::Limits::PlanarFixtures());
            auto outcome = Check(source, Limits{});
            Require(outcome.snapshot && outcome.status == Status::Checked, "AP214 size-select reference checks");
            auto width = outcome.snapshot->Field(Index(*outcome.snapshot, "960"), "curve_width");
            Require(
                width.Declaration().declaredType == "SIZE_SELECT" && &width.Value() == &source.source->Find("960")->components[0].parameters[2] &&
                    outcome.snapshot->Source().Records()[width.ReferenceTarget()].id == "962",
                "SIZE_SELECT keeps source field identity and its actual nominal measure reference"
            );
            const auto uses = outcome.snapshot->UsedIn("962", "CURVE_STYLE", "curve_width");
            Require(
                uses.size() == 1 && outcome.snapshot->Source().References()[uses[0]].recordIndex == Index(*outcome.snapshot, "960"),
                "SELECT references participate in typed inverse queries"
            );
            outcome = {};
            source = {};
            Require(width.Value().text == "962" && width.Location().recordId == "960", "Owning size-select field outlives source and outcome");
            width.ReferenceTarget();
            ++groups;
        }
        const auto style = Index(*presentation, "901"), relationship = Index(*presentation, "911");
        auto styles = presentation->Field(style, "styles");
        auto firstEndpoint = presentation->Field(relationship, "REPRESENTATION_RELATIONSHIP", "rep_1");
        auto colourName = presentation->Field(Index(*presentation, "908"), "COLOUR_SPECIFICATION", "name");
        Require(
            &firstEndpoint.Value() == &presentationRaw.source->Find("911")->components[0].parameters[2] &&
                presentation->Source().Records()[firstEndpoint.ReferenceTarget()].id == "910" &&
                presentation->Source().Records()[presentation->Field(relationship, "rep_2").ReferenceTarget()].id == "20",
            "Relationship endpoints must keep their actual declaring type, source values and directions"
        );
        // USEDIN returns incoming reference-occurrence indexes, not owner record indexes.
        const auto incoming = presentation->UsedIn("20", "REPRESENTATION_RELATIONSHIP", "rep_2");
        Require(
            incoming.size() == 1 && presentation->Source().References()[incoming[0]].recordIndex == relationship,
            "Relationship inverse query must retain the actual source reference occurrence"
        );
        Require(
            styles.ReferenceTargets() == std::vector<std::size_t>{ Index(*presentation, "902") } && presentation->UsedByRepresentation(style) &&
                !presentation->UsedByRepresentation(Index(*presentation, "908")),
            "Presentation source references and RI-only use closure"
        );
        Require(
            !snapshot->Instances("STYLED_ITEM") && snapshot->RegisteredTypes().size() == 72 &&
                snapshot->Field(Index(*snapshot, "20"), "items").ReferenceTargets() == std::vector<std::size_t>{ Index(*snapshot, "21") },
            "AP214 declarations must not leak into a live AP203 snapshot"
        );
        presentationRaw = {};
        presentationResult = {};
        presentation.reset();
        Require(
            firstEndpoint.Value().text == "910" && colourName.Value().text == "RGB" && styles.ReferenceTargets().size() == 1,
            "Owning presentation/relationship field views must survive release of their raw outcome and snapshot"
        );
        ++groups;

        auto gridText = Read(corpus / "S203-P01/input.step");
        gridText.replace(gridText.find(originalSchema), originalSchema.size(), "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}");
        const std::string originalPlane = "PLANE('',#133)";
        gridText.replace(
            gridText.find(originalPlane),
            originalPlane.size(),
            "B_SPLINE_SURFACE_WITH_KNOTS('source surface',2,1,((#100,#101),(#103,#102),(#100,#102)),"
            ".UNSPECIFIED.,.U.,.F.,.U.,(3,3),(2,2),(-2.,3.),(65.,66.),.UNSPECIFIED.)"
        );
        const std::string originalItems = "'body',(#21),#12";
        gridText.replace(gridText.find(originalItems), originalItems.size(), "'body',(#21,#133),#12");
        auto gridRaw = step_source::InspectBytes(gridText, step_source::Limits::PlanarFixtures());
        auto gridResult = Check(gridRaw, Limits{});
        Require(
            gridResult.status == Status::Checked && gridResult.snapshot && gridResult.geometryDependencies == "Passed",
            "Source spline dependencies must complete without geometric evaluation"
        );
        auto gridSnapshot = gridResult.snapshot;
        const auto surface = Index(*gridSnapshot, "134");
        auto grid = gridSnapshot->Field(surface, "B_SPLINE_SURFACE", "control_points_list");
        auto knots = gridSnapshot->Field(surface, "u_knots");
        const auto& pointRef = grid.Value().children[2].children[0];
        Require(
            grid.Declaration().nestedAggregates.size() == 1 && grid.Value().children.size() == 3 && grid.Value().children[0].children.size() == 2 &&
                gridSnapshot->ReferenceTarget(pointRef) == Index(*gridSnapshot, "100") &&
                &pointRef == &gridRaw.source->Find("134")->components[0].parameters[3].children[2].children[0],
            "Nested source grid must preserve rows, repeated identities and actual reference values"
        );
        const auto indexedGrid = gridSnapshot->Attribute(surface, 0, 3);
        Require(
            grid.Declaration().nestedAggregates.data() == indexedGrid.nestedAggregates.data() && indexedGrid.nestedAggregates.size() == 1,
            "Named and indexed views must borrow the same static dimension descriptors without vector copies"
        );
        bool matrixFlatteningRefused = false;
        try
        {
            grid.ReferenceTargets();
        }
        catch (const std::invalid_argument&)
        {
            matrixFlatteningRefused = true;
        }
        Require(
            matrixFlatteningRefused && gridSnapshot->Field(surface, "u_closed").Value().text == "U" &&
                gridSnapshot->IsA(surface, "BOUNDED_SURFACE") && gridSnapshot->IsA(surface, "B_SPLINE_SURFACE"),
            "Matrix lookup must not flatten rows or resolve LOGICAL unknown"
        );
        Require(
            gridResult.geometryDependencyChecks.total == 5 && gridResult.geometryDependencyChecks.entityPredicates == 4 &&
                gridResult.geometryDependencyChecks.typePredicates == 0 && !snapshot->Instances("B_SPLINE_SURFACE"),
            "Only the selected AP214 prerequisite subset is counted; AP203 catalog remains isolated"
        );
        gridRaw = {};
        gridResult = {};
        gridSnapshot.reset();
        Require(
            grid.Value().children[2].children[0].text == "100" && knots.Value().children[0].text == "-2.",
            "Owning spline field views must keep original grid and knot parameters after snapshot release"
        );
        ++groups;

        // MRI is an MWU and can occupy the existing conversion-factor reference.
        // This isolates source declarations/ownership, not full RI.WR1 qualification.
        auto measureText = conversionText;
        const std::string oldFactor = "LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#10)";
        measureText
            .replace(measureText.find(oldFactor), oldFactor.size(), "MEASURE_REPRESENTATION_ITEM('source scale',LENGTH_MEASURE(+25.400E+0),#10)");
        auto measureRaw = step_source::InspectBytes(measureText, step_source::Limits::PlanarFixtures());
        auto measureResult = Check(measureRaw, Limits{});
        Require(measureResult.snapshot && measureResult.unitDependencies == "Passed", "MRI source dependencies");
        auto measure = measureResult.snapshot;
        const auto measureIndex = Index(*measure, "18");
        auto measureValue = measure->Field(measureIndex, "MEASURE_WITH_UNIT", "value_component");
        auto measureUnit = measure->Field(measureIndex, "MEASURE_WITH_UNIT", "unit_component");
        auto measureName = measure->Field(measureIndex, "REPRESENTATION_ITEM", "name");
        Require(
            measure->IsA(measureIndex, "MEASURE_WITH_UNIT") && measure->IsA(measureIndex, "REPRESENTATION_ITEM") &&
                &measureValue.Value() == &measureRaw.source->Find("18")->components[0].parameters[1] &&
                measureUnit.ReferenceTarget() == Index(*measure, "10"),
            "MRI parent roles and fields must borrow the original source values and references"
        );
        measureRaw = {};
        measureResult = {};
        measure.reset();
        Require(
            measureName.Value().text == "source scale" && measureValue.Value().text == "LENGTH_MEASURE" &&
                measureValue.Value().children.front().text == "+25.400E+0" && measureUnit.Value().text == "10" &&
                measureUnit.Location().parameterPath == std::vector<std::size_t>{ 3 },
            "Owning MRI fields must retain the original tag, REAL lexeme, unit identity and slot after caller release"
        );
        ++groups;

        auto valueText = Read(corpus / "S203-P04/input.step");
        valueText.replace(valueText.find(originalSchema), originalSchema.size(), "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}");
        valueText.insert(valueText.find("ENDSEC;", valueText.find("DATA;")), R"STEP(
#900=PROPERTY_DEFINITION('source count',$,#7);
#901=VALUE_REPRESENTATION_ITEM('count',COUNT_MEASURE(123456789012345678901234567890));
#902=REPRESENTATION('source count',(#901),#12);
#903=PROPERTY_DEFINITION_REPRESENTATION(#900,#902);
)STEP");
        auto valueRaw = step_source::InspectBytes(valueText, step_source::Limits::PlanarFixtures());
        auto valueResult = Check(valueRaw, Limits{});
        Require(valueResult.snapshot && valueResult.status == Status::Checked, "AP214 source value snapshot");
        auto owningValue = valueResult.snapshot->Field(Index(*valueResult.snapshot, "901"), "value_component");
        Require(&owningValue.Value() == &valueRaw.source->Find("901")->components[0].parameters[1], "VRI field must borrow exact original value");
        valueRaw = {};
        valueResult = {};
        Require(
            owningValue.Value().text == "COUNT_MEASURE" && owningValue.Value().children[0].kind == step_source::ValueKind::Integer &&
                owningValue.Value().children[0].text == "123456789012345678901234567890" &&
                owningValue.Declaration().declaredIn == "VALUE_REPRESENTATION_ITEM" &&
                owningValue.Location().parameterPath == std::vector<std::size_t>{ { 2 } },
            "owning integer value literal and source binding"
        );
        ++groups;

        auto propertyText = Read(corpus / "S203-P01/input.step");
        propertyText.replace(propertyText.find(originalSchema), originalSchema.size(), "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}");
        propertyText.insert(propertyText.find("ENDSEC;", propertyText.find("DATA;")), R"STEP(
#920=PROPERTY_DEFINITION('material property',$,#7);
#921=PROPERTY_DEFINITION_REPRESENTATION(#920,#922);
#922=REPRESENTATION('material name',(#923),#924);
#923=DESCRIPTIVE_REPRESENTATION_ITEM('source alloy','original description');
#924=REPRESENTATION_CONTEXT('source properties','metadata');
#930=PROPERTY_DEFINITION('quantity','length',#8);
#931=PROPERTY_DEFINITION_REPRESENTATION(#930,#932);
#932=REPRESENTATION('quantity',(#933),#934);
#933=MEASURE_REPRESENTATION_ITEM('source length',LENGTH_MEASURE(+0.025400E+3),#935);
#934=REPRESENTATION_CONTEXT('quantity context','metadata');
#935=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.CENTI.,.METRE.));
)STEP");
        auto propertyRaw = step_source::InspectBytes(propertyText, step_source::Limits::PlanarFixtures());
        auto propertyResult = Check(propertyRaw, Limits{});
        Require(propertyResult.status == Status::Checked && propertyResult.snapshot, "Property source attachment checks");
        auto propertySnapshot = propertyResult.snapshot;
        const auto propertyFacts = propertySnapshot->PartDefinition().propertyAttachments;
        Require(
            propertyFacts.size() == 2 && propertyFacts[0].link == Index(*propertySnapshot, "921") &&
                propertyFacts[1].characterizedDefinition == Index(*propertySnapshot, "8") &&
                propertyFacts[1].productDefinition == Index(*propertySnapshot, "7") && propertyFacts[1].context == Index(*propertySnapshot, "934") &&
                propertySnapshot->PartDefinition().shapeRole == "OpenSurfaceModel" && propertySnapshot->UnitContext().lengthPower10Mm == 0,
            "Source links must retain the selected part, original property context and body role/unit scale"
        );
        auto propertyOwner = propertySnapshot->Field(Index(*propertySnapshot, "930"), "definition");
        auto propertyLink = propertySnapshot->Field(Index(*propertySnapshot, "931"), "used_representation");
        auto propertyValue = propertySnapshot->Field(Index(*propertySnapshot, "933"), "value_component");
        auto propertyUnit = propertySnapshot->Field(Index(*propertySnapshot, "933"), "unit_component");
        Require(
            &propertyOwner.Value() == &propertyRaw.source->Find("930")->components[0].parameters[2] &&
                propertyLink.ReferenceTarget() == Index(*propertySnapshot, "932") &&
                propertyUnit.ReferenceTarget() == Index(*propertySnapshot, "935"),
            "Property fields must borrow their source references without copying values or filling context units"
        );
        propertyRaw = {};
        propertyResult = {};
        propertySnapshot.reset();
        Require(
            propertyOwner.Value().text == "8" && propertyLink.Location().recordId == "931" &&
                propertyValue.Value().children.front().text == "+0.025400E+3" && propertyUnit.Value().text == "935" &&
                snapshot->PartDefinition().propertyAttachments.empty(),
            "Owning property fields must survive parent release without changing the live AP203 snapshot"
        );
        ++groups;

        auto interoperabilityText = propertyText;
        interoperabilityText.replace(interoperabilityText.find("'body'"), std::string("'body'").size(), "$");
        const std::string quantity = "LENGTH_MEASURE(+0.025400E+3)";
        interoperabilityText.replace(interoperabilityText.find(quantity), quantity.size(), "POSITIVE_RATIO_MEASURE(0.)");
        auto interoperabilityRaw = step_source::InspectBytes(interoperabilityText, step_source::Limits::PlanarFixtures());
        const auto strictInteroperability = Check(interoperabilityRaw, Limits{});
        Require(
            strictInteroperability.status == Status::Rejected && !strictInteroperability.snapshot,
            "Strict behavior cannot change for interoperability"
        );
        auto interoperability = Check(interoperabilityRaw, Limits{}, SourcePolicy::PreserveNonGeometric);
        Require(
            interoperability.status == Status::CheckedWithDiagnostics && interoperability.snapshot &&
                interoperability.preservedDiagnostics.size() == 2,
            "Only proven nongeometric issues can yield a source snapshot with diagnostics"
        );
        auto interoperabilitySnapshot = interoperability.snapshot;
        auto preservedQuantity = interoperabilitySnapshot->Field(Index(*interoperabilitySnapshot, "933"), "value_component");
        Require(
            &preservedQuantity.Value() == &interoperabilityRaw.source->Find("933")->components[0].parameters[1] &&
                interoperabilitySnapshot->PreservedDiagnostics().size() == 2 && snapshot->PreservedDiagnostics().empty(),
            "Explicit policy must retain original values/diagnostics and remain isolated from a live strict snapshot"
        );
        interoperability = {};
        interoperabilityRaw = {};
        interoperabilitySnapshot.reset();
        Require(
            preservedQuantity.Value().text == "POSITIVE_RATIO_MEASURE" && preservedQuantity.Value().children.front().text == "0.",
            "Owning interoperability source cannot silently rewrite the bad metadata branch or scalar"
        );
        ++groups;

        auto unitText = Read(corpus / "S203-P01/input.step");
        unitText.replace(unitText.find(originalSchema), originalSchema.size(), "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}");
        const std::string oldAssignments = "GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11))";
        unitText.replace(unitText.find(oldAssignments), oldAssignments.size(), "GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11,#903))");
        unitText.insert(unitText.find("ENDSEC;", unitText.find("DATA;")), R"STEP(
#900=(MASS_UNIT() NAMED_UNIT(*) SI_UNIT(.KILO.,.GRAM.));
#901=DERIVED_UNIT_ELEMENT(#900,+1.0);
#902=DERIVED_UNIT_ELEMENT(#10,-3.);
#903=DERIVED_UNIT((#901,#902));
)STEP");
        auto unitRaw = step_source::InspectBytes(unitText, step_source::Limits::PlanarFixtures());
        auto unitResult = Check(unitRaw, Limits{});
        Require(unitResult.status == Status::Checked && unitResult.snapshot && unitResult.unitDependencies == "Passed", "Derived source unit checks");
        auto unitSnapshot = unitResult.snapshot;
        auto power = unitSnapshot->Field(Index(*unitSnapshot, "902"), "exponent");
        auto unitElements = unitSnapshot->Field(Index(*unitSnapshot, "903"), "elements");
        auto dimensionDefinitions = unitSnapshot->UnitContext().dimensionDefinitions;
        const auto foundDimension = std::find_if(
            dimensionDefinitions.begin(),
            dimensionDefinitions.end(),
            [&](const UnitDimensionDefinition& value)
            {
                return value.recordIndex == Index(*unitSnapshot, "903");
            }
        );
        Require(
            foundDimension != dimensionDefinitions.end() && foundDimension->nominal == "Derived" && foundDimension->dimensions[0].numerator == "-3" &&
                foundDimension->dimensions[0].denominator == "1" && foundDimension->dimensions[1].numerator == "1",
            "Derived dimension facts must retain their source role"
        );
        Require(
            &power.Value() == &unitRaw.source->Find("902")->components[0].parameters[1] &&
                unitElements.ReferenceTargets() == std::vector<std::size_t>{ Index(*unitSnapshot, "901"), Index(*unitSnapshot, "902") } &&
                unitSnapshot->Field(Index(*unitSnapshot, "901"), "unit").ReferenceTarget() == Index(*unitSnapshot, "900"),
            "Unit queries must preserve original source values, order and reference identities"
        );
        bool derivedNameMissing = false;
        try
        {
            unitSnapshot->Field(Index(*unitSnapshot, "903"), "name");
        }
        catch (const std::invalid_argument&)
        {
            derivedNameMissing = true;
        }
        Require(
            derivedNameMissing && snapshot->UnitContext().dimensionDefinitions.empty() && snapshot->RegisteredTypes().size() == 72,
            "A derived name is not a serialized slot, and AP214 facts cannot change a live AP203 snapshot"
        );
        unitRaw = {};
        unitResult = {};
        unitSnapshot.reset();
        Require(
            power.Value().text == "-3." && unitElements.ReferenceTargets().size() == 2 && foundDimension->dimensions[0].numerator == "-3",
            "Owning field views and copied dimension strings must survive parent outcome/snapshot release"
        );
        ++groups;

        auto bound = snapshot->Field(Index(*snapshot, "203"), "EDGE", "edge_start");
        Require(bound.Declaration().declaredIn == "EDGE" && &bound.Value() == view.value, "Named query must reuse the inherited source value");
        const auto site = bound.Location();
        Require(
            site.recordId == "203" && site.component == "EDGE_CURVE" && site.parameterPath == std::vector<std::size_t>{ 2 } &&
                site.span.begin == view.value->span.begin && site.span.end == view.value->span.end,
            "Named field location must retain its actual source slot"
        );
        Require(
            snapshot->Field(Index(*snapshot, "10"), "NAMED_UNIT", "dimensions").Declaration().redeclaredBy == "SI_UNIT" &&
                snapshot->Field(Index(*snapshot, "10"), "dimensions").Value().kind == step_source::ValueKind::Derived,
            "Named lookup must retain the derived slot without evaluating it"
        );
        bool missingField = false;
        try
        {
            snapshot->Field(Index(*snapshot, "203"), "EDGE_CURVE", "edge_start");
        }
        catch (const std::invalid_argument&)
        {
            missingField = true;
        }
        Require(missingField, "Qualified lookup must use the declaring type, not an inherited alias");
        ++groups;

        const auto retainedField = [&]()
        {
            auto input = step_source::InspectBytes(Read(corpus / "S203-P01/input.step"), step_source::Limits::PlanarFixtures());
            auto checked = Check(input, Limits{});
            return checked.snapshot->Field(Index(*checked.snapshot, "203"), "edge_start");
        }();
        Require(retainedField.Value().text == "110" && retainedField.Location().recordId == "203", "Field view must own its source lifetime");
        ++groups;

        Require(bound.ReferenceTarget() == Index(*snapshot, "110"), "Scalar reference keeps source identity");
        Require(
            snapshot->Field(Index(*snapshot, "300"), "edge_list").ReferenceTargets() ==
                std::vector<std::size_t>{ Index(*snapshot, "204"), Index(*snapshot, "214"), Index(*snapshot, "224"), Index(*snapshot, "234") },
            "Aggregate references keep the original traversal order"
        );
        Require(retainedField.ReferenceTarget() == Index(*snapshot, "110"), "Owned reference query survives caller release");
        const auto copied = bound.Value();
        bool copiedRejected = false;
        try
        {
            snapshot->ReferenceTarget(copied);
        }
        catch (const std::invalid_argument&)
        {
            copiedRejected = true;
        }
        Require(copiedRejected, "Copied value must not masquerade as a source-owned reference");
        const auto otherInput = step_source::InspectBytes(Read(corpus / "S203-P01/input.step"), step_source::Limits::PlanarFixtures());
        const auto other = Check(otherInput, Limits{});
        bool foreignRejected = false;
        try
        {
            snapshot->ReferenceTarget(other.snapshot->Field(Index(*other.snapshot, "203"), "edge_start").Value());
        }
        catch (const std::invalid_argument&)
        {
            foreignRejected = true;
        }
        Require(foreignRejected, "Equal IDs and spans from a different source snapshot must be rejected");
        ++groups;

        const auto unset = snapshot->Field(Index(*snapshot, "11"), "SI_UNIT", "prefix");
        Require(unset.Value().kind == step_source::ValueKind::Unset, "Unset field must remain a present source slot");
        for (const auto& field : { unset, snapshot->Field(Index(*snapshot, "204"), "EDGE", "edge_start") })
        {
            bool rejected = false;
            try
            {
                field.ReferenceTarget();
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            Require(rejected, "Unset and derived slots must not produce guessed reference targets");
        }
        bool numericAggregateRejected = false;
        try
        {
            snapshot->Field(Index(*snapshot, "100"), "coordinates").ReferenceTargets();
        }
        catch (const std::invalid_argument&)
        {
            numericAggregateRejected = true;
        }
        Require(numericAggregateRejected, "Non-reference aggregate must not become an empty reference collection");
        ++groups;

        const auto units = snapshot->Field(Index(*snapshot, "12"), "GLOBAL_UNIT_ASSIGNED_CONTEXT", "units");
        Require(
            units.Location().component == "GLOBAL_UNIT_ASSIGNED_CONTEXT" && units.Location().parameterPath == std::vector<std::size_t>{ 1 } &&
                units.ReferenceTargets() == std::vector<std::size_t>{ Index(*snapshot, "10"), Index(*snapshot, "11") },
            "Complex facet source location and unit reference order"
        );
        const auto detachedLocation = [&]()
        {
            const auto input = step_source::InspectBytes(Read(corpus / "S203-P01/input.step"), step_source::Limits::PlanarFixtures());
            const auto checked = Check(input, Limits{});
            return checked.snapshot->Field(Index(*checked.snapshot, "203"), "edge_start").Location();
        }();
        Require(detachedLocation.recordId == "203" && detachedLocation.component == "EDGE_CURVE", "Returned location facts own their strings");
        bool unknownField = false;
        try
        {
            snapshot->Field(Index(*snapshot, "12"), "unregistered_field");
        }
        catch (const std::invalid_argument&)
        {
            unknownField = true;
        }
        Require(unknownField, "Unknown field query must not invent a missing or empty source value");
        ++groups;

        const auto loop = Index(*snapshot, "300");
        Require(
            snapshot->IsA(loop, "EDGE_LOOP") && snapshot->IsA(loop, "LOOP") && snapshot->IsA(loop, "PATH") &&
                snapshot->IsA(loop, "TOPOLOGICAL_REPRESENTATION_ITEM") && snapshot->IsA(loop, "REPRESENTATION_ITEM"),
            "full multiple inheritance"
        );
        Require(snapshot->Instances("LOOP")->size() == 1 && snapshot->Instances("MASS_UNIT")->empty(), "complete known populations");
        Require(snapshot->Instances("NOT_REGISTERED") == nullptr, "unknown query became fake empty population");
        ++groups;

        const auto contextElements = snapshot->UsedIn("1", "APPLICATION_CONTEXT_ELEMENT", "frame_of_reference");
        Require(contextElements.size() == 2, "APD must not count as context element");
        Require(snapshot->UsedIn("12", "REPRESENTATION", "context_of_items").size() == 1, "context inverse");
        Require(snapshot->UsedIn("22", "REPRESENTATION_ITEM", "name").empty(), "explicit non-reference role should be empty");
        bool rejectedUnknown = false;
        try
        {
            snapshot->UsedIn("1", "APPLICATION_CONTEXT_ELEMENT", "invented_field");
        }
        catch (const std::invalid_argument&)
        {
            rejectedUnknown = true;
        }
        Require(rejectedUnknown, "unregistered USEDIN role became empty");
        ++groups;

        for (auto r : *snapshot->Instances("REPRESENTATION_ITEM"))
        {
            Require(snapshot->UsedByRepresentation(r), "indirect RI use lost");
        }
        Require(!snapshot->UsedByRepresentation(Index(*snapshot, "4")), "product was treated as RI use");
        const auto derived = snapshot->Attribute(Index(*snapshot, "204"), 0, 1);
        Require(derived.derivedSlot && derived.redeclaredBy == "ORIENTED_EDGE", "OE derived marker contract");
        Require(snapshot->UnitContext().lengthPower10Mm == 0, "exact mm scale");
        ++groups;

        for (const auto* id : { "S203-P04", "S203-P05", "S203-P10", "S203-P12" })
        {
            const auto input = step_source::InspectBytes(Read(corpus / id / "input.step"), step_source::Limits::PlanarFixtures());
            const auto semantic = Check(input, Limits{});
            Require(semantic.status == Status::Checked, "other SI/source-role fixture");
        }
        ++groups;

        const auto conversion = step_source::InspectBytes(Read(corpus / "S203-P11/input.step"), step_source::Limits::PlanarFixtures());
        const auto deferred = Check(conversion, Limits{});
        Require(
            deferred.status == Status::Deferred && deferred.snapshot && !deferred.snapshot->UnitContext().lengthPower10Mm,
            "conversion factor was guessed in ticket 03"
        );
        Require(
            deferred.binding == "Passed" && deferred.types == "Passed" && deferred.part == "Passed" && deferred.siUnits == "Deferred",
            "Only the unit phase is deferred after complete semantic prerequisites"
        );
        ++groups;

        const auto siDimensions = snapshot->Attribute(Index(*snapshot, "10"), 1, 0);
        Require(
            siDimensions.name == "dimensions" && siDimensions.declaredIn == "NAMED_UNIT" && siDimensions.derivedSlot &&
                siDimensions.redeclaredBy == "SI_UNIT" && siDimensions.value->kind == step_source::ValueKind::Derived,
            "SI redeclaration must bind the original NAMED_UNIT slot"
        );
        const auto conversionDimensions = deferred.snapshot->Attribute(Index(*deferred.snapshot, "19"), 2, 0);
        Require(
            conversionDimensions.name == "dimensions" && conversionDimensions.declaredIn == "NAMED_UNIT" && !conversionDimensions.derivedSlot &&
                conversionDimensions.redeclaredBy.empty() && conversionDimensions.value->text == "17",
            "Conversion unit must retain its explicit dimensional reference"
        );
        ++groups;

        const auto context = Index(*snapshot, "12");
        Require(
            snapshot->Attribute(context, 0, 0).name == "coordinate_space_dimension" && snapshot->Attribute(context, 1, 0).name == "units" &&
                snapshot->Attribute(context, 2, 0).name == "context_identifier" && snapshot->Attribute(context, 2, 1).name == "context_type",
            "Complex context keeps each declaration in its actual source facet"
        );
        ++groups;

        const auto n10 = step_source::InspectBytes(Read(corpus / "S203-N10/input.step"), step_source::Limits::PlanarFixtures());
        const auto n10Result = Check(n10, Limits{});
        Require(
            n10Result.status == Status::Checked && n10Result.snapshot->PartDefinition().apdSchemaClaim.find("_mim_lf") != std::string::npos,
            "APD claim was silently replaced by HEADER binding"
        );
        ++groups;

        const auto p01 = step_source::InspectBytes(Read(corpus / "S203-P01/input.step"), step_source::Limits::PlanarFixtures());
        const auto limited = Check(p01, Limits{ 0 });
        Require(limited.status == Status::ResourceLimit && !limited.snapshot, "partial semantic graph escaped budget failure");
        Require(limited.diagnostics[0].source.key == "DependencyVisitLimit", "wrong budget responsibility");
        Require(
            limited.binding == "Passed" && limited.types == "ResourceLimit" && limited.part == "NotRun" && limited.siUnits == "NotRun",
            "Resource stop preserves completed binding and blocks later semantic phases"
        );
        ++groups;

        const auto missing = step_source::InspectBytes(Read(corpus / "S203-N03/input.step"), step_source::Limits::PlanarFixtures());
        const auto rejected = Check(missing, Limits{});
        Require(
            rejected.status == Status::SourceFailure && !rejected.snapshot && rejected.diagnostics[0].source.key == "UnresolvedRequiredReference",
            "raw source failure was turned into typed success"
        );
        Require(
            rejected.binding == "NotRun" && rejected.types == "NotRun" && rejected.part == "NotRun" && rejected.siUnits == "NotRun",
            "Upstream source failure must not enter semantic phases"
        );
        ++groups;

        const auto phaseCheck = [&](const char* name)
        {
            return Check(step_source::InspectBytes(Read(corpus / name / "input.step"), step_source::Limits::PlanarFixtures()), Limits{});
        };
        const auto bindingFailure = phaseCheck("S203-N01");
        const auto typeFailure = phaseCheck("S203-N04");
        const auto partFailure = phaseCheck("S203-N16");
        Require(bindingFailure.binding == "Unsupported" && bindingFailure.types == "NotRun", "Binding failure gate");
        Require(typeFailure.binding == "Passed" && typeFailure.types == "Rejected" && typeFailure.part == "NotRun", "Field failure gate");
        Require(partFailure.types == "Passed" && partFailure.part == "Unsupported" && partFailure.siUnits == "NotRun", "Part failure gate");
        ++groups;

        // phaseCheck's temporary raw outcome is gone; failed binding still owns its observations.
        Require(bindingFailure.schemaSelection && !bindingFailure.schemaSelection->qualified, "Failed binding must not select a profile");
        const auto& claims = bindingFailure.schemaSelection->identifiers;
        Require(claims.size() == 1 && !claims[0].literal.empty(), "Failed binding lost its owning HEADER observation");
        Require(!rejected.schemaSelection, "Source failure must not invent a schema-selection observation");
        ++groups;

        auto report = Check(p01, Limits{});
        auto reportingInput = p01;
        reportingInput.limits.reportBytes = 1;
        std::string text = "stale";
        Require(!InspectJson(reportingInput, report, text) && text.empty(), "partial/stale report was published");
        ++groups;

        {
            auto generalText = Read(corpus / "S203-P04/input.step");
            const auto ending = generalText.rfind("ENDSEC;");
            Require(ending != std::string::npos, "general fixture DATA terminator");
            generalText.insert(
                ending,
                "#900=PROPERTY_DEFINITION('count',$,#7);\n"
                "#901=VALUE_REPRESENTATION_ITEM('count',COUNT_MEASURE(0));\n"
                "#902=REPRESENTATION('counts',(#901),#12);\n"
                "#903=PROPERTY_DEFINITION_REPRESENTATION(#900,#902);\n"
                "#910=CC_DESIGN_DATE_AND_TIME_ASSIGNMENT(#911,#915,(#7));\n"
                "#911=DATE_AND_TIME(#912,#913);\n"
                "#912=CALENDAR_DATE(2026,18,10);\n"
                "#913=LOCAL_TIME(9,30,0.,#914);\n"
                "#914=COORDINATED_UNIVERSAL_TIME_OFFSET(8,$,.AHEAD.);\n"
                "#915=DATE_TIME_ROLE('design');\n"
            );
            auto generalRaw = step_source::InspectBytes(generalText, step_source::Limits::PlanarFixtures());
            auto general = Check(generalRaw, Limits{}, SourcePolicy::Strict, GeometryScope::GeneralBrep);
            Require(general.status == Status::Checked && general.snapshot, "explicit N2635 general source");
            Require(
                general.snapshot->SelectedProfile().kind == ProfileKind::Ap203e2GeneralSource &&
                    general.snapshot->SelectedProfile().expressSha256 == SchemaSha256,
                "general scope retains N2635 authority"
            );
            Require(general.snapshot->RegisteredTypes().size() > 128, "general type membership must exceed the old compact limit");
            Require(
                general.snapshot->IsA(Index(*general.snapshot, "910"), "CC_DESIGN_DATE_AND_TIME_ASSIGNMENT") &&
                    general.snapshot->IsA(Index(*general.snapshot, "910"), "DATE_AND_TIME_ASSIGNMENT"),
                "extended leaf and ancestor membership"
            );
            auto owned = general.snapshot->Field(Index(*general.snapshot, "901"), "value_component");
            Require(&owned.Value() == &generalRaw.source->Find("901")->components[0].parameters[1], "general source values were copied");
            const auto defaultAgain = Check(
                step_source::InspectBytes(Read(corpus / "S203-P04/input.step"), step_source::Limits::PlanarFixtures()),
                Limits{}
            );
            Require(defaultAgain.snapshot && !defaultAgain.snapshot->Instances("VALUE_REPRESENTATION_ITEM"), "catalog scope leaked into P0");
            general = {};
            generalRaw = {};
            Require(
                owned.Value().text == "COUNT_MEASURE" && owned.Value().children[0].kind == step_source::ValueKind::Integer &&
                    owned.Value().children[0].text == "0",
                "general owning field lost its NUMBER source value"
            );
            ++groups;
        }
        {
            auto metadataText = Read(corpus / "S203-P04/input.step");
            const std::string original = "AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}";
            metadataText.replace(metadataText.find(original), original.size(), "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}");
            metadataText.insert(
                metadataText.rfind("ENDSEC;"),
                "#940=DATE_TIME_ROLE('creation');\n"
                "#941=APPLIED_DATE_AND_TIME_ASSIGNMENT(#942,#940,(#7));\n"
                "#942=DATE_AND_TIME(#943,#944);\n"
                "#943=CALENDAR_DATE(+2024,29,2);\n"
                "#944=LOCAL_TIME(12,30,+17.000E+0,#945);\n"
                "#945=COORDINATED_UNIVERSAL_TIME_OFFSET(0,$,.BEHIND.);\n"
                "#950=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('display',(#951),#12);\n"
                "#951=STYLED_ITEM('style',(#952),#21);\n"
                "#952=PRESENTATION_STYLE_ASSIGNMENT((#953));\n"
                "#953=SURFACE_STYLE_USAGE(.BOTH.,#954);\n"
                "#954=SURFACE_SIDE_STYLE('surface',(#955));\n"
                "#955=SURFACE_STYLE_RENDERING_WITH_PROPERTIES($,#957,(#956));\n"
                "#956=SURFACE_STYLE_TRANSPARENT(+0.000E+0);\n"
                "#957=COLOUR_RGB('RGB',0.2,0.4,0.6);\n"
            );
            auto input = step_source::InspectBytes(metadataText, step_source::Limits::PlanarFixtures());
            auto checked = Check(input, Limits{}, SourcePolicy::PreserveNonGeometric);
            Require(checked.status == Status::CheckedWithDiagnostics && checked.snapshot, "AP214 explicit metadata policy");
            auto method = checked.snapshot->Field(Index(*checked.snapshot, "955"), "rendering_method");
            auto seconds = checked.snapshot->Field(Index(*checked.snapshot, "944"), "second_component");
            Require(&method.Value() == &input.source->Find("955")->components[0].parameters[0], "missing method source identity");
            Require(
                checked.snapshot->PreservedDiagnostics().size() == 1 && method.Value().kind == step_source::ValueKind::Unset,
                "missing rendering method must not receive an implicit value"
            );
            Require(checked.snapshot->IsA(Index(*checked.snapshot, "941"), "DATE_AND_TIME_ASSIGNMENT"), "applied date parent identity");
            input = {};
            checked = {};
            Require(
                method.Value().kind == step_source::ValueKind::Unset && seconds.Value().text == "+17.000E+0",
                "metadata fields retain owning source after outcome release"
            );
            ++groups;
        }
        {
            auto massMetadataText = Read(corpus / "S203-P01/input.step");
            massMetadataText.replace(massMetadataText.find(originalSchema), originalSchema.size(), "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}");
            massMetadataText.insert(
                massMetadataText.rfind("ENDSEC;"),
                "#970=(CONVERSION_BASED_UNIT('gram',#976) MASS_UNIT() NAMED_UNIT(#975));\n"
                "#975=DIMENSIONAL_EXPONENTS(0.,1.,0.,0.,0.,0.,0.);\n"
                "#976=MASS_MEASURE_WITH_UNIT(MASS_MEASURE(0.001),#977);\n"
                "#977=(MASS_UNIT() NAMED_UNIT(*) SI_UNIT(.KILO.,.GRAM.));\n"
                "#978=DERIVED_UNIT((#979,#980));\n"
                "#979=DERIVED_UNIT_ELEMENT(#970,1.);\n"
                "#980=DERIVED_UNIT_ELEMENT(#10,-3.);\n"
                "#981=MEASURE_REPRESENTATION_ITEM('density',POSITIVE_RATIO_MEASURE(1.),#978);\n"
                "#982=PROPERTY_DEFINITION('density',$,#7);\n"
                "#983=PROPERTY_DEFINITION_REPRESENTATION(#982,#984);\n"
                "#984=REPRESENTATION('density',(#981),#12);\n"
            );
            auto input = step_source::InspectBytes(massMetadataText, step_source::Limits::PlanarFixtures());
            auto checked = Check(input, Limits{}, SourcePolicy::PreserveNonGeometric);
            Require(checked.status == Status::CheckedWithDiagnostics && checked.snapshot, "property-only source mass spelling");
            auto reference = checked.snapshot->Field(Index(*checked.snapshot, "970"), "dimensions");
            const auto target = Index(*checked.snapshot, "975");
            Require(
                reference.Value().kind == step_source::ValueKind::Reference && reference.ReferenceTarget() == target,
                "diagnosed mass dimensions reference must stay bound, not replaced by '*'"
            );
            input = {};
            checked = {};
            Require(
                reference.Value().text == "975" && reference.ReferenceTarget() == target,
                "diagnosed original reference retains owning source after outcome release"
            );
            ++groups;
        }
        {
            auto weightedText = Read(corpus / "S203-P04/input.step");
            for (const auto id : { "200", "201" })
            {
                const auto first = weightedText.find("#" + std::string(id) + "=");
                Require(first != std::string::npos, "weighted fixture unused line dependency");
                weightedText.erase(first, weightedText.find('\n', first) + 1 - first);
            }
            const std::string line = "LINE('',#100,#201)";
            weightedText.replace(
                weightedText.find(line),
                line.size(),
                "(BOUNDED_CURVE() B_SPLINE_CURVE(1,(#100,#101),.POLYLINE_FORM.,.F.,.F.) "
                "B_SPLINE_CURVE_WITH_KNOTS((2,2),(0.,1.),.UNSPECIFIED.) CURVE() "
                "GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_CURVE((+2.000E0,5.)) "
                "REPRESENTATION_ITEM('weighted line'))"
            );
            auto weightedInput = step_source::InspectBytes(weightedText, step_source::Limits::PlanarFixtures());
            auto weighted = Check(weightedInput, Limits{}, SourcePolicy::Strict, GeometryScope::GeneralBrep);
            Require(weighted.status == Status::Checked && weighted.snapshot, "N2635 rational source");
            Require(weighted.snapshot->RegisteredTypes().size() == 139, "N2635 general catalog scope");
            auto weights = weighted.snapshot->Field(Index(*weighted.snapshot, "202"), "weights_data");
            Require(
                &weights.Value() == &weightedInput.source->Find("202")->components[5].parameters[0],
                "weights must reference the original rational facet"
            );
            weightedInput = {};
            weighted = {};
            Require(
                weights.Value().children[0].text == "+2.000E0" && weights.Value().children[1].text == "5.",
                "owning weights retain source lexemes after outcome release"
            );
            ++groups;
        }
        std::cout << "Passed " << groups << " semantic contract groups\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Semantic contract failure after " << groups << ": " << error.what() << '\n';
        return 1;
    }
}
