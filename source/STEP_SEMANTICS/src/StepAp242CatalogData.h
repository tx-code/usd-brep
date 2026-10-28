// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::ap242
{
// MBx-IF AP242 Ed.2 mim_lf.exp v1.101 (ff51beb1...), entity-local
// declarations and admitted SELECT branches. External-reference, mapped-item,
// camera and PMI branches are deliberately absent, not silently ignored.
inline constexpr std::string_view Representations[] = { "REPRESENTATION" };
inline constexpr std::string_view Products[] = { "PRODUCT_DEFINITION" };
inline constexpr std::string_view StyledTargets[] = { "GEOMETRIC_REPRESENTATION_ITEM", "TOPOLOGICAL_REPRESENTATION_ITEM", "REPRESENTATION" };
inline constexpr std::string_view PresentationItems[] = { "AXIS2_PLACEMENT_3D", "STYLED_ITEM" };
inline constexpr std::string_view ConstructionOrShape[] = { "CONSTRUCTIVE_GEOMETRY_REPRESENTATION", "SHAPE_REPRESENTATION" };
constexpr Recipe Apply(Recipe recipe)
{
    // The independently checked 166-entity common transport starts from the
    // shared recipe. This is a distinct catalog with the AP242 differences
    // below; it cannot select the AP214 source checker or inherit exceptions.
    for (auto& declaration : recipe.declarations)
    {
        for (auto& field : declaration.fields)
        {
            if (declaration.name == "REPRESENTATION_RELATIONSHIP" && (field.name == "rep_1" || field.name == "rep_2"))
            {
                field.type = "REPRESENTATION_OR_REPRESENTATION_REFERENCE";
            }
            if (declaration.name == "PRODUCT_DEFINITION_RELATIONSHIP" &&
                (field.name == "relating_product_definition" || field.name == "related_product_definition"))
            {
                field.type = "PRODUCT_DEFINITION_OR_REFERENCE";
            }
            if (declaration.name == "STYLED_ITEM")
            {
                if (field.name == "styles")
                {
                    field.minimum = 0;
                }
                if (field.name == "item")
                {
                    field.type = "STYLED_ITEM_TARGET";
                }
            }
            if ((declaration.name == "CURVE_STYLE" || declaration.name == "POINT_STYLE") && field.name != "name")
            {
                field.optional = true;
            }
        }
    }
    // AP242 CBU.dimensions is explicit and WR1 checks the factor's dimensions.
    // Preserve SI_UNIT's independent DERIVE; never make its '*' explicit too.
    recipe.derivedSlots.erase(
        std::remove_if(
            recipe.derivedSlots.begin(),
            recipe.derivedSlots.end(),
            [](const auto& slot)
            {
                return slot.recordType == "CONVERSION_BASED_UNIT" && slot.declaredIn == "NAMED_UNIT" && slot.field == "dimensions";
            }
        ),
        recipe.derivedSlots.end()
    );
    recipe.referenceSelects.insert(
        recipe.referenceSelects.end(),
        { { "REPRESENTATION_OR_REPRESENTATION_REFERENCE", Representations },
          { "PRODUCT_DEFINITION_OR_REFERENCE", Products },
          { "STYLED_ITEM_TARGET", StyledTargets },
          { "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION_ITEMS", PresentationItems },
          { "CONSTRUCTIVE_GEOMETRY_REPRESENTATION_OR_SHAPE_REPRESENTATION", ConstructionOrShape } }
    );
    // AP242 explicit redeclarations occupy their inherited source slots; they
    // narrow effective types without adding parameters or demanding DERIVE '*'.
    recipe.explicitRedeclarations.insert(
        recipe.explicitRedeclarations.end(),
        { { "SHAPE_DEFINITION_REPRESENTATION", "PROPERTY_DEFINITION_REPRESENTATION", "definition", "PROPERTY_DEFINITION", FieldKind::Reference },
          { "SHAPE_DEFINITION_REPRESENTATION",
            "PROPERTY_DEFINITION_REPRESENTATION",
            "used_representation",
            "SHAPE_REPRESENTATION",
            FieldKind::Reference },
          { "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION",
            "REPRESENTATION",
            "items",
            "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION_ITEMS",
            FieldKind::Aggregate },
          { "CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP",
            "REPRESENTATION_RELATIONSHIP",
            "rep_1",
            "CONSTRUCTIVE_GEOMETRY_REPRESENTATION_OR_SHAPE_REPRESENTATION",
            FieldKind::Reference },
          { "CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP",
            "REPRESENTATION_RELATIONSHIP",
            "rep_2",
            "CONSTRUCTIVE_GEOMETRY_REPRESENTATION",
            FieldKind::Reference } }
    );
    return recipe;
}
} // namespace step_semantics::catalog_build::ap242
