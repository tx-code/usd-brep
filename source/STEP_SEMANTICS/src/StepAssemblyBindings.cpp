// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyBindings.h"

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
class Binder
{
public:

    Binder(const ProductStructure& graph, Limits limits, std::size_t& visits) : g(graph), limits(limits), visits(visits)
    {
    }
    std::vector<AssemblyOccurrenceBinding> Run()
    {
        // This is a project construction prerequisite, not a new EXPRESS rule:
        // the fixed AP214/N916 PDU/CDSR/RRWT declarations are already bound by
        // InspectProductStructure. Join by source identity, never display names.
        std::unordered_map<std::size_t, std::size_t> representationOwner;
        for (const auto& definition : g.Definitions())
        {
            Visit(definition.definition);
            for (const auto& link : definition.representations)
            {
                Visit(link.link);
                const auto [it, inserted] = representationOwner.emplace(link.representation, definition.definition);
                if (!inserted && it->second != definition.definition)
                {
                    Fail(
                        "AssemblyRepresentationOwnershipAmbiguous",
                        link.link,
                        "Direct representation is associated with more than one product definition"
                    );
                }
            }
        }
        std::unordered_map<std::size_t, const ProductUsageLink*> usages;
        for (const auto& usage : g.Usages())
        {
            Visit(usage.usage);
            if (!usage.nextAssemblyOccurrence)
            {
                Fail("AssemblyUsageKindUnsupported", usage.usage, "Occurrence binding requires NEXT_ASSEMBLY_USAGE_OCCURRENCE");
            }
            usages.emplace(usage.usage, &usage);
        }
        if (usages.empty())
        {
            Fail("AssemblyOccurrencesRequired", std::nullopt, "No declared assembly occurrence is available");
        }
        std::unordered_map<std::size_t, const RepresentationTransformLink*> transforms;
        for (const auto& transform : g.Transforms())
        {
            Visit(transform.relationship);
            transforms.emplace(transform.relationship, &transform);
        }
        std::unordered_map<std::size_t, const ContextDependentShapeLink*> associations;
        std::unordered_set<std::size_t> usedTransforms;
        for (const auto& shape : g.ContextDependentShapes())
        {
            Visit(shape.link);
            if (!usages.contains(shape.productRelationship))
            {
                Fail("AssemblyShapeUsageUnbound", shape.link, "Context-dependent shape is not attached to a supported occurrence");
            }
            if (!associations.emplace(shape.productRelationship, &shape).second)
            {
                Fail("AssemblyOccurrenceShapeAmbiguous", shape.link, "Occurrence has more than one context-dependent shape association");
            }
        }
        std::vector<AssemblyOccurrenceBinding> result;
        result.reserve(usages.size());
        // Preserve the graph's source occurrence order. Repeated child definitions
        // remain separate bindings; this pass does not expand nested paths.
        for (const auto& usage : g.Usages())
        {
            Visit(usage.usage);
            const auto association = associations.find(usage.usage);
            if (association == associations.end())
            {
                Fail("AssemblyOccurrenceShapeMissing", usage.usage, "Occurrence has no context-dependent shape association");
            }
            const auto& shape = *association->second;
            const auto transformed = transforms.find(shape.representationRelationship);
            if (transformed == transforms.end())
            {
                Fail(
                    "AssemblyOccurrenceTransformUnestablished",
                    shape.link,
                    "Occurrence relationship has no declared item-defined transformation; identity is not inferred"
                );
            }
            const auto& t = *transformed->second;
            const auto first = representationOwner.find(t.firstRepresentation), second = representationOwner.find(t.secondRepresentation);
            if (first == representationOwner.end() || second == representationOwner.end())
            {
                Fail(
                    "AssemblyRepresentationDefinitionUnbound",
                    t.relationship,
                    "Transformed endpoints require direct definition associations in this binding scope"
                );
            }
            const bool childFirst = first->second == usage.relatedDefinition && second->second == usage.relatingDefinition;
            const bool parentFirst = first->second == usage.relatingDefinition && second->second == usage.relatedDefinition;
            if (childFirst == parentFirst)
            {
                Fail(
                    "AssemblyRepresentationUsageMismatch",
                    t.relationship,
                    "Transformed representation endpoints do not identify this occurrence's distinct parent and child definitions"
                );
            }
            // Direction is recorded, not numerically evaluated. Keep item order
            // coupled to its declared representation/context; do not silently swap
            // malformed source items or assume rep_1 always denotes the child.
            result.push_back(
                { usage.usage,
                  usage.relatingDefinition,
                  usage.relatedDefinition,
                  shape.link,
                  t.relationship,
                  t.transformation,
                  childFirst ? t.secondRepresentation : t.firstRepresentation,
                  childFirst ? t.firstRepresentation : t.secondRepresentation,
                  childFirst ? t.secondContext : t.firstContext,
                  childFirst ? t.firstContext : t.secondContext,
                  childFirst ? t.secondItem : t.firstItem,
                  childFirst ? t.firstItem : t.secondItem,
                  childFirst }
            );
            usedTransforms.insert(t.relationship);
        }
        for (const auto& t : g.Transforms())
        {
            Visit(t.relationship);
            if (!usedTransforms.contains(t.relationship))
            {
                Fail("AssemblyTransformationUnowned", t.relationship, "Declared transformation is not owned by any bound occurrence");
            }
        }
        return result;
    }

private:

    const ProductStructure& g;
    Limits limits;
    std::size_t& visits;
    void Visit(std::size_t record)
    {
        if (visits >= limits.dependencyVisits)
        {
            Fail("AssemblyBindingVisitLimit", record, "Assembly binding visit budget exhausted", Status::ResourceLimit);
        }
        ++visits;
    }
    [[noreturn]] void Fail(const char* key, std::optional<std::size_t> record, const char* message, Status status = Status::Unsupported) const
    {
        Diagnostic d;
        d.source.key = key;
        d.source.stage = "AssemblyBinding";
        d.source.message = message;
        if (record)
        {
            const auto& r = g.Source().Records().at(*record);
            d.source.recordId = r.id;
            d.source.primary = r.span;
        }
        d.classification = status == Status::ResourceLimit ? "ResourceLimit" : "ProfileUnsupported";
        d.responsibility = "direct occurrence binding prerequisite; not full source/model qualification";
        throw Failure{ status, std::move(d) };
    }
};
} // namespace
AssemblyBindingOutcome BindAssemblyOccurrences(std::shared_ptr<const ProductStructure> structure, Limits limits)
{
    if (!structure)
    {
        throw std::invalid_argument("Assembly binding requires owning declared product structure");
    }
    AssemblyBindingOutcome outcome;
    try
    {
        auto occurrences = Binder(*structure, limits, outcome.dependencyVisits).Run();
        outcome.plan = std::make_shared<const AssemblyBindingPlan>(AssemblyBindingPlan{ std::move(structure), std::move(occurrences) });
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
