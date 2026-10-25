// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportAssemblyEvidence.h"

#include "StepImportNativeData.h"

#include <algorithm>
#include <stdexcept>

namespace step_import::detail
{
std::shared_ptr<const AssemblyEvidence> CaptureAssemblyEvidence(const AssemblyNativeCandidate& candidate)
{
    if (!candidate.source || candidate.definitions.size() != candidate.source->DefinitionCount() || candidate.instances.instances.empty())
    {
        throw std::logic_error("Complete assembly candidate required for evidence");
    }
    auto evidence = std::make_shared<AssemblyEvidence>();
    evidence->instances = candidate.instances;
    evidence->retainedPlacements = candidate.source->RetainedPlacements();
    for (std::size_t i = 0; i < candidate.definitions.size(); ++i)
    {
        const auto source = candidate.source->Definition(i);
        const auto& part = source->PartDefinition();
        if (candidate.definitions[i].bodies.size() != part.shapes.size())
        {
            throw std::logic_error("Assembly evidence body identity mismatch");
        }
        const auto& wire = candidate.definitions[i].wireframe;
        if (bool(wire) != (part.shapeRole == "WireframeModel") || (wire && wire->brep))
        {
            throw std::logic_error("Assembly wireframe role/owner mismatch");
        }
        AssemblyDefinitionEvidence definition{ candidate.definitions[i].sourceDefinition, part.shapeRole, {}, {} };
        for (const auto& shape : part.shapes)
        {
            definition.shapes.push_back(shape.shape);
        }
        if (wire)
        {
            const auto& wireRoles = wire->source->PartDefinition().roles;
            const auto owner = std::find_if(
                wireRoles.begin(),
                wireRoles.end(),
                [](const auto& role)
                {
                    return role.name == "definition";
                }
            );
            const auto root = std::find_if(
                part.roles.begin(),
                part.roles.end(),
                [](const auto& role)
                {
                    return role.name == "representation";
                }
            );
            if (root == part.roles.end() || owner == wireRoles.end() || owner->recordIndex != definition.definition ||
                wire->source->UnitContext().lengthUnit != source->UnitContext().lengthUnit ||
                wire->source->Source().Sha256() != source->Source().Sha256())
            {
                throw std::logic_error("Assembly wireframe source mismatch");
            }
            for (const auto item : source->Field(root->recordIndex, "items").ReferenceTargets())
            {
                if (source->IsA(item, "GEOMETRIC_CURVE_SET"))
                {
                    definition.wireframeSets.push_back(item);
                }
            }
            if (definition.wireframeSets.empty() || definition.wireframeSets.size() != wire->wireframes.size())
            {
                throw std::logic_error("Assembly wireframe inventory incomplete");
            }
            for (std::size_t j = 0; j < definition.wireframeSets.size(); ++j)
            {
                const auto& group = wire->wireframes[j];
                if (group.setId != source->Source().Records()[definition.wireframeSets[j]].id ||
                    group.representationId != source->Source().Records()[root->recordIndex].id || !group.relationshipId.empty() ||
                    group.curves.size() + group.points.size() != source->Field(definition.wireframeSets[j], "elements").ReferenceTargets().size())
                {
                    throw std::logic_error("Assembly wireframe source uses incomplete");
                }
            }
        }
        evidence->definitions.push_back(std::move(definition));
    }
    return evidence;
}
} // namespace step_import::detail
