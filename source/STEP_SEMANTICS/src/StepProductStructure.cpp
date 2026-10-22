// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepProductStructure.h"

#include <gsl/assert>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

namespace step_semantics
{
struct ProductStructureData
{
    std::shared_ptr<const step_source::SourceSnapshot> source;
    ProductStructureBasis basis;
    std::vector<ProductDefinitionLink> definitions;
    std::vector<ProductUsageLink> usages;
    std::vector<ContextDependentShapeLink> dependentShapes;
    std::vector<RepresentationTransformLink> transforms;
    std::vector<ProductRepresentationRelationship> relationships;
    std::vector<std::size_t> roots;
};
ProductStructure::ProductStructure(std::shared_ptr<const ProductStructureData> data) : m_data(std::move(data))
{
    Expects(m_data && m_data->source);
}
const step_source::SourceSnapshot& ProductStructure::Source() const
{
    return *m_data->source;
}
const ProductStructureBasis& ProductStructure::Basis() const
{
    return m_data->basis;
}
std::span<const ProductDefinitionLink> ProductStructure::Definitions() const
{
    return m_data->definitions;
}
std::span<const ProductUsageLink> ProductStructure::Usages() const
{
    return m_data->usages;
}
std::span<const ContextDependentShapeLink> ProductStructure::ContextDependentShapes() const
{
    return m_data->dependentShapes;
}
std::span<const RepresentationTransformLink> ProductStructure::Transforms() const
{
    return m_data->transforms;
}
std::span<const ProductRepresentationRelationship> ProductStructure::RepresentationRelationships() const
{
    return m_data->relationships;
}
std::span<const std::size_t> ProductStructure::Roots() const
{
    return m_data->roots;
}

namespace
{
class Builder
{
public:

