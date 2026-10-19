// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

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
// conversion's exact source decimal is multiplied once, with bounded rational
// capacity. The callback checks cooperative resource/cancellation boundaries.
// Canonical source contract: source/STEP_SEMANTICS/README.md, conversion-unit
// external mappings and unit-dimensional dependencies; positivity here is a
// project physical-normalization prerequisite, not a new source WHERE clause.
ResolvedLengthScale ResolveLengthScale(const step_semantics::Snapshot& source, std::size_t root, const std::function<void()>& boundary);
} // namespace step_import::detail
