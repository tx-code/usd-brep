// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::ap214_surface_role
{
constexpr void AppendDeclarations(Recipe& recipe)
{
    // AP214E3_2010.exp (71ab140f...), these entities' exact explicit
    // declarations. Append after existing identities. GEOMETRIC_SET's parent
    // declaration/SELECT already exists in the wireframe recipe; admitting its
    // ordinary mapping does not make it a GEOMETRIC_CURVE_SET or material BRep.
    SetSimple(recipe, "GEOMETRIC_SET");
    recipe.declarations.insert(
        recipe.declarations.end(),
        {
            { "GEOMETRICALLY_BOUNDED_SURFACE_SHAPE_REPRESENTATION", { "SHAPE_REPRESENTATION" }, {}, true },
            { "SPHERICAL_SURFACE", { "ELEMENTARY_SURFACE" }, { { "radius", "POSITIVE_LENGTH_MEASURE", FieldKind::Real } }, true },
        }
    );
}
} // namespace step_semantics::catalog_build::ap214_surface_role
