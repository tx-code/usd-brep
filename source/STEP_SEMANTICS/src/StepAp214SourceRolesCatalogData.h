// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::ap214_source_roles
{
inline constexpr std::string_view InvisibleItems[] = { "REPRESENTATION", "STYLED_ITEM" };
inline constexpr std::string_view InvisibleItemsWithLayers[] = { "PRESENTATION_LAYER_ASSIGNMENT", "REPRESENTATION", "STYLED_ITEM" };

constexpr void AppendDeclarations(Recipe& recipe, bool hasLayers)
{
    // AP214E3_2010.exp (71ab140f...), PRODUCT_CATEGORY[_RELATIONSHIP] and
    // INVISIBILITY/invisible_item. Category.id is computed, not a P21 slot.
    // DRAUGHTING_CALLOUT remains an unregistered SELECT branch; the seven-arc
    // catalog also keeps layers outside its narrower presentation subset.
    SetSimple(recipe, "PRODUCT_CATEGORY");
    Field invisible{ "invisible_items", "INVISIBLE_ITEM", FieldKind::Aggregate };
    invisible.minimum = 1;
    invisible.unique = Unique::Set;
    recipe.declarations.insert(
        recipe.declarations.end(),
        {
            { "PRODUCT_CATEGORY_RELATIONSHIP",
              {},
              { { "name", "LABEL", FieldKind::String },
                { "description", "TEXT", FieldKind::String, true },
                { "category", "PRODUCT_CATEGORY", FieldKind::Reference },
                { "sub_category", "PRODUCT_CATEGORY", FieldKind::Reference } },
              true },
            { "INVISIBILITY", {}, { invisible }, true },
        }
    );
    recipe.referenceSelects.push_back(
        { "INVISIBLE_ITEM",
          hasLayers ? std::span<const std::string_view>(InvisibleItemsWithLayers) : std::span<const std::string_view>(InvisibleItems) }
    );
}
} // namespace step_semantics::catalog_build::ap214_source_roles
