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
        auto snapshot = result.snapshot;
        Require(snapshot->Source().Records().size() == 51, "whole source retained");
        const auto view = snapshot->Attribute(Index(*snapshot, "203"), 0, 1);
        Require(view.name == "edge_start" && view.declaredIn == "EDGE" && view.declaredType == "VERTEX", "inherited field identity");
        Require(view.value == &raw.source->Find("203")->components[0].parameters[1], "attribute view cloned source value");
        raw = {};
        result = {};
        Require(view.value->text == "110" && snapshot->Source().Find("203"), "owning semantic snapshot lifetime");
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
        ++groups;

        const auto missing = step_source::InspectBytes(Read(corpus / "S203-N03/input.step"), step_source::Limits::PlanarFixtures());
        const auto rejected = Check(missing, Limits{});
        Require(
            rejected.status == Status::SourceFailure && !rejected.snapshot && rejected.diagnostics[0].source.key == "UnresolvedRequiredReference",
            "raw source failure was turned into typed success"
        );
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
