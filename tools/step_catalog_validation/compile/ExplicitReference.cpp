// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalogBuilder.h"
using namespace step_semantics;
using namespace step_semantics::catalog_build;
#ifndef STEP_REFERENCE_CASE
#    define STEP_REFERENCE_CASE 0
#endif
constexpr std::string_view Narrow[] = { "CHILD" };
constexpr std::string_view Wide[] = { "ROOT", "OTHER" };
constexpr Recipe Description()
{
    Recipe recipe;
    catalog_build::Field items{ "items", "ROOT", FieldKind::Aggregate };
    items.minimum = 1;
    items.unique = Unique::Set;
    recipe.declarations = { { "ROOT", {}, {} },
                            { "CHILD", { "ROOT" }, {} },
                            { "GRANDCHILD", { "CHILD" }, {} },
                            { "OTHER", {}, {} },
                            { "BASE", {}, { { "target", "ROOT", FieldKind::Reference }, items } },
                            { "NARROW", { "BASE" }, {}, true } };
    recipe.referenceSelects = { { "NARROW_SELECT", Narrow }, { "WIDE_SELECT", Wide } };
    recipe.explicitRedeclarations = { { "NARROW", "BASE", "target", "CHILD", FieldKind::Reference },
                                      { "NARROW", "BASE", "items", "NARROW_SELECT", FieldKind::Aggregate } };
    if constexpr (STEP_REFERENCE_CASE == 1)
    {
        recipe.explicitRedeclarations[0].type = "MISSING";
    }
    if constexpr (STEP_REFERENCE_CASE == 2)
    {
        recipe.explicitRedeclarations[0].type = "WIDE_SELECT";
    }
    if constexpr (STEP_REFERENCE_CASE == 3)
    {
        recipe.explicitRedeclarations[0].kind = FieldKind::Real;
    }
    if constexpr (STEP_REFERENCE_CASE == 4)
    {
        recipe.explicitRedeclarations[0].kind = FieldKind::Aggregate;
    }
    if constexpr (STEP_REFERENCE_CASE == 5)
    {
        // Checking only a branch-name match would miss the admitted ROOT and
        // its wider descendants when the original domain is just CHILD.
        recipe.declarations[4].fields[0].type = "CHILD";
        recipe.explicitRedeclarations[0].type = "ROOT";
    }
    if constexpr (STEP_REFERENCE_CASE == 6)
    {
        recipe.explicitRedeclarations[1].type = "WIDE_SELECT";
    }
    return recipe;
}
using Compiled = CompiledCatalog<Description>;
[[maybe_unused]] constexpr auto checked = &Compiled::catalog;
#if STEP_REFERENCE_CASE == 0
static_assert(Compiled::fields[0].type == "ROOT");
static_assert(Compiled::explicitFields[0].type == "CHILD");
static_assert(Compiled::explicitFields[1].minimum == 1 && Compiled::explicitFields[1].unique == Unique::Set);
static_assert(Compiled::explicitFields[1].elementKind == FieldKind::Reference);
#endif
