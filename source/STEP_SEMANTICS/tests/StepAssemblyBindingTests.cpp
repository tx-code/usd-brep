// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyBindings.h"
#include "StepAssemblyDefinitions.h"
#include "StepAssemblyInputs.h"

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
void ContextInputs(const std::string& source)
{
    using namespace step_semantics;
    auto inspect = [](const std::string& text, Limits limits = {})
    {
        auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
        return InspectAssemblyInputs(raw, limits, SourcePolicy::Strict);
    };
    auto observed = inspect(source);
    Require(
        observed.checks.status == Status::Deferred && observed.inputs && !observed.checks.snapshot && !observed.checks.document,
        "assembly context observations never publish a qualified source model"
    );
    Require(
        observed.inputs->Contexts().size() == 2 && observed.inputs->Bindings().occurrences.size() == 2,
        "repeated occurrences share two checked contexts"
    );
    for (const auto& context : observed.inputs->Contexts())
    {
        Require(context.units.lengthPower10Mm == 0, "both original contexts declare millimetres");
    }
    Require(
        observed.checks.interoperability == "NotRun" && observed.checks.part == "NotQualifiedAssemblyInputs",
        "context checks cannot imply ownership or metadata approval"
    );
    const auto mixed = Replace(
        Replace(
            source,
            "#921=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11))",
            "#921=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#939,#11))"
        ),
        "ENDSEC;\nEND-ISO",
        "#939=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT($,.METRE.));\nENDSEC;\nEND-ISO"
    );
    auto different = inspect(mixed);
    Require(different.inputs && different.inputs->Contexts().size() == 2, "mixed source length contexts retained");
    for (const auto& context : different.inputs->Contexts())
    {
        const auto& id = different.inputs->SourceFields().Source().Records()[context.context].id;
        Require(context.units.lengthPower10Mm == (id == "12" ? 0 : 3), "unit scale belongs to its context, not the first part");
    }
    auto missing = inspect(Replace(
        source,
        "#921=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11))",
        "#921=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#11))"
    ));
    Require(
        !missing.inputs && !missing.checks.snapshot && !missing.checks.document &&
            missing.checks.diagnostics.back().source.key == "UnsupportedUnitAssignment",
        "late parent-unit failure discards child context observations"
    );
    auto limits = Limits{};
    limits.dependencyVisits = observed.checks.dependencyVisits;
    Require(bool(inspect(source, limits).inputs), "exact combined graph/binding/unit budget succeeds");
    --limits.dependencyVisits;
    auto stopped = inspect(source, limits);
    Require(stopped.checks.status == Status::ResourceLimit && !stopped.inputs, "one shared visit budget bounds every assembly prerequisite");
    auto owner = different.inputs;
    different = {};
    Require(
        owner->SourceFields().Source().Bytes() == mixed && owner->Contexts().size() == 2,
        "contexts and source bindings outlive the inspection result"
    );
    const auto converted = Replace(
        mixed,
        "#939=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT($,.METRE.));",
        "#939=(CONVERSION_BASED_UNIT('synthetic inch',#940) LENGTH_UNIT() NAMED_UNIT(*));\n"
        "#940=MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#10);"
    );
    auto conversion = inspect(converted);
    Require(
        conversion.inputs && conversion.checks.siUnits == "DeferredAssemblyContextConversion",
        "source conversion chain remains explicit deferred numeric work"
    );
    for (const auto& context : conversion.inputs->Contexts())
    {
        if (conversion.inputs->SourceFields().Source().Records()[context.context].id == "921")
        {
            Require(
                !context.units.lengthPower10Mm && conversion.inputs->SourceFields().Source().Records()[context.units.lengthUnit].id == "939",
                "conversion context cannot inherit the child millimetre scale"
            );
        }
    }
}
void DefinitionScopes(const std::string& source)
{
    using namespace step_semantics;
    const auto plan = [](const std::string& text, Limits limits = {})
    {
        auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
        auto input = InspectAssemblyInputs(raw, {}, SourcePolicy::Strict);
        Require(bool(input.inputs), "assembly input for definition scope");
        return PlanAssemblyDefinitions(input.inputs, limits);
    };
    auto observed = plan(source);
    Require(observed.plan && observed.plan->definitions.size() == 2, "one scope per definition, not per repeated instance");
    for (const auto& scope : observed.plan->definitions)
    {
        const auto& fields = observed.plan->inputs->SourceFields();
        const auto id = fields.Source().Records()[scope.definition].id;
        Require(scope.representations.size() == 1 && scope.relationships.empty(), "occurrence links excluded from local adjacency");
        Require(scope.hasOccurrences == (id == "902"), "placement-only parent role retained");
        Require(scope.placementOnly == (id == "902"), "parent wrapper is separate from material geometry");
        bool solid = false;
        for (auto item : scope.directItems)
        {
            solid |= fields.IsA(item, "MANIFOLD_SOLID_BREP");
        }
        Require(solid == (id == "7"), "child solid remains with its definition");
    }
    auto linked = Replace(source, "'body',(#21,#931),#12", "'body',(#931),#12");
    linked = Replace(
        linked,
        "ENDSEC;\nEND-ISO",
        "#950=ADVANCED_BREP_SHAPE_REPRESENTATION('linked',(#21),#12);\n"
        "#951=SHAPE_REPRESENTATION_RELATIONSHIP('',$,#20,#950);\nENDSEC;\nEND-ISO"
    );
    auto wrapped = plan(linked);
    Require(
        wrapped.plan && wrapped.plan->definitions[0].representations.size() == 2 && wrapped.plan->definitions[0].relationships.size() == 1,
        "ordinary same-context body link included"
    );
    auto joined = plan(Replace(source, "ENDSEC;\nEND-ISO", "#950=SHAPE_REPRESENTATION_RELATIONSHIP('',$,#20,#920);\nENDSEC;\nEND-ISO"));
    Require(
        !joined.plan && joined.diagnostics[0].source.key == "AssemblyDefinitionContextMismatch",
        "ordinary cross-context link never substitutes for occurrence placement"
    );
    auto localParent = plan(Replace(source, "'parent',(#933,#935),#921", "'parent',(#933,#935,#21),#921"));
    Require(
        localParent.plan && localParent.plan->definitions[1].directItems.size() == 3,
        "parent-owned geometry is retained, not erased by assembly role"
    );
    Limits limits;
    limits.dependencyVisits = observed.dependencyVisits;
    Require(bool(plan(source, limits).plan), "exact definition planning budget");
    --limits.dependencyVisits;
    auto stopped = plan(source, limits);
    Require(stopped.status == Status::ResourceLimit && !stopped.plan, "late scope failure withholds entire plan");
    auto owner = observed.plan;
    auto surface = plan(Replace(
        Replace(source, "#21=MANIFOLD_SOLID_BREP('',#22)", "#21=SHELL_BASED_SURFACE_MODEL('',(#22))"),
        "#20=ADVANCED_BREP_SHAPE_REPRESENTATION",
        "#20=MANIFOLD_SURFACE_SHAPE_REPRESENTATION"
    ));
    Require(surface.plan && !surface.plan->definitions[0].placementOnly, "surface definition retained");
    bool retainedSurface = false;
    for (const auto& item : surface.plan->definitions[0].items)
    {
        retainedSurface |= item.role == AssemblyItemRole::SurfaceModel;
    }
    Require(retainedSurface, "closed surface shell does not acquire material role");
    auto absent = plan(Replace(source, "'body',(#21,#931),#12", "'body',(#931),#12"));
    Require(
        !absent.plan && absent.diagnostics[0].source.key == "AssemblyLeafGeometryMissing",
        "missing leaf geometry is not an empty successful part"
    );
    auto extra = plan(Replace(source, "'parent',(#933,#935),#921", "'parent',(#933,#935,#932),#921"));
    Require(!extra.plan && extra.diagnostics[0].source.key == "AssemblyDefinitionItemUnsupported", "parent point item cannot be silently dropped");
    auto multiUse = plan(Replace(linked, "'linked',(#21),#12", "'linked',(#21,#931),#12"));
    Require(bool(multiUse.plan), "multiple memberships of the same placement retained");
    bool twoUses = false;
    for (const auto& item : multiUse.plan->definitions[0].items)
    {
        twoUses |= item.role == AssemblyItemRole::Placement && item.representations.size() == 2;
    }
    Require(twoUses, "deduplicated item inventory does not discard representation provenance");
    observed = {};
    Require(owner->inputs->SourceFields().Source().Bytes() == source, "definition plan owns original source");
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
        ContextInputs(source);
        DefinitionScopes(source);
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
            Replace(source, "ITEM_DEFINED_TRANSFORMATION('',$,#931,#933)", "ITEM_DEFINED_TRANSFORMATION('',$,#933,#931)"),
            "AssemblyTransformItemOwnershipMismatch"
        );
        Refused(Replace(source, "'body',(#21,#931),#12", "'body',(#21),#12"), "AssemblyTransformItemOwnershipMismatch");
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
        auto bindings = plan->structure->Bindings();
        const auto axis = plan->occurrences[0].childItem;
        auto location = bindings.Field(axis, "PLACEMENT", "location");
        Require(
            bindings.IsA(axis, "AXIS2_PLACEMENT_3D") && location.Declaration().declaredIn == "PLACEMENT",
            "assembly reuses inherited typed field binding"
        );
        Require(bindings.Source().Records()[location.ReferenceTarget()].id == "930", "source frame location identity");
        Require(&location.Value() == &bindings.Field(axis, "location").Value(), "named access preserves original source value identity");
        bool copiedRejected = false;
        const auto copiedReference = location.Value();
        try
        {
            bindings.ReferenceTarget(copiedReference);
        }
        catch (const std::invalid_argument&)
        {
            copiedRejected = true;
        }
        Require(copiedRejected, "copied reference values cannot forge source membership");
        bool unknownRejected = false;
        try
        {
            bindings.IsA(axis, "UNREGISTERED_FRAME_TYPE");
        }
        catch (const std::invalid_argument&)
        {
            unknownRejected = true;
        }
        Require(unknownRejected, "unregistered types are not silently absent");
        plan.reset();
        Require(
            bindings.Source().Bytes() == source && bindings.Source().Records()[location.ReferenceTarget()].id == "930",
            "typed source and field views outlive the assembly plan"
        );
        std::cout << "Assembly occurrence binding contracts passed; numeric/native qualification not run\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
