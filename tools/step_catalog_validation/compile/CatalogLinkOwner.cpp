// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalogBuilder.h"
namespace
{
constexpr step_semantics::catalog_build::Recipe Describe()
{
    step_semantics::catalog_build::Recipe recipe;
    recipe.declarations = { { "A", {}, { { "x", "REAL", step_semantics::FieldKind::Real } }, true } };
    return recipe;
}
using Result = step_semantics::catalog_build::CompiledCatalog<Describe>;
} // namespace
const step_semantics::Catalog& LinkedCatalog()
{
    return Result::catalog;
}
