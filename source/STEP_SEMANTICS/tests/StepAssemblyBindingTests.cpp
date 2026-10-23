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
        Require(
            scope.faces.size() == (id == "902" ? 0 : 6) && scope.deferredRegionFaces.empty(),
            "reachable face population belongs to its definition"
        );
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
    auto badRegion = plan(Replace(source, "#507=ADVANCED_FACE('',(#506),", "#507=ADVANCED_FACE('',(#506,#516),"));
    Require(
        !badRegion.plan && badRegion.status == Status::Rejected && badRegion.diagnostics[0].source.key == "SourceRuleViolation:FACE.WR2" &&
            badRegion.diagnostics[0].source.stage == "D3" && badRegion.diagnostics[0].field == "bounds" &&
            !badRegion.diagnostics[0].source.parameterPath.empty(),
        "bad assembly face retains source rule and field diagnostic"
    );
    auto unmarked = plan(Replace(source, "#506=FACE_OUTER_BOUND", "#506=FACE_BOUND"));
    Require(
        unmarked.plan && unmarked.plan->definitions[0].deferredRegionFaces.size() == 1,
        "legal unmarked region stays deferred, never guessed or rejected as duplicate outer"
    );
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
void DefinitionChecks(const std::string& source)
{
    using namespace step_semantics;
    const auto inspect = [](const std::string& text, Limits limits = {})
    {
        auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
        return InspectAssemblyDefinitions(raw, limits, SourcePolicy::Strict);
    };
    auto result = inspect(source);
    Require(
        result.inputs && result.inputs->DefinitionChecks().size() == 2 && result.checks.status == Status::Deferred && !result.checks.snapshot &&
            !result.checks.document,
        "all definition prerequisites observed without whole-document qualification"
    );
    const auto parts = result.inputs->DefinitionChecks();
    Require(
        parts[0].shapeRole == "MaterialSolid" && parts[0].shapes.size() == 1 && parts[1].shapeRole == "AssemblyPlacementWrapper" &&
            parts[1].shapes.empty(),
        "source material and parent wrapper remain distinct"
    );
    auto badParent = Replace(source, "#902=PRODUCT_DEFINITION('parent',$,#901,#6)", "#902=PRODUCT_DEFINITION('parent',$,#901,#945)");
    badParent = Replace(badParent, "ENDSEC;\nEND-ISO", "#945=PRODUCT_DEFINITION_CONTEXT('part definition',#1,'manufacturing');\nENDSEC;\nEND-ISO");
    auto rejected = inspect(badParent);
    Require(
        !rejected.inputs && rejected.checks.part == "Unsupported" &&
            rejected.checks.diagnostics.back().source.key == "UnsupportedDefinitionLifecycleStage",
        "late parent product failure discards prior child checks"
    );
    auto missingOuter = inspect(Replace(source, "#506=FACE_OUTER_BOUND", "#506=FACE_BOUND"));
    Require(
        missingOuter.inputs && missingOuter.inputs->DefinitionChecks()[0].deferredRegionFaces.size() == 1,
        "existing native region obligation retained on checked child definition"
    );
    auto surface = inspect(Replace(
        Replace(source, "#21=MANIFOLD_SOLID_BREP('',#22)", "#21=SHELL_BASED_SURFACE_MODEL('',(#22))"),
        "#20=ADVANCED_BREP_SHAPE_REPRESENTATION",
        "#20=MANIFOLD_SURFACE_SHAPE_REPRESENTATION"
    ));
    Require(
        surface.inputs && surface.inputs->DefinitionChecks()[0].shapeRole == "ClosedSurfaceModel",
        "actual part check preserves closed surface role"
    );
    Limits limits;
    limits.dependencyVisits = result.checks.dependencyVisits;
    Require(bool(inspect(source, limits).inputs), "definition inspection shares exact total visit budget");
    --limits.dependencyVisits;
    auto stopped = inspect(source, limits);
    Require(!stopped.inputs && stopped.checks.status == Status::ResourceLimit, "late definition budget exhaustion publishes nothing");
    auto owner = result.inputs;
    auto linked = Replace(
        Replace(source, "#20=ADVANCED_BREP_SHAPE_REPRESENTATION", "#20=SHAPE_REPRESENTATION"),
        "'body',(#21,#931),#12",
        "'body',(#931),#12"
    );
    linked = Replace(
        linked,
        "ENDSEC;\nEND-ISO",
        "#950=ADVANCED_BREP_SHAPE_REPRESENTATION('body',(#21),#12);\n"
        "#951=SHAPE_REPRESENTATION_RELATIONSHIP('',$,#20,#950);\nENDSEC;\nEND-ISO"
    );
    auto linkedCheck = inspect(linked);
    Require(
        linkedCheck.inputs && linkedCheck.inputs->DefinitionChecks()[0].shapes.size() == 1,
        "existing representation selection follows ordinary child wrapper to its body"
    );
    auto sharedCategory = inspect(Replace(source, "ENDSEC;\nEND-ISO", "#950=PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,(#4,#900));\nENDSEC;\nEND-ISO"));
    Require(
        sharedCategory.inputs && sharedCategory.inputs->DefinitionChecks().size() == 2,
        "category membership uses the complete assembly definition set, not a single arbitrary product"
    );
    result = {};
    Require(
        owner->DefinitionChecks()[0].shapes.size() == 1 && owner->SourceFields().Source().Bytes() == source,
        "owning checks survive outcome release"
    );
}
void CompleteSource(const std::string& source)
{
    using namespace step_semantics;
    auto check = [](const std::string& text,
                    Limits limits = {},
                    SourcePolicy policy = SourcePolicy::Strict,
                    UnreferencedPlacementPolicy retention = UnreferencedPlacementPolicy::Reject)
    {
        auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
        return CheckAssemblySource(
            raw,
            limits,
            policy,
            SchemaInterpretation::DeclaredOnly,
            UnitInterpretation::DeclaredOnly,
            ComplexMappingInterpretation::DeclaredOnly,
            retention
        );
    };
    auto complete = check(source);
    Require(
        complete.document && complete.checks.status == Status::Checked && !complete.checks.snapshot && !complete.checks.document,
        "complete assembly source has its own owning publication interface"
    );
    Require(complete.document->DefinitionCount() == 2, "all source definitions published");
    auto child = complete.document->Definition(0), parent = complete.document->Definition(1);
    auto opaqueCheck = [](const std::string& text)
    {
        auto raw = step_source::InspectBytes(
            text,
            step_source::Limits::PlanarFixtures(),
            step_source::HeaderNamePolicy::StrictAscii,
            step_source::DataStringPolicy::PreserveOpaqueBytes
        );
        return CheckAssemblySource(raw, {}, SourcePolicy::PreserveNonGeometric);
    };
    auto opaqueSource = Replace(source, "#5=PRODUCT_DEFINITION_FORMATION('A'", std::string("#5=PRODUCT_DEFINITION_FORMATION('") + "\x80" + "'");
    auto opaque = opaqueCheck(opaqueSource);
    Require(opaque.document && !opaque.checks.preservedDiagnostics.empty(), "each independent product's singleton revision label can remain opaque");
    auto ambiguous = Replace(
        opaqueSource,
        "ENDSEC;\nEND-ISO",
        "#960=PRODUCT_DEFINITION_FORMATION('B',$,#4);\n#961=PRODUCT_DEFINITION('other',$,#960,#6);\n"
        "#962=PRODUCT_DEFINITION_SHAPE('',$,#961);\n#963=SHAPE_DEFINITION_REPRESENTATION(#962,#964);\n"
        "#964=ADVANCED_BREP_SHAPE_REPRESENTATION('other',(#21,#931),#12);\nENDSEC;\nEND-ISO"
    );
    auto deniedIdentity = opaqueCheck(ambiguous);
    Require(
        !deniedIdentity.document && std::any_of(
                                        deniedIdentity.checks.diagnostics.begin(),
                                        deniedIdentity.checks.diagnostics.end(),
                                        [](const auto& d)
                                        {
                                            return d.source.key == "OpaqueDataTextUnestablished";
                                        }
                                    ),
        "opaque label cannot choose among same-product revisions"
    );
    const auto append = [](std::string text, const std::string& records)
    {
        text.insert(text.find("ENDSEC;\nEND-ISO"), records);
        return text;
    };
    const std::string frame =
        "#950=CARTESIAN_POINT('unused',(1.,2.,3.));\n"
        "#951=DIRECTION('',(1.,0.,0.));\n#952=DIRECTION('',(0.,0.,1.));\n"
        "#953=AXIS2_PLACEMENT_3D('unused',#950,#952,#951);\n";
    auto unused = append(source, frame);
    Require(!check(unused).document, "default rejects unattached geometry");
    auto retained = check(unused, {}, SourcePolicy::Strict, UnreferencedPlacementPolicy::RetainDisconnected);
    Require(
        retained.document && retained.document->RetainedPlacements() && retained.document->RetainedPlacements()->records.size() == 4 &&
            retained.document->RetainedPlacements()->placements.size() == 1 && retained.document->DefinitionCount() == 2 &&
            retained.document->Definition(0)->PartDefinition().shapes.size() == 1 &&
            retained.document->RetainedPlacements()->originalDiagnostic.source.key == "UnownedOrOrphanProfileContent",
        "explicit policy retains exact disconnected declarations and original profile refusal without adding geometry"
    );
    Require(retained.document->Definition(0)->UnattachedNonGeometricDeclarations().empty(), "geometric residue is not mislabeled nongeometric");
    for (const auto& bad : { Replace(unused, "#951=DIRECTION('',(1.,0.,0.))", "#951=DIRECTION('',(0.,0.,0.))"),
                             Replace(unused, "#951=DIRECTION('',(1.,0.,0.))", "#951=DIRECTION('',(0.,0.,2.))"),
                             Replace(unused, "#950=CARTESIAN_POINT('unused',(1.,2.,3.))", "#950=CARTESIAN_POINT('unused',(1.,2.))"),
                             append(unused, "#954=DIRECTION('',(0.,1.,0.));\n"),
                             Replace(unused, "#950,#952,#951)", "#950,#936,#951)") })
    {
        Require(
            !check(bad, {}, SourcePolicy::Strict, UnreferencedPlacementPolicy::RetainDisconnected).document,
            "invalid, incomplete or externally connected group cannot be retained"
        );
    }
    auto capacity = check(
        Replace(unused, "#951=DIRECTION('',(1.,0.,0.))", "#951=DIRECTION('',(1.E1000,0.,0.))"),
        {},
        SourcePolicy::Strict,
        UnreferencedPlacementPolicy::RetainDisconnected
    );
    Require(!capacity.document && capacity.checks.status == Status::ResourceLimit, "bounded exact proof failure cannot become acceptance");
    auto retainBudget = Limits{};
    retainBudget.dependencyVisits = retained.checks.dependencyVisits - 1;
    Require(
        !check(unused, retainBudget, SourcePolicy::Strict, UnreferencedPlacementPolicy::RetainDisconnected).document,
        "full retention proof still obeys shared semantic visit budget"
    );
    Require(
        child->PartDefinition().shapeRole == "MaterialSolid" && parent->PartDefinition().shapeRole == "AssemblyPlacementWrapper" &&
            parent->PartDefinition().shapes.empty(),
        "qualified wrapper has no invented material body"
    );
    auto orphan = check(Replace(source, "ENDSEC;\nEND-ISO", "#950=DIRECTION('',(1.,0.,0.));\nENDSEC;\nEND-ISO"));
    Require(
        !orphan.document && orphan.checks.diagnostics.back().source.key == "UnownedOrOrphanProfileContent",
        "global closure refuses unowned geometric content despite good definitions"
    );
    auto metadata = check(Replace(
        source,
        "ENDSEC;\nEND-ISO",
        "#950=PROPERTY_DEFINITION('synthetic',$,#7);\n#951=REPRESENTATION('property',(#952),#12);\n"
        "#952=DESCRIPTIVE_REPRESENTATION_ITEM('synthetic','value');\n#953=PROPERTY_DEFINITION_REPRESENTATION(#950,#951);\nENDSEC;\nEND-ISO"
    ));
    Require(
        metadata.document && !metadata.document->Definition(0)->PartDefinition().propertyAttachments.empty() &&
            metadata.document->Definition(1)->PartDefinition().propertyAttachments.empty(),
        "inverse property attachment retained only on owning definition"
    );
    auto labels = Replace(source, "PRODUCT_DEFINITION_CONTEXT('part definition'", "PRODUCT_DEFINITION_CONTEXT('nonstandard label'");
    auto pendingOrphan = check(
        Replace(labels, "ENDSEC;\nEND-ISO", "#950=DIRECTION('',(1.,0.,0.));\nENDSEC;\nEND-ISO"),
        {},
        SourcePolicy::PreserveNonGeometric
    );
    Require(
        !pendingOrphan.document && pendingOrphan.checks.part == "Unsupported" && pendingOrphan.checks.siUnits == "Passed",
        "pending metadata does not corrupt completed unit state when closure fails"
    );
    Require(!check(labels).document, "strict definition label refusal retained");
    auto interoperable = check(labels, {}, SourcePolicy::PreserveNonGeometric);
    Require(
        interoperable.document && interoperable.checks.status == Status::CheckedWithDiagnostics &&
            !interoperable.checks.preservedDiagnostics.empty() && interoperable.checks.unapprovedDiagnostics.empty(),
        "nongeometric exceptions only approved after whole-document ownership"
    );
    Limits limits;
    limits.dependencyVisits = complete.checks.dependencyVisits;
    Require(bool(check(source, limits).document), "exact full assembly semantic budget");
    --limits.dependencyVisits;
    auto stopped = check(source, limits);
    Require(!stopped.document && stopped.checks.status == Status::ResourceLimit, "late global closure exhaustion withholds all definitions");
    complete = {};
    Require(child->Source().Bytes() == source && &child->Source() == &parent->Source(), "qualified definition views retain shared original capture");
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
        DefinitionChecks(source);
        CompleteSource(source);
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
