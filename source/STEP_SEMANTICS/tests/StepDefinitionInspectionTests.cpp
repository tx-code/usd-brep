// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSemantics.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
using namespace step_semantics;
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
std::string Read(const char* path)
{
    std::ifstream stream(path, std::ios::binary);
    Require(bool(stream), "public independent-definition fixture required");
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}
void Replace(std::string& text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    Require(at != std::string::npos, "public mutation token required");
    text.replace(at, from.size(), to);
}
void Append(std::string& text, const std::string& record)
{
    text.insert(text.rfind("ENDSEC;"), record + "\n");
}
void BreakFace(std::string& text, bool second = false)
{
    const std::string prefix = second ? "10" : "";
    const std::string bound = second ? "990001" : "990000";
    Replace(text, "ADVANCED_FACE('',(#" + prefix + "506)", "ADVANCED_FACE('',(#" + prefix + "506,#" + bound + ")");
    Append(text, "#" + bound + "=FACE_OUTER_BOUND('',#" + prefix + "505,.T.);");
}
Outcome Inspect(const std::string& text, Limits limits = {})
{
    const auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
    return InspectIndependentDefinitions(raw, limits, SourcePolicy::Strict);
}
void NoQualification(const Outcome& result)
{
    Require(!result.snapshot && !result.document, "definition observations cannot publish qualified source");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "distinct and shared public fixtures required");
        const auto source = Read(argv[1]);
        auto complete = Inspect(source);
        NoQualification(complete);
        Require(
            complete.status == Status::Deferred && complete.definitionInspection && complete.definitionInspection->AllDefinitionsVisited(),
            "all-green observations remain diagnostic only"
        );
        const auto checks = complete.definitionInspection->Checks();
        Require(
            checks.size() == 2 && checks[0].part == "CheckedPrerequisites" && checks[1].units == "Checked",
            "independent prerequisites and contexts are observed separately"
        );
        auto retained = complete.definitionInspection;
        complete = {};
        Require(
            retained->Structure().Source().Bytes() == source && retained->Checks().size() == 2,
            "immutable observations own original source after outcome release"
        );
        auto single = source;
        for (const auto id : { "900", "901", "902", "903", "904", "920" })
        {
            const auto start = single.find("#" + std::string(id) + "=");
            Require(start != std::string::npos, "public second definition record");
            single.erase(start, single.find('\n', start) + 1 - start);
        }
        const auto singleResult = Inspect(single);
        Require(
            singleResult.definitionInspection && singleResult.definitionInspection->Checks().size() == 1 &&
                singleResult.definitionInspection->AllDefinitionsVisited(),
            "diagnostic mode inventories a single declared definition"
        );
        NoQualification(singleResult);
        auto broken = source;
        BreakFace(broken);
        const auto one = Inspect(broken);
        NoQualification(one);
        Require(
            one.status == Status::Rejected && one.definitionInspection->AllDefinitionsVisited() &&
                one.definitionInspection->Checks()[0].part == "Rejected" && one.definitionInspection->Checks()[0].units == "Checked" &&
                one.definitionInspection->Checks()[1].part == "CheckedPrerequisites" && one.definitionInspection->Checks()[1].units == "Checked",
            "first region failure does not suppress independent later observations"
        );
        BreakFace(broken, true);
        const auto both = Inspect(broken);
        Require(
            both.definitionInspection->Checks()[0].diagnostics.size() == 1 && both.definitionInspection->Checks()[1].diagnostics.size() == 1 &&
                both.definitionInspection->Checks()[0].diagnostics[0].source.recordId == "507" &&
                both.definitionInspection->Checks()[1].diagnostics[0].source.recordId == "10507",
            "source failures retain each original face identity"
        );
        auto shared = Read(argv[2]);
        BreakFace(shared);
        const auto sharedResult = Inspect(shared);
        Require(
            sharedResult.definitionInspection->Checks()[0].part == "Rejected" && sharedResult.definitionInspection->Checks()[1].part == "Rejected",
            "shared invalid geometry affects both definitions, never just one inventory owner"
        );
        auto badUnits = source;
        Replace(
            badUnits,
            "ADVANCED_BREP_SHAPE_REPRESENTATION('second',(#10021),#12)",
            "ADVANCED_BREP_SHAPE_REPRESENTATION('second',(#10021),#990002)"
        );
        Append(
            badUnits,
            "#990002=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10)) "
            "REPRESENTATION_CONTEXT('missing angle','3D'));"
        );
        const auto unitResult = Inspect(badUnits);
        Require(
            unitResult.definitionInspection && unitResult.definitionInspection->Checks()[0].units == "Checked" &&
                unitResult.definitionInspection->Checks()[1].units == "Unsupported",
            "unit failures remain definition-scoped observations"
        );
        auto unknown = source;
        Append(unknown, "#990000=UNKNOWN_INFLUENCE(#507);");
        const auto unknownResult = Inspect(unknown);
        Require(
            unknownResult.status == Status::Unsupported && !unknownResult.definitionInspection,
            "unknown field footprint cannot enter per-definition checking"
        );
        auto globalUnitFailure = source;
        Replace(globalUnitFailure, "SI_UNIT(.MILLI.,.METRE.)", "SI_UNIT(.MILLI.,.SECOND.)");
        const auto global = Inspect(globalUnitFailure);
        Require(
            global.status == Status::Unsupported && !global.definitionInspection,
            "global unit dependency failure still precedes all definition observations"
        );
        auto metadata = source;
        Replace(metadata, "PRODUCT_DEFINITION_CONTEXT('part definition'", "PRODUCT_DEFINITION_CONTEXT('unapproved caption'");
        const auto metadataRaw = step_source::InspectBytes(metadata, step_source::Limits::PlanarFixtures());
        const auto metadataResult = InspectIndependentDefinitions(metadataRaw, Limits{}, SourcePolicy::PreserveNonGeometric);
        Require(
            metadataResult.definitionInspection && !metadataResult.definitionInspection->UnapprovedDiagnostics().empty() &&
                metadataResult.preservedDiagnostics.empty(),
            "provisional metadata facts are retained without approving an exception"
        );
        NoQualification(metadataResult);
        auto budget = Limits{};
        budget.dependencyVisits = Inspect(source).dependencyVisits - 1;
        const auto limited = Inspect(source, budget);
        NoQualification(limited);
        Require(
            limited.status == Status::ResourceLimit && limited.definitionInspection && !limited.definitionInspection->AllDefinitionsVisited(),
            "visit exhaustion stops remaining observations"
        );
        budget = {};
        budget.preservedDiagnostics = 1;
        const auto diagnosticLimit = Inspect(broken, budget);
        Require(
            diagnosticLimit.status == Status::ResourceLimit && diagnosticLimit.definitionInspection &&
                !diagnosticLimit.definitionInspection->AllDefinitionsVisited(),
            "diagnostic budget is cumulative across definitions"
        );
        std::cout << "definition inspection contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