    Builder(const SemanticData& data, const std::function<void(step_source::Span)>& visit, bool includeSingleDefinition)
        : m_data(data), m_visit(visit), m_includeSingleDefinition(includeSingleDefinition)
    {
    }
    ProductStructureResult Run()
    {
        if (!HasDeclaredProductStructureProfile(m_data.profile.kind) || m_data.geometryScope != GeometryScope::GeneralBrep)
        {
            return {};
        }
        if (!m_includeSingleDefinition && Population("PRODUCT").size() <= 1 && Population("PRODUCT_DEFINITION").size() <= 1 &&
            Population("PRODUCT_DEFINITION_USAGE").empty() && Population("CONTEXT_DEPENDENT_SHAPE_REPRESENTATION").empty() &&
            Population("REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION").empty())
        {
            return {}; // No extra work/charge/order change for the established single-part path.
        }
        Expects(m_data.bindingsComplete);
        auto output = std::make_shared<ProductStructureData>();
        output->source = m_data.source;
        output->basis = { m_data.profile,
                          m_data.appliedInterpretation,
                          m_data.policy,
                          m_data.complexMappingAssumptions.empty() ? ComplexMappingInterpretation::DeclaredOnly :
                                                                     ComplexMappingInterpretation::RegisteredFacetNames,
                          m_data.complexMappingDiagnostics };
        const auto count = m_data.source->Records().size();
        constexpr auto absent = std::numeric_limits<std::size_t>::max();
        std::vector<std::size_t> definitionIndex(count, absent);
        for (const auto record : Population("PRODUCT_DEFINITION"))
        {
            const auto formation = Ref(record, "formation");
            definitionIndex[record] = output->definitions.size();
            output->definitions.push_back({ record, formation, Ref(formation, "of_product"), Ref(record, "frame_of_reference"), {} });
        }
        for (const auto record : Population("SHAPE_DEFINITION_REPRESENTATION"))
        {
            const auto shape = Ref(record, "definition");
            const auto owner = Ref(shape, "definition");
            if (Has(owner, "PRODUCT_DEFINITION"))
            {
                Expects(definitionIndex[owner] != absent);
                const auto representation = Ref(record, "used_representation");
                output->definitions[definitionIndex[owner]].representations.push_back(
                    { record, shape, representation, Ref(representation, "context_of_items") }
                );
            }
        }
        std::map<std::tuple<std::string, std::size_t, std::size_t>, std::size_t> uniqueUsages;
        std::vector<std::vector<std::pair<std::size_t, std::size_t>>> outgoing(output->definitions.size());
        std::vector<unsigned char> child(output->definitions.size());
        for (const auto record : Population("PRODUCT_DEFINITION_USAGE"))
        {
            const auto parent = Ref(record, "relating_product_definition");
            const auto related = Ref(record, "related_product_definition");
            const auto& id = Value(record, "id");
            if (id.kind != step_source::ValueKind::String)
            {
                Fail(
                    record,
                    "id",
                    "ProductUsageIdentifierEncodingUnestablished",
                    "Usage uniqueness requires an interpreted source identifier",
                    Status::Unsupported
                );
            }
            // AP214E3_2010.exp (71ab140f...) / modified N916 (020b4d25...), PDU.UR1:
            // the identity tuple includes BOTH definitions. PRODUCT.id and
            // occurrence names never select or merge a product/instance.
            const auto [entry, inserted] = uniqueUsages.emplace(std::tuple{ id.text, parent, related }, record);
            if (!inserted)
            {
                Fail(
                    record,
                    "id",
                    "SourceRuleViolation:PRODUCT_DEFINITION_USAGE.UR1",
                    "Usage identifier and both product definitions duplicate another usage",
                    Status::Rejected,
                    entry->second
                );
            }
            Expects(definitionIndex[parent] != absent && definitionIndex[related] != absent);
            outgoing[definitionIndex[parent]].push_back({ definitionIndex[related], record });
            child[definitionIndex[related]] = 1;
            output->usages.push_back({ record, parent, related, Has(record, "NEXT_ASSEMBLY_USAGE_OCCURRENCE") });
        }
        // PDU.WR1 applies to the whole usage population, not a chosen root or
        // a recursion-depth sample. Iterative DFS retains edge identity and
        // charges every traversed edge through the original semantic budget.
        std::vector<unsigned char> color(output->definitions.size());
        for (std::size_t root = 0; root < output->definitions.size(); ++root)
        {
            Visit(output->definitions[root].definition);
            if (!child[root])
            {
                output->roots.push_back(output->definitions[root].definition);
            }
            if (color[root])
            {
                continue;
            }
            std::vector<std::pair<std::size_t, std::size_t>> stack{ { root, 0 } };
            color[root] = 1;
            while (!stack.empty())
            {
                auto& [node, next] = stack.back();
                if (next == outgoing[node].size())
                {
                    color[node] = 2;
                    stack.pop_back();
                    continue;
                }
                const auto [target, usage] = outgoing[node][next++];
                Visit(usage);
                if (color[target] == 1)
                {
                    Fail(
                        usage,
                        "related_product_definition",
                        "SourceRuleViolation:PRODUCT_DEFINITION_USAGE.WR1",
                        "Product definition usage graph contains a cycle",
                        Status::Rejected,
                        output->definitions[target].definition
                    );
                }
                if (!color[target])
                {
                    color[target] = 1;
                    stack.push_back({ target, 0 });
                }
            }
        }
        for (const auto record : Population("REPRESENTATION_RELATIONSHIP"))
        {
            const auto first = Ref(record, "rep_1"), second = Ref(record, "rep_2");
            const auto firstContext = Ref(first, "context_of_items"), secondContext = Ref(second, "context_of_items");
            const bool shape = Has(record, "SHAPE_REPRESENTATION_RELATIONSHIP");
            if (shape && !Has(first, "SHAPE_REPRESENTATION") && !Has(second, "SHAPE_REPRESENTATION"))
            {
                Fail(
                    record,
                    "rep_1",
                    "SourceRuleViolation:SHAPE_REPRESENTATION_RELATIONSHIP.WR1",
                    "Neither representation has the declared shape representation role",
                    Status::Rejected
                );
            }
            const bool transformed = Has(record, "REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION");
            const auto transform = transformed ? std::optional<std::size_t>(Ref(record, "transformation_operator")) : std::nullopt;
            output->relationships.push_back({ record, first, second, firstContext, secondContext, shape, transform });
            if (!transformed)
            {
                continue;
            }
            if (firstContext == secondContext)
            {
                Fail(
                    record,
                    "rep_2",
                    "SourceRuleViolation:REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION.WR1",
                    "Transformed representations must refer to distinct source contexts",
                    Status::Rejected,
                    first
                );
            }
            output->transforms.push_back(
                { record,
                  first,
                  second,
                  firstContext,
                  secondContext,
                  *transform,
                  Ref(*transform, "transform_item_1"),
                  Ref(*transform, "transform_item_2") }
            );
        }
        for (const auto record : Population("CONTEXT_DEPENDENT_SHAPE_REPRESENTATION"))
        {
            const auto relationship = Ref(record, "representation_relation");
            const auto shape = Ref(record, "represented_product_relation");
            const auto productRelationship = Ref(shape, "definition");
            if (!Has(productRelationship, "PRODUCT_DEFINITION_RELATIONSHIP"))
            {
                Fail(
                    record,
                    "represented_product_relation",
                    "SourceRuleViolation:CONTEXT_DEPENDENT_SHAPE_REPRESENTATION.WR1",
                    "Context dependent shape must reference a product definition relationship",
                    Status::Rejected,
                    shape
                );
            }
            // Modified N916 has CDSR.WR1 only. The AP214 name/description
            // derived attributes and inverse WHERE clauses must not leak into it.
            if (IsAp214SourceProfile(m_data.profile.kind))
            {
                InverseLimit(record, "DESCRIPTION_ATTRIBUTE", "described_item", "SourceRuleViolation:CONTEXT_DEPENDENT_SHAPE_REPRESENTATION.WR2");
                InverseLimit(record, "NAME_ATTRIBUTE", "named_item", "SourceRuleViolation:CONTEXT_DEPENDENT_SHAPE_REPRESENTATION.WR3");
            }
            output->dependentShapes.push_back({ record, relationship, shape, productRelationship });
        }
        return { std::make_shared<const ProductStructure>(std::move(output)), {} };
    }

private:

    const SemanticData& m_data;
    const std::function<void(step_source::Span)>& m_visit;
    bool m_includeSingleDefinition = false;
    void Visit(std::size_t record) const
    {
        m_visit(m_data.source->Records().at(record).span);
    }
    const std::vector<std::size_t>& Population(std::string_view type) const
    {
        return m_data.populations.at(Schema(m_data).RequireType(type));
    }
    bool Has(std::size_t record, std::string_view type) const
    {
        const auto id = Schema(m_data).Find(type);
        return id && m_data.types.at(record).test(*id);
    }
    const step_source::Value& Value(std::size_t record, std::string_view name) const
    {
        Visit(record);
        return *m_data.fields.at(record).attributes.at(FindFieldBinding(m_data, record, name)).declaration.value;
    }
    std::size_t Ref(std::size_t record, std::string_view name) const
    {
        return m_data.referenceTargets.at(&Value(record, name));
    }
    void InverseLimit(std::size_t target, std::string_view owner, std::string_view field, const char* key) const
    {
        const auto type = Schema(m_data).Find(owner);
        // Unregistered types cannot occur after complete mapping. This is the
        // closed-profile zero-population proof, not a general empty USEDIN.
        if (!type)
        {
            return;
        }
        std::size_t count = 0;
        for (auto record : m_data.populations.at(*type))
        {
            if (Ref(record, field) == target && ++count > 1)
            {
                Fail(record, field, key, "More than one source attribute assignment targets the context dependent shape", Status::Rejected, target);
            }
        }
    }
    [[noreturn]] void Fail(
        std::size_t record,
        std::string_view field,
        const char* key,
        const char* message,
        Status status,
        std::optional<std::size_t> related = {}
    ) const
    {
        const auto& bound = m_data.fields.at(record).attributes.at(FindFieldBinding(m_data, record, field));
        const auto& source = m_data.source->Records().at(record);
        Diagnostic d;
        d.source.key = key;
        d.source.stage = "D3";
        d.source.message = message;
        d.source.primary = bound.declaration.value->span;
        d.source.recordId = source.id;
        d.source.component = source.components.at(bound.component).name;
        d.source.parameterPath.push_back(bound.parameter + 1);
        if (related)
        {
            d.source.related.push_back(m_data.source->Records().at(*related).idSpan);
        }
        d.field = field;
        d.declaredIn = bound.declaration.declaredIn;
        d.declaredType = bound.declaration.declaredType;
        d.classification = status == Status::Rejected ? "SourceInvalid" : "ProfileUnsupported";
        d.responsibility = std::string(SourceArtifactName(m_data.profile.kind)) + ": declared product structure prerequisites only";
        throw ProductStructureFailure{ status, std::move(d) };
    }
};
} // namespace
ProductStructureResult InspectProductStructure(
    const SemanticData& data,
    const std::function<void(step_source::Span)>& visit,
    bool includeSingleDefinition
)
{
    Expects(bool(visit));
    try
    {
        return Builder(data, visit, includeSingleDefinition).Run();
    }
    catch (ProductStructureFailure& failure)
    {
        return { {}, std::move(failure) };
    }
}
} // namespace step_semantics
