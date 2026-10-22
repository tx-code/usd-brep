// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSourceIsolation.h"

#include <algorithm>
#include <gsl/assert>
#include <unordered_set>

namespace step_semantics
{
namespace
{
class Proof
{
public:

    Proof(
        SemanticData& data,
        const ProductStructure& structure,
        std::span<const std::vector<std::size_t>> outgoing,
        const std::function<void(step_source::Span)>& visit
    )
        : d(data), graph(structure), out(outgoing), charge(visit), extra(data.types.size()), incomingExtra(data.types.size())
    {
        evidence.request = *d.selectedSourceRequest;
    }
    void Run()
    {
        const auto count = d.types.size();
        const std::unordered_set<std::string> requested(evidence.request.definitionIds.begin(), evidence.request.definitionIds.end());
        std::vector<std::size_t> selectedParts, selectedSeeds, excludedSeeds, metadataSeeds, anchors;
        std::vector<unsigned char> baseRoles(count);
        for (const auto& definition : graph.Definitions())
        {
            // These bridges are established declaration ownership, not guessed
            // transforms. They are used when expanding complete inverse metadata
            // assignments, so a mixed PD/product assignment cannot conceal the
            // geometry of one of its original targets.
            Bridge(definition.product, definition.formation);
            Bridge(definition.formation, definition.definition);
            for (const auto& link : definition.representations)
            {
                Bridge(definition.definition, link.link);
                Bridge(definition.definition, link.shapeDefinition);
            }
        }
        for (std::size_t i = 0; i < d.independentParts.size(); ++i)
        {
            const auto& part = d.independentParts[i].part;
            const auto owner = std::find_if(
                part.roles.begin(),
                part.roles.end(),
                [](const auto& role)
                {
                    return role.name == "definition";
                }
            );
            Expects(owner != part.roles.end());
            Spend(owner->recordIndex);
            const auto& id = Record(owner->recordIndex).id;
            evidence.sourceDefinitionIds.push_back(id);
            const bool selected = requested.contains(id);
            (selected ? evidence.selectedDefinitionIds : evidence.excludedDefinitionIds).push_back(id);
            auto& seeds = selected ? selectedSeeds : excludedSeeds;
            if (selected)
            {
                selectedParts.push_back(i);
            }
            for (const auto& role : part.roles)
            {
                Spend(role.recordIndex);
                baseRoles[role.recordIndex] = 1;
                seeds.push_back(role.recordIndex);
                if (selected && Anchor(role.recordIndex))
                {
                    anchors.push_back(role.recordIndex);
                }
            }
            for (const auto& attachment : part.propertyAttachments)
            {
                const std::array roots{ attachment.link,
                                        attachment.propertyDefinition,
                                        attachment.characterizedDefinition,
                                        attachment.productDefinition,
                                        attachment.representation,
                                        attachment.context };
                seeds.insert(seeds.end(), roots.begin(), roots.end());
                if (selected)
                {
                    for (auto root : roots)
                    {
                        if (Anchor(root))
                        {
                            anchors.push_back(root);
                        }
                    }
                    metadataSeeds.insert(metadataSeeds.end(), roots.begin(), roots.end());
                }
            }
        }
        Need(selectedParts.size() == requested.size() && !selectedParts.empty(), "SelectedSourceDefinitionUnavailable", 0);
        auto selected = Forward(selectedSeeds, false);
        for (std::size_t r = 0; r < count; ++r)
        {
            Spend(r);
            if (selected[r] && Has(r, "REPRESENTATION_ITEM"))
            {
                anchors.push_back(r);
            }
        }
        // Raw reverse reach finds inverse attachments of ANY selected anchor.
        // Expand the entire original attachment, never just selected style/items.
        // Unit/application contexts are not anchors: shared units alone do not
        // turn an excluded face's local bounds-count error into selected geometry.
        const auto touchingSelected = Reverse(anchors, false);
        for (const auto& role : d.part.roles)
        {
            Spend(role.recordIndex);
            if (!baseRoles[role.recordIndex] && touchingSelected[role.recordIndex])
            {
                metadataSeeds.push_back(role.recordIndex);
            }
        }
        for (auto record : d.unattachedNonGeometric)
        {
            Spend(record);
            if (touchingSelected[record])
            {
                metadataSeeds.push_back(record);
            }
        }
        const auto metadata = Forward(metadataSeeds, true);
        for (std::size_t r = 0; r < count; ++r)
        {
            Spend(r);
            selected[r] |= metadata[r];
        }
        const auto excluded = Forward(excludedSeeds, false);
        std::vector<std::size_t> failedFaces;
        for (const auto& failure : d.isolatedSourceFailures)
        {
            const auto* record = d.source->Find(failure.diagnostic.source.recordId);
            Expects(record != nullptr);
            const auto face = static_cast<std::size_t>(record - d.source->Records().data());
            Spend(face);
            // AP214E3_2010.exp (71ab140f...), FACE.WR2 reads this face's bounds.
            // No unknown clause, geometry failure or missing footprint can enter
            // this profile. Other implemented source/global obligations ran in full.
            Need(failure.diagnostic.source.key == "SourceRuleViolation:FACE.WR2" && Has(face, "FACE"), "SourceIsolationLocalityUnestablished", face);
            Need(!selected[face], "SourceIsolationSelectedInfluence", face);
            failedFaces.push_back(face);
        }
        const auto incoming = Reverse(failedFaces, true);
        for (std::size_t r = 0; r < count; ++r)
        {
            Spend(r);
            if (!incoming[r])
            {
                continue;
            }
            Need(!selected[r], "SourceIsolationSharedIncomingUse", r);
            // Unknown attribution is a refusal, not evidence of harmlessness.
            // In particular an inverse style/property/admin use outside the
            // proven excluded islands cannot disappear merely for being incoming.
            Need(excluded[r], "SourceIsolationIncomingBoundaryUnestablished", r);
        }
        evidence.failures = d.isolatedSourceFailures;
        evidence.localityEstablished = true;
        Part summary;
        summary.shapeRole = "SelectedIndependentProductDefinitions";
        for (const auto& role : d.part.roles)
        {
            if (!baseRoles[role.recordIndex] && selected[role.recordIndex])
            {
                summary.roles.push_back(role);
            }
        }
        std::vector<SemanticData::QualifiedPart> qualified;
        qualified.reserve(selectedParts.size());
        for (const auto index : selectedParts)
        {
            const auto& part = d.independentParts[index].part;
            summary.roles.insert(summary.roles.end(), part.roles.begin(), part.roles.end());
            summary.shapes.insert(summary.shapes.end(), part.shapes.begin(), part.shapes.end());
            summary.propertyAttachments.insert(summary.propertyAttachments.end(), part.propertyAttachments.begin(), part.propertyAttachments.end());
            summary.applicationProtocolPresent &= part.applicationProtocolPresent;
            qualified.push_back(std::move(d.independentParts[index]));
        }
        // No qualified source owner has escaped. Only now discard excluded
        // candidate views; the full original source and its failed facts stay.
        d.part = std::move(summary);
        d.independentParts = std::move(qualified);
        d.sourceSelection = std::move(evidence);
    }

private:

