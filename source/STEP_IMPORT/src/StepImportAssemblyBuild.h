// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportAssemblyInstances.h"
#include "StepImportNativeBuild.h"

namespace step_import::detail
{
struct AssemblyNativeDefinition
{
    std::size_t sourceDefinition;
    std::vector<NativeCandidate> bodies;
    NativeCandidate wireframe{}; // Geometry-only owner; never exposed as a ModelSnapshot/BRep.
};
struct AssemblyNativeCandidate
{
    std::shared_ptr<const step_semantics::AssemblyDocument> source;
    AssemblyInstanceCandidate instances;
    std::vector<AssemblyNativeDefinition> definitions;
};
// Private, unpublished candidate. Caller owns the native serialization lock and
// final report/commit boundary. All definitions are built exactly once; instances
// retain transforms rather than modifying or merging definition geometry.
Result<AssemblyNativeCandidate> BuildAssembly(
    std::shared_ptr<const step_semantics::AssemblyDocument> source,
    const Options& options,
    Report& report,
    Execution& execution,
    std::size_t maximumOccurrences,
    std::size_t maximumInstances,
    std::size_t maximumDepth
);
} // namespace step_import::detail
