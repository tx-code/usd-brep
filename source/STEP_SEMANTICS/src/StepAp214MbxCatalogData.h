// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::ap214_mbx
{
inline constexpr std::string_view derivedProperty[] = { "PROPERTY_DEFINITION" };
inline constexpr std::string_view layeredItem[] = { "REPRESENTATION_ITEM" };
constexpr void AddAp214MbxDeclarations(Recipe& catalog)
{
    Field assignedItems{ "assigned_items", "LAYERED_ITEM", FieldKind::Aggregate };
    assignedItems.minimum = 1;
    assignedItems.unique = Unique::Set;
    // AP214E3_2010.exp (71ab140f...), GENERAL_PROPERTY[_ASSOCIATION]
    // and OVER_RIDING_STYLED_ITEM: exact transport fields, in source order.
    // The derived-property SELECT is deliberately restricted to the one
    // admitted branch. Registration cannot grant selected-part ownership.
    catalog.declarations.insert(
        catalog.declarations.end(),
        {
            { "GENERAL_PROPERTY",
              {},
              { { "id", "IDENTIFIER", FieldKind::String },
                { "name", "LABEL", FieldKind::String },
                { "description", "TEXT", FieldKind::String, true } },
              true },
            { "GENERAL_PROPERTY_ASSOCIATION",
              {},
              { { "name", "LABEL", FieldKind::String },
                { "description", "TEXT", FieldKind::String, true },
                { "base_definition", "GENERAL_PROPERTY", FieldKind::Reference },
                { "derived_definition", "DERIVED_PROPERTY_SELECT", FieldKind::Reference } },
              true },
            { "OVER_RIDING_STYLED_ITEM", { "STYLED_ITEM" }, { { "over_ridden_style", "STYLED_ITEM", FieldKind::Reference } }, true },
            { "PRESENTATION_LAYER_ASSIGNMENT",
              {},
              { { "name", "LABEL", FieldKind::String }, { "description", "TEXT", FieldKind::String }, assignedItems },
              true },
        }
    );
    catalog.referenceSelects.push_back({ "DERIVED_PROPERTY_SELECT", derivedProperty });
    catalog.referenceSelects.push_back({ "LAYERED_ITEM", layeredItem });
}
} // namespace step_semantics::catalog_build::ap214_mbx
