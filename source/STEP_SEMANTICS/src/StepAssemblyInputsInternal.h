// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAssemblyInputs.h"

namespace step_semantics
{
struct AssemblyInputData
{
    std::shared_ptr<const AssemblyBindingPlan> bindings;
    std::vector<AssemblyContextUnits> contexts;
};
} // namespace step_semantics
