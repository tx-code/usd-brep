// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportDefinitionSelection.h"

#include "StepImportExecution.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>

namespace step_import::detail
{
std::vector<std::size_t> SelectNativeDefinitions(
    const step_semantics::ProductDocument& source,
    const Options& options,
    Report& report,
    Execution& execution
)
{
    std::vector<std::size_t> indices;
    if (const auto& owned = source.SourceSelection())
    {
        execution.Check(Phase::BeforeDefinitionSelection);
        if (!options.definitionSelection || !options.sourceErrorIsolation || !owned->localityEstablished ||
            owned->request.sourceSha256 != report.sourceSha256 || owned->request.sourceSha256 != options.definitionSelection->sourceSha256 ||
            owned->request.definitionIds != options.definitionSelection->definitionIds ||
            owned->request.maximumIsolatedFailures != options.sourceErrorIsolation->maximumFailures ||
            owned->request.maximumInfluenceVisits != options.sourceErrorIsolation->maximumInfluenceVisits ||
            owned->selectedDefinitionIds.size() != source.PartCount())
        {
            throw Failure{ Status::Unsupported,
                           "SourceSelectionReceiptMismatch",
                           "Native selection requires the owning checked source request",
                           {},
                           "Selection" };
        }
        DefinitionSelectionReceipt receipt;
        receipt.sourceSha256 = owned->request.sourceSha256;
        receipt.configuration = step_semantics::SourceFaceClauseSelectionProfile;
        receipt.sourceScope = owned->failures.empty() ? "AllDefinitionsChecked" : "SelectedDefinitionsOnly";
        receipt.sourceDefinitionIds = owned->sourceDefinitionIds;
        receipt.selectedDefinitionIds = owned->selectedDefinitionIds;
        receipt.excludedDefinitionIds = owned->excludedDefinitionIds;
        receipt.isolatedSourceFailureCount = owned->failures.size();
        indices.resize(source.PartCount());
        std::iota(indices.begin(), indices.end(), 0);
        for (const auto i : indices)
        {
            const auto& roles = source.Part(i)->PartDefinition().roles;
            const auto owner = std::find_if(
                roles.begin(),
                roles.end(),
                [](const auto& role)
                {
                    return role.name == "definition";
                }
            );
            if (owner == roles.end() || source.Source().Records().at(owner->recordIndex).id != receipt.selectedDefinitionIds[i])
            {
                throw Failure{ Status::Unsupported,
                               "SourceSelectionReceiptMismatch",
                               "Qualified source views must match selected declaration identities",
                               {},
                               "Selection" };
            }
        }
        report.definitionSelection = std::move(receipt);
        execution.Check(Phase::DefinitionSelectionReady);
        return indices;
    }
    if (options.sourceErrorIsolation)
    {
        throw Failure{ Status::Unsupported,
                       "SourceSelectionReceiptRequired",
                       "Source clause isolation requires owning source locality evidence",
                       {},
                       "Selection" };
    }
    if (!options.definitionSelection)
    {
        indices.resize(source.PartCount());
        std::iota(indices.begin(), indices.end(), 0);
        return indices;
    }
    execution.Check(Phase::BeforeDefinitionSelection);
    const auto& request = *options.definitionSelection;
    if (request.sourceSha256 != report.sourceSha256)
    {
        throw Failure{ Status::Unsupported,
                       "DefinitionSelectionInputMismatch",
                       "Selection requires the captured original source identity",
                       {},
                       "Selection" };
    }
    std::unordered_set<std::string> remaining;
    for (const auto& id : request.definitionIds)
    {
        if (!remaining.insert(id).second)
        {
            throw Failure{ Status::Unsupported, "DuplicateSelectedDefinition", "A definition may be requested only once", {}, "Selection" };
        }
    }
    DefinitionSelectionReceipt receipt;
    receipt.sourceSha256 = report.sourceSha256;
    for (std::size_t i = 0; i < source.PartCount(); ++i)
    {
        execution.Check(Phase::BeforeDefinitionSelection);
        const auto part = source.Part(i);
        const auto& roles = part->PartDefinition().roles;
        const auto definition = std::find_if(
            roles.begin(),
            roles.end(),
            [](const auto& role)
            {
                return role.name == "definition";
            }
        );
        if (definition == roles.end())
        {
            throw std::logic_error("QualifiedDefinitionIdentityRequired");
        }
        const auto& id = source.Source().Records().at(definition->recordIndex).id;
        receipt.sourceDefinitionIds.push_back(id);
        if (remaining.erase(id))
        {
            receipt.selectedDefinitionIds.push_back(id);
            indices.push_back(i);
        }
        else
        {
            receipt.excludedDefinitionIds.push_back(id);
        }
    }
    if (!remaining.empty() || indices.empty())
    {
        throw Failure{ Status::Unsupported,
                       "SelectedDefinitionUnavailable",
                       "Every requested ID must name a qualified independent product definition",
                       {},
                       "Selection" };
    }
    if (options.faceOrientationRepair)
    {
        for (const auto& edit : options.faceOrientationRepair->edits)
        {
            if (std::find(receipt.selectedDefinitionIds.begin(), receipt.selectedDefinitionIds.end(), edit.definitionId) ==
                receipt.selectedDefinitionIds.end())
            {
                throw Failure{ Status::Unsupported,
                               "RepairOutsideDefinitionSelection",
                               "Repair cannot change an unselected definition",
                               {},
                               "Selection" };
            }
        }
    }
    // No partial semantic snapshot is created. Shared geometry/contexts retain
    // the original complete source bindings; each selected builder borrows its
    // qualified definition view and publishes only after ALL selected checks.
    // Excluded definitions are NotRequested natively, never accepted or healed.
    report.definitionSelection = std::move(receipt);
    execution.Check(Phase::DefinitionSelectionReady);
    return indices;
}
} // namespace step_import::detail
