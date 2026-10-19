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

constexpr void AddBorrowedRealItemTransport(Recipe& catalog)
{
    // N2635 (c68dd022...), not AP214E3_2010: the foreign RI/literal
    // intersection preserves all ten ancestors and the same two source slots.
    // Mathematical parents are diagnostic guards, not admitted expressions.
    // NUMERIC_EXPRESSION's computed LOGICAL attributes have no P21 slots.
    catalog.declarations.insert(
        catalog.declarations.end(),
        {
            { "GENERIC_EXPRESSION", {}, {} },
            { "EXPRESSION", { "GENERIC_EXPRESSION" }, {} },
            { "NUMERIC_EXPRESSION", { "EXPRESSION" }, {} },
            { "SIMPLE_GENERIC_EXPRESSION", { "GENERIC_EXPRESSION" }, {} },
            { "GENERIC_LITERAL", { "SIMPLE_GENERIC_EXPRESSION" }, {} },
            { "SIMPLE_NUMERIC_EXPRESSION", { "NUMERIC_EXPRESSION", "SIMPLE_GENERIC_EXPRESSION" }, {} },
            { "LITERAL_NUMBER", { "SIMPLE_NUMERIC_EXPRESSION", "GENERIC_LITERAL" }, { { "the_value", "NUMBER", FieldKind::Number } } },
            { "REAL_LITERAL", { "LITERAL_NUMBER" }, {} },
            { "REAL_REPRESENTATION_ITEM", { "REPRESENTATION_ITEM", "REAL_LITERAL" }, {}, true },
        }
    );
    catalog.explicitRedeclarations.push_back({ "REAL_LITERAL", "LITERAL_NUMBER", "the_value", "REAL", FieldKind::Real });
}
} // namespace step_semantics::catalog_build::ap214_mbx
