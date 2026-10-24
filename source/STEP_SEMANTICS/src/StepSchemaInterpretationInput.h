// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepSemantics.h"

#include <tl/expected.hpp>

namespace step_semantics
{
// Request/input identity evidence only; never selects an EXPRESS implementation
// or approves the source's units, geometry, topology, ownership or publication.
tl::expected<void, Diagnostic> ValidateSchemaInterpretationInput(
    const step_source::InspectOutcome& source,
    const SchemaInterpretationRequest& request
);
} // namespace step_semantics
