// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportAssemblyBuild.h"

namespace step_import::detail
{
struct AssemblyDefinitionEvidence
{
    std::size_t definition;
    std::string role;
    std::vector<std::size_t> shapes;
    std::vector<std::size_t> wireframeSets{};
};
struct AssemblyEvidence
{
    // No ModelData/BRep references: a failed report cannot retain native candidates.
    AssemblyInstanceCandidate instances;
    std::vector<AssemblyDefinitionEvidence> definitions;
    std::optional<step_semantics::RetainedPlacementDeclarations> retainedPlacements;
};
std::shared_ptr<const AssemblyEvidence> CaptureAssemblyEvidence(const AssemblyNativeCandidate& candidate);
Result<void> SerializeAssemblyFailure(
    Status status,
    const Report& report,
    const Quality& quality,
    const step_source::InspectOutcome& source,
    std::size_t limit,
    std::string& output
);
Result<void> SerializeAssembly(
    Status status,
    const Report& report,
    const Quality& quality,
    const step_source::InspectOutcome& source,
    const AssemblyEvidence& assembly,
    std::size_t limit,
    std::string& output
);
} // namespace step_import::detail
