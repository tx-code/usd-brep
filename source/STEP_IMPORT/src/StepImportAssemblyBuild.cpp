// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportAssemblyBuild.h"

#include "StepImportGroupedBuild.h"
#include "StepImportObservation.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace step_import::detail
{
Result<AssemblyNativeCandidate> BuildAssembly(
    std::shared_ptr<const step_semantics::AssemblyDocument> source,
    const Options& options,
    Report& report,
    Execution& execution,
    std::size_t maximumOccurrences,
    std::size_t maximumInstances,
    std::size_t maximumDepth
)
{
    if (!source)
    {
        throw std::invalid_argument("Assembly construction requires complete source ownership");
    }
    if (!options.generalBrep || options.faceOrientationRepair || options.cylindricalBandRepair || options.definitionSelection ||
        options.sourceErrorIsolation)
    {
        return tl::unexpected(
            Failure{ Status::Unsupported,
                     "AssemblyBuildConfigurationUnsupported",
                     "Assembly construction requires explicit general scope without repair",
                     {},
                     "AssemblyBuild" }
        );
    }
    auto inputs = std::shared_ptr<const step_semantics::AssemblyInputs>(source, &source->Inputs());
    auto instances = ExpandAssemblyInstances(
        inputs,
        inputs->Bindings().structure->Roots(),
        maximumOccurrences,
        maximumInstances,
        maximumDepth,
        [&]
        {
            return execution.Check(Phase::BeforeNativeCall);
        }
    );
    if (!instances)
    {
        return tl::unexpected(std::move(instances.error()));
    }
    // Refuse unsupported associated geometry before allocating any native body.
    // Every declared definition participates; no post-failure survivor selection.
    for (std::size_t i = 0; i < source->DefinitionCount(); ++i)
    {
        if (auto step = execution.Check(Phase::BeforeNativeCall); !step)
        {
            return tl::unexpected(std::move(step.error()));
        }
        const auto& part = source->Definition(i)->PartDefinition();
        if (part.shapeRole == "WireframeModel")
        {
            // Source qualification does not create BRep topology or qualify a
            // detached curve factory. Until definition-owned native geometry
            // is implemented, refuse the complete request before any body is
            // allocated; never drop the wire leaf or call it a wrapper.
            const auto root = std::find_if(
                part.roles.begin(),
                part.roles.end(),
                [](const auto& role)
                {
                    return role.name == "representation";
                }
            );
            if (root == part.roles.end())
            {
                throw std::logic_error("Wireframe definition requires its qualified source root");
            }
            return tl::unexpected(
                Failure{ Status::Unsupported,
                         "AssemblyWireframeConstructionUnimplemented",
                         "Wireframe definitions require independent owning native geometry",
                         root->recordIndex,
                         "AssemblyBuild" }
            );
        }
        for (const auto& role : source->Definition(i)->PartDefinition().roles)
        {
            if (auto step = execution.Check(Phase::BeforeNativeCall); !step)
            {
                return tl::unexpected(std::move(step.error()));
            }
            if (role.name == "wireframe_representation" || role.name == "construction_representation")
            {
                return tl::unexpected(
                    Failure{ Status::Unsupported,
                             "AssemblyAssociatedGeometryUnimplemented",
                             "Associated geometry must remain owned by its definition",
                             role.recordIndex,
                             "AssemblyBuild" }
                );
            }
        }
    }
    AssemblyNativeCandidate candidate;
    candidate.source = source;
    candidate.instances = std::move(*instances);
    std::vector<std::size_t> selected(source->DefinitionCount());
    std::iota(selected.begin(), selected.end(), 0);
    std::size_t bodyCount = 0;
    return ObserveDefinitions(
        *source,
        selected,
        report,
        execution,
        [&](const DefinitionMember& member, Report& checks) -> Result<void>
        {
            if (auto step = execution.Check(Phase::BeforeNativeCall); !step)
            {
                return tl::unexpected(std::move(step.error()));
            }
            const auto& definition = member.source->PartDefinition();
            AssemblyNativeDefinition native{ member.sourceDefinitionRecord, {} };
            if (definition.shapeRole == "AssemblyPlacementWrapper")
            {
                if (!definition.shapes.empty())
                {
                    throw std::logic_error("Assembly wrapper acquired geometry");
                }
                checks.numericalIntegration.reset(); // No integral was performed for a container.
            }
            else if (definition.shapes.size() > 1)
            {
                checks.materialCollection = true;
                auto bodies = BuildMaterialCollection(member.source, options, checks, execution);
                if (!bodies)
                {
                    return tl::unexpected(std::move(bodies.error()));
                }
                native.bodies = std::move(*bodies);
            }
            else
            {
                auto body = BuildGeneral(member.source, options, checks, execution);
                if (!body)
                {
                    return tl::unexpected(std::move(body.error()));
                }
                native.bodies.push_back(std::move(*body));
            }
            bodyCount += native.bodies.size();
            candidate.definitions.push_back(std::move(native));
            return {};
        },
        [&]() -> Result<AssemblyNativeCandidate>
        {
            if (!bodyCount)
            {
                return tl::unexpected(
                    Failure{ Status::Unsupported, "AssemblyGeometryMissing", "No native body exists in the complete assembly", {}, "AssemblyBuild" }
                );
            }
            if (auto step = execution.Check(Phase::NativeReady); !step)
            {
                return tl::unexpected(std::move(step.error()));
            }
            return std::move(candidate);
        }
    );
}
} // namespace step_import::detail
