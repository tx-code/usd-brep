// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyDefinitions.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace step_semantics
{
namespace
{
struct Failure
{
    Status status;
    Diagnostic diagnostic;
};
class Planner
{
public:

    Planner(const AssemblyInputs& inputs, Limits limits, std::size_t& visits)
        : inputs(inputs), graph(*inputs.Bindings().structure), fields(inputs.SourceFields()), limits(limits), visits(visits)
    {
    }
    std::vector<AssemblyDefinitionScope> Run()
    {
        std::unordered_map<std::size_t, std::size_t> roots;
        for (const auto& definition : graph.Definitions())
        {
            Visit(definition.definition);
            if (definition.representations.size() != 1)
            {
                Fail("AssemblyDefinitionRepresentationMultiplicity", definition.definition, "One explicit definition representation root is required");
            }
            if (!roots.emplace(definition.representations.front().representation, definition.definition).second)
            {
                Fail("AssemblyDefinitionRootAmbiguous", definition.definition, "A representation root cannot identify two distinct definitions");
            }
        }
        std::unordered_set<std::size_t> occurrenceLinks, parents;
        for (const auto& occurrence : inputs.Bindings().occurrences)
        {
            Visit(occurrence.usage);
            occurrenceLinks.insert(occurrence.representationRelationship);
            parents.insert(occurrence.parentDefinition);
        }
        std::unordered_map<std::size_t, std::vector<std::pair<std::size_t, std::size_t>>> adjacency;
        for (const auto& relation : graph.RepresentationRelationships())
        {
            Visit(relation.relationship);
            // Occurrence relationships place definitions; they must never join
            // their local geometry populations. Keep them in the owning plan.
            if (occurrenceLinks.contains(relation.relationship))
            {
                continue;
            }
            if (relation.transformation)
            {
                Fail(
                    "AssemblyUnboundPlacementRelationship",
                    relation.relationship,
                    "Unbound transformed relationships cannot be treated as local geometry"
                );
            }
            if (!relation.shapeRelationship)
            {
                continue; // Other links remain whole-document closure obligations.
            }
            if (relation.firstContext != relation.secondContext)
            {
                Fail(
                    "AssemblyDefinitionContextMismatch",
                    relation.relationship,
                    "Local shape links require identical contexts; equivalence is not inferred"
                );
            }
            adjacency[relation.firstRepresentation].push_back({ relation.secondRepresentation, relation.relationship });
            adjacency[relation.secondRepresentation].push_back({ relation.firstRepresentation, relation.relationship });
        }
        std::unordered_map<std::size_t, std::size_t> owners;
        std::vector<AssemblyDefinitionScope> result;
        for (const auto& definition : graph.Definitions())
        {
            const auto& root = definition.representations.front();
            if (!inputs.FindContext(root.context))
            {
                Fail(
                    "AssemblyDefinitionUnitContextUnestablished",
                    root.representation,
                    "Definition context has not been checked by assembly input inspection"
                );
            }
            AssemblyDefinitionScope scope{
                definition.definition, root.representation, root.context, {}, {}, {}, parents.contains(definition.definition), {}, false
            };
            std::unordered_set<std::size_t> seen, relationships, items;
            std::unordered_map<std::size_t, std::vector<std::size_t>> memberships;
            std::vector<std::size_t> stack{ root.representation };
            while (!stack.empty())
            {
                const auto representation = stack.back();
                stack.pop_back();
                if (!seen.insert(representation).second)
                {
                    continue;
                }
                Visit(representation);
                const auto otherRoot = roots.find(representation);
                if (otherRoot != roots.end() && otherRoot->second != definition.definition)
                {
                    Fail("AssemblyDefinitionsJoinedWithoutOccurrence", representation, "An ordinary shape link joins different definition roots");
                }
                const auto [owner, inserted] = owners.emplace(representation, definition.definition);
                if (!inserted && owner->second != definition.definition)
                {
                    Fail("AssemblyDefinitionRepresentationShared", representation, "Local representation membership is ambiguous between definitions");
                }
                if (fields.Field(representation, "context_of_items").ReferenceTarget() != scope.context)
                {
                    Fail("AssemblyDefinitionContextMismatch", representation, "Local representation has a different source context");
                }
                for (const auto& item : fields.Field(representation, "items").Value().children)
                {
                    Visit(representation);
                    const auto target = fields.ReferenceTarget(item);
                    items.insert(target);
                    memberships[target].push_back(representation);
                }
                scope.representations.push_back(representation);
                for (const auto& [other, relationship] : adjacency[representation])
                {
                    Visit(relationship);
                    relationships.insert(relationship);
                    stack.push_back(other);
                }
            }
            // Stable source-record order is evidence ordering, not precedence.
            // Keep all direct item identities, including parent-owned geometry.
            std::sort(scope.representations.begin(), scope.representations.end());
            scope.relationships.assign(relationships.begin(), relationships.end());
            scope.directItems.assign(items.begin(), items.end());
            std::sort(scope.relationships.begin(), scope.relationships.end());
            std::sort(scope.directItems.begin(), scope.directItems.end());
            bool hasGeometry = false;
            for (const auto item : scope.directItems)
            {
                Visit(item);
                AssemblyItemRole role;
                // Nominal source roles from the already bound fixed profile,
                // not inferred from closure, volume, rendering or parenthood.
                // A surface model with a CLOSED_SHELL remains a surface model.
                if (fields.IsA(item, "AXIS2_PLACEMENT_3D"))
                {
                    role = AssemblyItemRole::Placement;
                }
                else if (fields.IsA(item, "MANIFOLD_SOLID_BREP"))
                {
                    role = AssemblyItemRole::MaterialSolid;
                }
                else if (fields.IsA(item, "SHELL_BASED_SURFACE_MODEL"))
                {
                    role = AssemblyItemRole::SurfaceModel;
                }
                else
                {
                    Fail(
                        "AssemblyDefinitionItemUnsupported",
                        item,
                        "Definition item requires an explicit supported geometry or placement role; it cannot be discarded"
                    );
                }
                hasGeometry |= role != AssemblyItemRole::Placement;
                auto& uses = memberships.at(item);
                std::sort(uses.begin(), uses.end());
                scope.items.push_back({ item, role, std::move(uses) });
            }
            scope.placementOnly = !hasGeometry;
            if (scope.placementOnly && !scope.hasOccurrences)
            {
                Fail("AssemblyLeafGeometryMissing", scope.definition, "A definition without child occurrences cannot stand in for missing geometry");
            }
            result.push_back(std::move(scope));
        }
        return result;
    }

private:

    const AssemblyInputs& inputs;
    const ProductStructure& graph;
    const SourceBindings& fields;
    Limits limits;
    std::size_t& visits;
    void Visit(std::size_t record)
    {
        if (visits >= limits.dependencyVisits)
        {
            Fail("AssemblyDefinitionVisitLimit", record, "Definition scope budget exhausted", Status::ResourceLimit);
        }
        ++visits;
    }
    [[noreturn]] void Fail(const char* key, std::size_t record, const char* message, Status status = Status::Unsupported) const
    {
        const auto& source = fields.Source().Records().at(record);
        Diagnostic d;
        d.source.key = key;
        d.source.stage = "AssemblyDefinitions";
        d.source.recordId = source.id;
        d.source.primary = source.span;
        d.source.message = message;
        d.classification = status == Status::ResourceLimit ? "ResourceLimit" : "ProfileUnsupported";
        d.responsibility = "same-definition representation scope; not full document/geometry qualification";
        throw Failure{ status, std::move(d) };
    }
};
} // namespace
AssemblyDefinitionOutcome PlanAssemblyDefinitions(std::shared_ptr<const AssemblyInputs> inputs, Limits limits)
{
    if (!inputs)
    {
        throw std::invalid_argument("Assembly definition planning requires owning inputs");
    }
    AssemblyDefinitionOutcome outcome;
    try
    {
        auto definitions = Planner(*inputs, limits, outcome.dependencyVisits).Run();
        outcome.plan = std::make_shared<const AssemblyDefinitionPlan>(AssemblyDefinitionPlan{ std::move(inputs), std::move(definitions) });
        outcome.status = Status::Checked;
    }
    catch (Failure& failure)
    {
        outcome.status = failure.status;
        outcome.diagnostics.push_back(std::move(failure.diagnostic));
    }
    return outcome;
}
} // namespace step_semantics
