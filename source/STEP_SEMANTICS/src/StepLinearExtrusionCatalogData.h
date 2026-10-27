// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::linear_extrusion
{
inline constexpr std::string_view SweptKinds[] = { "SURFACE_OF_LINEAR_EXTRUSION", "SURFACE_OF_REVOLUTION" };
constexpr void AppendDeclarations(Recipe& recipe)
{
    // Pinned AP214E3_2010.exp (71ab140f...), SURFACE_OF_LINEAR_EXTRUSION
    // and SWEPT_SURFACE's ONEOF. No local WHERE is declared on this entity.
    // VECTOR.WR1 still allows zero; native rank/finite-chart restrictions are
    // separate from source validity. Append without renumbering prior types.
    recipe.declarations.push_back(
        { "SURFACE_OF_LINEAR_EXTRUSION", { "SWEPT_SURFACE" }, { { "extrusion_axis", "VECTOR", FieldKind::Reference } }, true }
    );
}
} // namespace step_semantics::catalog_build::linear_extrusion
