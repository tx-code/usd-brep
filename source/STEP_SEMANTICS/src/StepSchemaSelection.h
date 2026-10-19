// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepSemantics.h"

namespace step_semantics
{
// Private selection module. Recognition and permission to use the existing catalog are distinct.
SchemaSelection SelectSchema(const step_source::SourceSnapshot& source, GeometryScope scope);
// Explicit application interpretation only; never modifies HEADER recognition
// or registers an unverified OID as a publisher/ISO binding.
std::optional<QualifiedSchemaProfile> SelectExplicitInterpretation(const SchemaSelection& selection, SchemaInterpretation interpretation);
} // namespace step_semantics
