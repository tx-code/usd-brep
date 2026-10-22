// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepIndependentProducts.h"

#include <algorithm>
#include <gsl/assert>
#include <unordered_map>
#include <unordered_set>

namespace step_semantics
{
namespace
{
class Planner
{
public:

    Planner(const SemanticData& data, const ProductStructure& graph, const std::function<void(step_source::Span)>& visit)
        : d(data), g(graph), charge(visit)
    {
    }
    IndependentProductPlan Run()
    {
        Expects(d.bindingsComplete);
        if (!g.Usages().empty() || !g.Transforms().empty() || !g.ContextDependentShapes().empty())
        {
            Fail(
                "IndependentProductsContainAssembly",
                g.Definitions().front().definition,
                "Independent definitions cannot discard usage, placement or context-dependent shape relationships"
            );
        }
        const auto count = d.source->Records().size();
        std::vector<std::vector<std::size_t>> references(count);
        for (const auto& ref : d.source->References())
        {
            references[ref.recordIndex].push_back(ref.targetRecordIndex);
        }
        std::vector<std::vector<std::pair<std::size_t, std::size_t>>> adjacency(count);
        for (const auto& rel : g.RepresentationRelationships())
        {
            Visit(rel.relationship);
            adjacency[rel.firstRepresentation].push_back({ rel.secondRepresentation, rel.relationship });
            adjacency[rel.secondRepresentation].push_back({ rel.firstRepresentation, rel.relationship });
        }
        std::unordered_set<std::size_t> definitionRoots;
        for (const auto& definition : g.Definitions())
        {
            for (const auto& link : definition.representations)
            {
                definitionRoots.insert(link.representation);
            }
        }
        std::unordered_map<std::size_t, std::vector<std::size_t>> categoryByProduct, protocolByApplication;
        for (auto category : Population("PRODUCT_RELATED_PRODUCT_CATEGORY"))
        {
            for (const auto& product : Value(category, "products").children)
            {
                categoryByProduct[Target(product)].push_back(category);
            }
        }
        for (auto protocol : Population("APPLICATION_PROTOCOL_DEFINITION"))
        {
            protocolByApplication[Ref(protocol, "application")].push_back(protocol);
        }
        IndependentProductPlan result;
        for (const auto& def : g.Definitions())
        {
            Visit(def.definition);
            if (def.representations.size() != 1)
            {
                Fail(
                    "IndependentProductRepresentationMultiplicity",
                    def.definition,
                    "Each independent definition requires one explicit shape-definition representation"
                );
            }
            const auto& link = def.representations.front();
            IndependentProductScope scope;
            scope.definition = def.definition;
            const auto set = [&](std::string_view type, std::vector<std::size_t> values)
            {
                scope.populations.emplace(Schema(d).RequireType(type), std::move(values));
            };
            set("PRODUCT", { def.product });
            set("PRODUCT_DEFINITION_FORMATION", { def.formation });
            set("PRODUCT_DEFINITION", { def.definition });
            set("PRODUCT_DEFINITION_CONTEXT", { def.definitionContext });
            set("PRODUCT_DEFINITION_SHAPE", { link.shapeDefinition });
            set("SHAPE_DEFINITION_REPRESENTATION", { link.link });
            const auto& contexts = Value(def.product, "frame_of_reference").children;
            if (contexts.size() != 1)
            {
                Fail("IndependentProductContextMultiplicity", def.product, "One explicit product context is required per definition");
            }
            const auto pc = Target(contexts[0]);
            set("PRODUCT_CONTEXT", { pc });
            const auto productApplication = Ref(pc, "frame_of_reference"), definitionApplication = Ref(def.definitionContext, "frame_of_reference");
            std::vector<std::size_t> applications{ productApplication };
            if (definitionApplication != productApplication)
            {
                applications.push_back(definitionApplication);
            }
            set("APPLICATION_CONTEXT", applications);
            std::vector<std::size_t> protocols;
            for (auto app : applications)
            {
                if (auto found = protocolByApplication.find(app); found != protocolByApplication.end())
                {
                    protocols.insert(protocols.end(), found->second.begin(), found->second.end());
                }
            }
            std::sort(protocols.begin(), protocols.end());
            set("APPLICATION_PROTOCOL_DEFINITION", std::move(protocols));

            std::unordered_set<std::size_t> component, relations, geometry;
            std::vector<std::size_t> stack{ link.representation };
            while (!stack.empty())
            {
                auto r = stack.back();
                stack.pop_back();
                if (!component.insert(r).second)
                {
                    continue;
                }
                Visit(r);
                for (auto [other, rel] : adjacency[r])
                {
                    relations.insert(rel);
                    stack.push_back(other);
                }
            }
            // A relation connecting distinct definition roots is not evidence of
            // independent placement/equivalence. Keep shared source definitions,
            // but never select a surviving root or infer a transform here.
            for (auto candidate : component)
            {
                if (candidate != link.representation && definitionRoots.contains(candidate))
                {
                    Fail(
                        "IndependentProductsShareRepresentationRelationship",
                        link.representation,
                        "Representation relationships join different product roots; independent ownership is not established"
                    );
                }
            }
            stack = { link.representation };
            while (!stack.empty())
            {
                auto r = stack.back();
                stack.pop_back();
                if (!geometry.insert(r).second)
                {
                    continue;
                }
                Visit(r);
                for (auto target : references[r])
                {
                    stack.push_back(target);
                }
            }
            for (const auto type : { "REPRESENTATION",
                                     "SHAPE_REPRESENTATION",
                                     "ADVANCED_BREP_SHAPE_REPRESENTATION",
                                     "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
                                     "GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION",
                                     "CONSTRUCTIVE_GEOMETRY_REPRESENTATION" })
            {
                if (!Schema(d).Find(type))
                {
                    continue;
                }
                std::vector<std::size_t> values;
                for (auto r : component)
                {
                    if (Has(r, type))
                    {
                        values.push_back(r);
                    }
                }
                std::sort(values.begin(), values.end());
                set(type, std::move(values));
            }
            for (const auto type :
                 { "REPRESENTATION_RELATIONSHIP", "SHAPE_REPRESENTATION_RELATIONSHIP", "CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP" })
            {
                if (!Schema(d).Find(type))
                {
                    continue;
                }
                std::vector<std::size_t> values;
                for (auto r : relations)
                {
                    if (Has(r, type))
                    {
                        values.push_back(r);
                    }
                }
                std::sort(values.begin(), values.end());
                set(type, std::move(values));
            }
            std::vector<std::size_t> faces;
            for (auto r : geometry)
            {
                if (Has(r, "FACE"))
                {
                    faces.push_back(r);
                }
            }
            std::sort(faces.begin(), faces.end());
            set("FACE", std::move(faces));
            std::vector<std::size_t> categories;
            if (auto found = categoryByProduct.find(def.product); found != categoryByProduct.end())
            {
                categories = found->second;
            }
            set("PRODUCT_RELATED_PRODUCT_CATEGORY", std::move(categories));
            result.parts.push_back(std::move(scope));
        }
        return result;
    }

private:

