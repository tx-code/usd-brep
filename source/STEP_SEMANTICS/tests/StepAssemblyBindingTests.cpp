// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyBindings.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
std::shared_ptr<const step_semantics::ProductStructure> Graph(const std::string& text)
{
    auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
    auto checked = step_semantics::Check(raw, {}, step_semantics::SourcePolicy::Strict, step_semantics::GeometryScope::GeneralBrep);
    Require(
        checked.status == step_semantics::Status::Unsupported && !checked.snapshot && checked.productStructure,
        "complete declared assembly graph, without single-part admission"
    );
    return checked.productStructure;
}
std::string Replace(std::string text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    Require(at != std::string::npos, "synthetic mutation target");
    text.replace(at, from.size(), to);
    return text;
}
void Refused(const std::string& source, const char* key)
{
    auto result = step_semantics::BindAssemblyOccurrences(Graph(source), {});
    Require(
        result.status == step_semantics::Status::Unsupported && !result.plan && result.diagnostics.size() == 1,
        "ambiguous assembly withholds the entire binding plan"
    );
    Require(result.diagnostics[0].source.key == key && !result.diagnostics[0].source.recordId.empty(), "stable refusal with source identity");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "pass synthetic assembly fixture");
        std::ifstream stream(argv[1], std::ios::binary);
        Require(bool(stream), "fixture exists");
        const std::string source{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        auto graph = Graph(source);
        auto result = step_semantics::BindAssemblyOccurrences(graph, {});
        Require(
            result.status == step_semantics::Status::Checked && result.plan && result.plan->occurrences.size() == 2,
            "both repeated occurrences bind"
        );
        auto plan = result.plan;
        const auto id = [&](std::size_t record) -> const std::string&
        {
            return plan->structure->Source().Records()[record].id;
        };
        Require(id(plan->occurrences[0].usage) == "910" && id(plan->occurrences[1].usage) == "916", "distinct source usages retained");
        for (const auto& occurrence : plan->occurrences)
        {
            Require(id(occurrence.parentDefinition) == "902" && id(occurrence.childDefinition) == "7", "parent/child roles");
            Require(id(occurrence.parentRepresentation) == "920" && id(occurrence.childRepresentation) == "20", "representation ownership");
            Require(id(occurrence.parentContext) == "921" && id(occurrence.childContext) == "12", "separate source contexts");
            Require(occurrence.childIsFirstRepresentation && id(occurrence.childItem) == "931", "source transform direction");
        }
        Require(id(plan->occurrences[0].parentItem) == "933" && id(plan->occurrences[1].parentItem) == "935", "distinct target frames retained");
        auto limits = step_semantics::Limits{};
        limits.dependencyVisits = result.dependencyVisits;
        Require(bool(step_semantics::BindAssemblyOccurrences(graph, limits).plan), "exact visit budget succeeds");
        --limits.dependencyVisits;
        auto stopped = step_semantics::BindAssemblyOccurrences(graph, limits);
        Require(
            stopped.status == step_semantics::Status::ResourceLimit && !stopped.plan &&
                stopped.diagnostics[0].source.key == "AssemblyBindingVisitLimit",
            "late budget failure releases every partial binding"
        );
        const auto reversed = Replace(
            Replace(source, "REPRESENTATION_RELATIONSHIP('',$,#20,#920)", "REPRESENTATION_RELATIONSHIP('',$,#920,#20)"),
            "ITEM_DEFINED_TRANSFORMATION('',$,#931,#933)",
            "ITEM_DEFINED_TRANSFORMATION('',$,#933,#931)"
        );
        auto inverse = step_semantics::BindAssemblyOccurrences(Graph(reversed), {});
        Require(
            inverse.plan && !inverse.plan->occurrences[0].childIsFirstRepresentation &&
                inverse.plan->occurrences[0].childItem == plan->occurrences[0].childItem &&
                inverse.plan->occurrences[0].parentItem == plan->occurrences[0].parentItem,
            "reversed relationship binds by ownership, not rep_1 convention"
        );
        Refused(Replace(source, "#918=CONTEXT_DEPENDENT_SHAPE_REPRESENTATION(#924,#917);", ""), "AssemblyOccurrenceShapeMissing");
        Refused(
            Replace(source, "ENDSEC;\nEND-ISO", "#950=CONTEXT_DEPENDENT_SHAPE_REPRESENTATION(#914,#911);\nENDSEC;\nEND-ISO"),
            "AssemblyOccurrenceShapeAmbiguous"
        );
        auto unbound = Replace(source, "REPRESENTATION_RELATIONSHIP('',$,#20,#920)", "REPRESENTATION_RELATIONSHIP('',$,#20,#951)");
        Refused(
            Replace(unbound, "ENDSEC;\nEND-ISO", "#951=SHAPE_REPRESENTATION('',(#933),#921);\nENDSEC;\nEND-ISO"),
            "AssemblyRepresentationDefinitionUnbound"
        );
        Refused(
            Replace(
                source,
                "ENDSEC;\nEND-ISO",
                "#950=(REPRESENTATION_RELATIONSHIP('',$,#20,#920) REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION(#915) "
                "SHAPE_REPRESENTATION_RELATIONSHIP());\nENDSEC;\nEND-ISO"
            ),
            "AssemblyTransformationUnowned"
        );
        Refused(
            Replace(
                source,
                "NEXT_ASSEMBLY_USAGE_OCCURRENCE('instance-a','same label',$,#902,#7,$)",
                "PRODUCT_DEFINITION_USAGE('instance-a','same label',$,#902,#7)"
            ),
            "AssemblyUsageKindUnsupported"
        );
        Refused(
            Replace(source, "#904=SHAPE_DEFINITION_REPRESENTATION(#903,#920)", "#904=SHAPE_DEFINITION_REPRESENTATION(#903,#20)"),
            "AssemblyRepresentationOwnershipAmbiguous"
        );
        result = {};
        graph.reset();
        Require(plan->structure->Source().Bytes() == source && id(plan->occurrences[1].usage) == "916", "binding plan owns immutable source lifetime");
        std::cout << "Assembly occurrence binding contracts passed; numeric/native qualification not run\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
