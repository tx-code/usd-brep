// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSemantics.h"

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
        std::cout << "Passed " << groups << " semantic contract groups\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Semantic contract failure after " << groups << ": " << error.what() << '\n';
        return 1;
    }
}
