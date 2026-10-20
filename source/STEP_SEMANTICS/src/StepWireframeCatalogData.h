// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::wireframe
{
inline constexpr std::string_view SetMembers[] = { "POINT", "CURVE", "SURFACE" };
constexpr void AppendDeclarations(Recipe& recipe)
{
    // Identical explicit transport in pinned AP214 (71ab140f...), N2635
    // (c68dd022...) and modified N916 (020b4d25...), GEOMETRIC[_CURVE]_SET
    // and GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION. GRI.dim is
    // computed, not another P21 slot. GEOMETRIC_SET initially remains a guarded
    // parent. The separate AP214 surface-role recipe later admits its ordinary
    // mapping; N2635/modified N916 retain only the named curve-set leaf.
    Field elements{ "elements", "GEOMETRIC_SET_SELECT", FieldKind::Aggregate };
    elements.minimum = 1;
    elements.unique = Unique::Set;
    recipe.declarations.insert(
        recipe.declarations.end(),
        {
            { "GEOMETRIC_SET", { "GEOMETRIC_REPRESENTATION_ITEM" }, { elements } },
            { "GEOMETRIC_CURVE_SET", { "GEOMETRIC_SET" }, {}, true },
            { "GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION", { "SHAPE_REPRESENTATION" }, {}, true },
        }
    );
    recipe.referenceSelects.push_back({ "GEOMETRIC_SET_SELECT", SetMembers });
}
} // namespace step_semantics::catalog_build::wireframe
