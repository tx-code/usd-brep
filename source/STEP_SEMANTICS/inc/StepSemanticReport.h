// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepSemantics.h"
#include "StepSourceReport.h"

// Internal report composition; keeps schema-specific serialization in STEP_SEMANTICS.
namespace step_semantics::report
{
void WriteJson(step_source::report::Writer& writer, const step_source::InspectOutcome& source, const Outcome& outcome);
void WriteSchemaSelection(step_source::report::Writer& writer, const std::optional<SchemaSelection>& selection);
} // namespace step_semantics::report
