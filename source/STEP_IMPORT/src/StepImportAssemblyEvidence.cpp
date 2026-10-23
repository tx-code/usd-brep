// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportAssemblyEvidence.h"

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
    for (std::size_t i = 0; i < candidate.definitions.size(); ++i)
    {
        const auto source = candidate.source->Definition(i);
        const auto& part = source->PartDefinition();
        if (candidate.definitions[i].bodies.size() != part.shapes.size())
        {
            throw std::logic_error("Assembly evidence body identity mismatch");
        }
        AssemblyDefinitionEvidence definition{ candidate.definitions[i].sourceDefinition, part.shapeRole, {} };
        for (const auto& shape : part.shapes)
        {
            definition.shapes.push_back(shape.shape);
        }
        evidence->definitions.push_back(std::move(definition));
    }
    return evidence;
}
} // namespace step_import::detail
