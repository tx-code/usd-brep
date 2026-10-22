// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::products
{
inline constexpr std::string_view characterized[] = { "PRODUCT_DEFINITION", "PRODUCT_DEFINITION_SHAPE", "PRODUCT_DEFINITION_RELATIONSHIP" };
inline constexpr std::string_view transformations[] = { "ITEM_DEFINED_TRANSFORMATION" };
inline constexpr std::string_view legacyClassified[] = { "PRODUCT_DEFINITION_FORMATION", "ASSEMBLY_COMPONENT_USAGE" };
inline constexpr std::string_view transformedShape[] = { "REPRESENTATION_RELATIONSHIP",
                                                         "REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION",
                                                         "SHAPE_REPRESENTATION_RELATIONSHIP" };
constexpr void AddDeclarations(Recipe& recipe, ProfileKind profile)
{
    const bool optionalDescription = profile != ProfileKind::Ap203N916Source;
    // Fixed AP214E3_2010.exp (71ab140f...) and modified N916 (020b4d25...),
    // entity-local transport in declaration
    // order. These declarations do not establish an assembly placement, part
    // ownership, physical unit interpretation or native construction capability.
    recipe.declarations.insert(
        recipe.declarations.end(),
        { { "PRODUCT_DEFINITION_RELATIONSHIP",
            {},
            { { "id", "IDENTIFIER", FieldKind::String },
              { "name", "LABEL", FieldKind::String },
              { "description", "TEXT", FieldKind::String, optionalDescription },
              { "relating_product_definition", "PRODUCT_DEFINITION", FieldKind::Reference },
              { "related_product_definition", "PRODUCT_DEFINITION", FieldKind::Reference } },
            true },
          { "PRODUCT_DEFINITION_USAGE", { "PRODUCT_DEFINITION_RELATIONSHIP" }, {}, true },
          { "ASSEMBLY_COMPONENT_USAGE", { "PRODUCT_DEFINITION_USAGE" }, { { "reference_designator", "IDENTIFIER", FieldKind::String, true } }, true },
          { "NEXT_ASSEMBLY_USAGE_OCCURRENCE", { "ASSEMBLY_COMPONENT_USAGE" }, {}, true },
          { "CONTEXT_DEPENDENT_SHAPE_REPRESENTATION",
            {},
            { { "representation_relation", "SHAPE_REPRESENTATION_RELATIONSHIP", FieldKind::Reference },
              { "represented_product_relation", "PRODUCT_DEFINITION_SHAPE", FieldKind::Reference } },
            true },
          { "ITEM_DEFINED_TRANSFORMATION",
            {},
            { { "name", "LABEL", FieldKind::String },
              { "description", "TEXT", FieldKind::String, optionalDescription },
              { "transform_item_1", "REPRESENTATION_ITEM", FieldKind::Reference },
              { "transform_item_2", "REPRESENTATION_ITEM", FieldKind::Reference } },
            true },
          { "REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION",
            { "REPRESENTATION_RELATIONSHIP" },
            { { "transformation_operator", "TRANSFORMATION", FieldKind::Reference } },
            true } }
    );
    for (auto& select : recipe.referenceSelects)
    {
        if (select.name == "CHARACTERIZED_DEFINITION")
        {
            select.branches = characterized;
        }
        if (profile == ProfileKind::Ap203N916Source && select.name == "CLASSIFIED_ITEM")
        {
            // Modified N916 CLASSIFIED_ITEM admits assembly usage through its
            // actual subtype, not an arbitrary PDR or a product label match.
            select.branches = legacyClassified;
        }
    }
    recipe.referenceSelects.push_back({ "TRANSFORMATION", transformations });
    recipe.complexSignatures.push_back(transformedShape);
}
} // namespace step_semantics::catalog_build::products
