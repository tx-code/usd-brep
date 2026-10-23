// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepSemantics.h"

namespace step_semantics
{
struct AssemblyOccurrenceBinding
{
    std::size_t usage, parentDefinition, childDefinition;
    std::size_t dependentShape, representationRelationship, transformation;
    std::size_t parentRepresentation, childRepresentation, parentContext, childContext;
    std::size_t parentItem, childItem;
    bool childIsFirstRepresentation;
};
// A complete direct-representation binding plan, not a qualified assembly.
// Includes direct representation.items membership. Units, numeric frames, full
// document closure, root selection and native construction remain unestablished.
struct AssemblyBindingPlan
{
    std::shared_ptr<const ProductStructure> structure;
    std::vector<AssemblyOccurrenceBinding> occurrences;
};
struct AssemblyBindingOutcome
{
    Status status = Status::Unsupported;
    std::shared_ptr<const AssemblyBindingPlan> plan;
    std::vector<Diagnostic> diagnostics;
    std::size_t dependencyVisits = 0;
};
// All declared usages participate. No partial plan survives ambiguity or budget
// exhaustion. Limits charge this pass separately from prior graph inspection.
AssemblyBindingOutcome BindAssemblyOccurrences(std::shared_ptr<const ProductStructure> structure, Limits limits);
} // namespace step_semantics