    const SemanticData& d;
    const ProductStructure& g;
    const std::function<void(step_source::Span)>& charge;
    void Visit(std::size_t r) const
    {
        charge(d.source->Records().at(r).span);
    }
    const std::vector<std::size_t>& Population(std::string_view name) const
    {
        return d.populations.at(Schema(d).RequireType(name));
    }
    bool Has(std::size_t r, std::string_view type) const
    {
        auto t = Schema(d).Find(type);
        return t && d.types.at(r).test(*t);
    }
    bool Plain(std::size_t r) const
    {
        const auto& x = d.source->Records().at(r);
        return x.components.size() == 1 && x.components[0].name == "REPRESENTATION";
    }
    const step_source::Value& Value(std::size_t r, std::string_view name) const
    {
        Visit(r);
        return *d.fields.at(r).attributes.at(FindFieldBinding(d, r, name)).declaration.value;
    }
    std::size_t Target(const step_source::Value& value) const
    {
        return d.referenceTargets.at(&value);
    }
    std::size_t Ref(std::size_t r, std::string_view name) const
    {
        return Target(Value(r, name));
    }
    [[noreturn]] void Fail(const char* key, std::size_t r, const char* message) const
    {
        const auto& record = d.source->Records().at(r);
        Diagnostic x;
        x.source.key = key;
        x.source.stage = "D2";
        x.source.recordId = record.id;
        x.source.primary = record.span;
        x.source.message = message;
        x.classification = "ProfileUnsupported";
        x.responsibility = "complete independent-definition source selection guard";
        throw ProductStructureFailure{ Status::Unsupported, std::move(x) };
    }
};
} // namespace
IndependentProductPlan PlanIndependentProducts(
    const SemanticData& data,
    const ProductStructure& graph,
    const std::function<void(step_source::Span)>& visit
)
{
    Expects(bool(visit));
    try
    {
        return Planner(data, graph, visit).Run();
    }
    catch (ProductStructureFailure& failure)
    {
        return { {}, std::move(failure) };
    }
}
} // namespace step_semantics
