// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportAssemblyPlacement.h"

namespace step_import::detail
{
struct AssemblyInstance
{
    std::size_t definition, rootIndex, depth;
    std::optional<std::size_t> parentIndex, usage;
    std::array<Point, 3> rotationToRoot;
    Point translationToRootMm;
};
struct AssemblyInstanceCandidate
{
    AssemblyPlacementCandidate placements;
    std::vector<std::size_t> roots;
    // Parent indices precede children and encode occurrence paths without
    // quadratic path copies. Different roots retain independent coordinate frames.
    std::vector<AssemblyInstance> instances;
};
Result<AssemblyInstanceCandidate> ExpandAssemblyInstances(
    std::shared_ptr<const step_semantics::AssemblyInputs> inputs,
    std::span<const std::size_t> roots,
    std::size_t maximumOccurrences,
    std::size_t maximumInstances,
    std::size_t maximumDepth,
    const ExecutionCheckpoint& boundary
);
} // namespace step_import::detail
