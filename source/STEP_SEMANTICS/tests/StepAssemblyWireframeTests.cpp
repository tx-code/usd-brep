// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyDefinitions.h"

#include <algorithm>
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
std::string Replace(std::string text, std::string_view from, std::string_view to)
{
    const auto at = text.find(from);
    Require(at != std::string::npos, "public mutation target");
    text.replace(at, from.size(), to);
    return text;
}
AssemblySourceOutcome Check(const std::string& text, Limits limits = {}, SourcePolicy policy = SourcePolicy::Strict)
{
    const auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
    return CheckAssemblySource(raw, limits, policy);
}
void Refused(const std::string& text, std::string_view key, Status status = Status::Unsupported)
{
    // Geometry/unit defects remain blocking under nongeometric interoperability.
    for (auto policy : { SourcePolicy::Strict, SourcePolicy::PreserveNonGeometric })
    {
        auto result = Check(text, {}, policy);
        Require(!result.document && result.checks.status == status, "one defective leaf withholds complete source qualification");
        Require(
            std::any_of(
                result.checks.diagnostics.begin(),
                result.checks.diagnostics.end(),
                [&](const auto& diagnostic)
                {
                    return diagnostic.source.key == key && !diagnostic.source.recordId.empty();
                }
            ),
            "stable diagnostic retains its actual source identity"
        );
    }
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "public assembly wireframe fixture required");
        std::ifstream file(argv[1], std::ios::binary);
        Require(bool(file), "fixture exists");
        const std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
        auto inputs = InspectAssemblyInputs(raw, {}, SourcePolicy::Strict);
        Require(bool(inputs.inputs), "complete typed assembly inputs");
        auto plan = PlanAssemblyDefinitions(inputs.inputs, {});
        Require(plan.plan && plan.plan->definitions.size() == 3, "one scope per definition, never per occurrence");
        const auto& fields = plan.plan->inputs->SourceFields();
        const auto& scope = plan.plan->definitions.back();
        Require(
            fields.Source().Records()[scope.definition].id == "962" && scope.wireframeOnly && !scope.placementOnly && !scope.hasOccurrences &&
                scope.representations.size() == 1 && scope.relationships.empty() && scope.faces.empty(),
            "wire leaf has explicit physical geometry without invented local relationship or BRep faces"
        );
        Require(
            std::count_if(
                scope.items.begin(),
                scope.items.end(),
                [](const auto& item)
                {
                    return item.role == AssemblyItemRole::Wireframe;
                }
            ) == 1,
            "geometric curve set retains its nominal role and direct membership"
        );
        auto inspected = InspectAssemblyDefinitions(raw, {}, SourcePolicy::Strict);
        Require(
            inspected.inputs && inspected.checks.status == Status::Deferred && !inspected.checks.snapshot && !inspected.checks.document &&
                inspected.inputs->DefinitionChecks().back().shapeRole == "WireframeModel",
            "definition observations do not imply whole-file ownership"
        );
        auto result = Check(text);
        Require(result.document && result.checks.status == Status::Checked, "complete strict source checks include the wire leaf");
        auto document = result.document;
        Require(document->DefinitionCount() == 3 && document->Inputs().Bindings().occurrences.size() == 3, "every definition and usage retained");
        auto wire = document->Definition(2);
        const auto& part = wire->PartDefinition();
        Require(part.shapeRole == "WireframeModel" && part.shapes.empty(), "wire geometry is neither an empty solid nor a placement wrapper");
        Require(
            document->Definition(0)->PartDefinition().shapeRole == "MaterialSolid" &&
                document->Definition(1)->PartDefinition().shapeRole == "AssemblyPlacementWrapper",
            "other source roles are unchanged"
        );
        const auto representation = std::find_if(
            part.roles.begin(),
            part.roles.end(),
            [](const auto& role)
            {
                return role.name == "representation";
            }
        );
        Require(representation != part.roles.end() && wire->Source().Records()[representation->recordIndex].id == "967", "actual SDR root retained");
        Require(
            std::none_of(
                part.roles.begin(),
                part.roles.end(),
                [](const auto& role)
                {
                    return role.name == "wireframe_shape_relationship";
                }
            ),
            "a direct definition does not fabricate associated-geometry links"
        );
        const auto set = wire->Field(representation->recordIndex, "items").ReferenceTargets().back();
        Require(wire->IsA(set, "GEOMETRIC_CURVE_SET"), "source set membership");
        const auto members = wire->Field(set, "elements").ReferenceTargets();
        Require(
            members.size() == 3 && wire->IsA(members[0], "TRIMMED_CURVE") && wire->IsA(members[1], "TRIMMED_CURVE"),
            "all curves and points retained"
        );
        Require(
            wire->Field(members[1], "trim_1").Value().children.front().children.front().text == "4." &&
                wire->Field(members[1], "trim_2").Value().children.front().children.front().text == "3." &&
                wire->Field(members[1], "sense_agreement").Value().text == "F" && wire->UnitContext().lengthPower10Mm == 0,
            "reverse spline trim parameters and source length units stay untouched"
        );
        Limits exact;
        exact.dependencyVisits = result.checks.dependencyVisits;
        Require(bool(Check(text, exact).document), "exact shared semantic work budget succeeds");
        --exact.dependencyVisits;
        auto limited = Check(text, exact);
        Require(!limited.document && limited.checks.status == Status::ResourceLimit, "N-1 work cannot publish a partial assembly source");
        Refused(
            Replace(text, "#967=GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION", "#967=SHAPE_REPRESENTATION"),
            "AssemblyDefinitionItemUnsupported"
        );
        const auto linkedMixed = Replace(
            text,
            "ENDSEC;\nEND-ISO",
            "#985=GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION('associated',(#968),#12);\n"
            "#986=SHAPE_REPRESENTATION_RELATIONSHIP('',$,#20,#985);\nENDSEC;\nEND-ISO"
        );
        Refused(linkedMixed, "AssemblyDefinitionItemUnsupported");
        Refused(
            Replace(text, "#968=GEOMETRIC_CURVE_SET('',(#974,#975,#976))", "#968=GEOMETRIC_CURVE_SET('',(#202,#975,#976))"),
            "SourceRuleViolation:GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION.WR3",
            Status::Rejected
        );
        Refused(
            Replace(text, "#968=GEOMETRIC_CURVE_SET('',(#974,#975,#976))", "#968=GEOMETRIC_CURVE_SET('',(#504,#975,#976))"),
            "SourceRuleViolation:GEOMETRIC_CURVE_SET.WR1",
            Status::Rejected
        );
        const auto missingLength = Replace(
            Replace(
                text,
                "#967=GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION('wire',(#931,#968),#12)",
                "#967=GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION('wire',(#931,#968),#983)"
            ),
            "ENDSEC;\nEND-ISO",
            "#983=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#11)) REPRESENTATION_CONTEXT('wire','3D'));\nENDSEC;\nEND-ISO"
        );
        Refused(missingLength, "UnsupportedUnitAssignment");
        const auto orphan = Replace(text, "ENDSEC;\nEND-ISO", "#990=CARTESIAN_POINT('unowned',(1.,2.,3.));\nENDSEC;\nEND-ISO");
        Refused(orphan, "UnownedOrOrphanProfileContent");
        result = {};
        raw = {};
        inputs = {};
        inspected = {};
        plan = {};
        document.reset();
        Require(
            wire->Source().Bytes() == text && wire->PartDefinition().shapeRole == "WireframeModel",
            "wire source snapshot owns all original bytes and evidence"
        );
        std::cout << "Assembly wire-only source contracts passed; native construction not qualified\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