    SemanticData& d;
    const ProductStructure& graph;
    std::span<const std::vector<std::size_t>> out;
    const std::function<void(step_source::Span)>& charge;
    std::vector<std::vector<std::size_t>> extra, incomingExtra;
    SourceSelectionEvidence evidence;
    const step_source::Record& Record(std::size_t r) const
    {
        return d.source->Records().at(r);
    }
    bool Has(std::size_t r, std::string_view type) const
    {
        const auto id = Schema(d).Find(type);
        return id && d.types.at(r).test(*id);
    }
    bool Anchor(std::size_t r) const
    {
        return Has(r, "PRODUCT") || Has(r, "PRODUCT_DEFINITION_FORMATION") || Has(r, "PRODUCT_DEFINITION") || Has(r, "PRODUCT_DEFINITION_SHAPE") ||
               Has(r, "SHAPE_DEFINITION_REPRESENTATION") || Has(r, "REPRESENTATION") || Has(r, "REPRESENTATION_ITEM") ||
               Has(r, "PROPERTY_DEFINITION") || Has(r, "PROPERTY_DEFINITION_REPRESENTATION");
    }
    void Spend(std::size_t r)
    {
        if (evidence.influenceVisits >= evidence.request.maximumInfluenceVisits)
        {
            Fail(Status::ResourceLimit, "SourceIsolationInfluenceLimit", r);
        }
        ++evidence.influenceVisits;
        d.sourceInfluenceVisits = evidence.influenceVisits;
        charge(Record(r).span);
    }
    void Bridge(std::size_t from, std::size_t to)
    {
        Spend(from);
        extra[from].push_back(to);
        incomingExtra[to].push_back(from);
    }
    std::vector<unsigned char> Forward(std::vector<std::size_t> stack, bool ownership)
    {
        std::vector<unsigned char> seen(d.types.size());
        while (!stack.empty())
        {
            const auto r = stack.back();
            stack.pop_back();
            if (seen[r])
            {
                continue;
            }
            seen[r] = 1;
            Spend(r);
            for (auto ref : out[r])
            {
                Spend(r);
                stack.push_back(d.source->References()[ref].targetRecordIndex);
            }
            if (ownership)
            {
                for (auto target : extra[r])
                {
                    Spend(r);
                    stack.push_back(target);
                }
            }
        }
        return seen;
    }
    std::vector<unsigned char> Reverse(std::vector<std::size_t> stack, bool ownership)
    {
        std::vector<unsigned char> seen(d.types.size());
        while (!stack.empty())
        {
            const auto r = stack.back();
            stack.pop_back();
            if (seen[r])
            {
                continue;
            }
            seen[r] = 1;
            Spend(r);
            for (auto ref : d.source->Incoming(Record(r).id))
            {
                Spend(r);
                stack.push_back(d.source->References()[ref].recordIndex);
            }
            if (ownership)
            {
                for (auto source : incomingExtra[r])
                {
                    Spend(r);
                    stack.push_back(source);
                }
            }
        }
        return seen;
    }
    void Need(bool condition, const char* key, std::size_t r) const
    {
        if (!condition)
        {
            Fail(Status::Unsupported, key, r);
        }
    }
    [[noreturn]] void Fail(Status status, const char* key, std::size_t r) const
    {
        Diagnostic diagnostic;
        diagnostic.source.key = key;
        diagnostic.source.stage = "D2";
        diagnostic.source.recordId = Record(r).id;
        diagnostic.source.primary = Record(r).span;
        diagnostic.source.message = "Selected source clause isolation evidence is unestablished";
        diagnostic.classification = status == Status::ResourceLimit ? "ResourceLimit" : "ProfileUnsupported";
        diagnostic.responsibility = "SourceFaceClauseSelection-0.1 complete influence and incoming-boundary proof";
        throw ProductStructureFailure{ status, std::move(diagnostic) };
    }
};
} // namespace
std::optional<ProductStructureFailure> QualifySelectedSource(
    SemanticData& data,
    const ProductStructure& structure,
    std::span<const std::vector<std::size_t>> outgoing,
    const std::function<void(step_source::Span)>& visit
)
{
    Expects(data.selectedSourceRequest && bool(visit) && outgoing.size() == data.types.size());
    try
    {
        Proof(data, structure, outgoing, visit).Run();
        return {};
    }
    catch (ProductStructureFailure& failure)
    {
        return std::move(failure);
    }
}
} // namespace step_semantics
