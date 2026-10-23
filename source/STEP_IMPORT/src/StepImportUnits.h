// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAssemblyInputs.h"
#include "StepImport.h"
#include "StepImportExecutionCallbacks.h"

namespace step_import::detail
{
struct ResolvedLengthScale
{
    Fraction scaleMm;
    std::vector<ConversionUnitDefinition> definitions; // Base-to-root order; source declarations stay immutable.
};
// Interpret ONLY a checked nominal LENGTH_UNIT chain ending at SI METRE.
// Unit dimensions/SELECT bindings belong to STEP_SEMANTICS; this pass does not
// infer quantity from dimensions, names or unrelated property units. Each
// conversion's exact source decimal is multiplied once, with checked 4096-bit
// magnitude capacity. Preserve wider factor/intermediate ratios as exact integer
// strings, and narrow only the final mm ratio to Fraction. The callback checks
// cooperative resource/cancellation boundaries, including arithmetic loops.
// Canonical source contract: source/STEP_SEMANTICS/README.md, conversion-unit
// external mappings and unit-dimensional dependencies; positivity here is a
// project physical-normalization prerequisite, not a new source WHERE clause.
[[nodiscard]] Result<ResolvedLengthScale> ResolveLengthScale(
    const step_semantics::Snapshot& source,
    std::size_t root,
    const ExecutionCheckpoint& boundary
);
// Context identity must belong to this owning, checked assembly input receipt.
// Uses the same exact conversion engine as qualified single-part normalization.
[[nodiscard]] Result<ResolvedLengthScale> ResolveLengthScale(
    const step_semantics::AssemblyInputs& inputs,
    std::size_t context,
    const ExecutionCheckpoint& boundary
);
} // namespace step_import::detail
