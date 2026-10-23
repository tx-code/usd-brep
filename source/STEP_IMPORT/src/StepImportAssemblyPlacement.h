// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportUnits.h"

namespace step_import::detail
{
struct AssemblyContextScale
{
    std::size_t context;
    ResolvedLengthScale length;
};
struct AssemblyOccurrencePlacement
{
    std::size_t usage;
    std::array<Point, 3> rotation; // Row-major child-mm -> parent-mm, not world coordinates.
    Point translationMm;
    double orthonormalResidual;
};
struct AssemblyPlacementCandidate
{
    std::shared_ptr<const step_semantics::AssemblyInputs> source;
    std::vector<AssemblyContextScale> contexts;
    std::vector<AssemblyOccurrencePlacement> occurrences;
};
// Numerical observation only: no native geometry, source closure or continuous
// geometric-error certificate. Axes are right-handed IDT placements; other
// transform families are not inferred. All-or-nothing and cooperatively cancellable.
Result<AssemblyPlacementCandidate> EvaluateAssemblyPlacements(
    std::shared_ptr<const step_semantics::AssemblyInputs> inputs,
    std::size_t maximumOccurrences,
    const ExecutionCheckpoint& boundary
);
} // namespace step_import::detail
