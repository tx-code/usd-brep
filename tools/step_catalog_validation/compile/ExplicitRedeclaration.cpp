// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalogBuilder.h"

using namespace step_semantics;
using namespace step_semantics::catalog_build;
#ifndef STEP_EXPLICIT_CASE
#    define STEP_EXPLICIT_CASE 0
#endif

constexpr Recipe Description()
{
    Recipe recipe;
    recipe.declarations = {
        { "BASE", {}, { { "the_value", "NUMBER", FieldKind::Number } } },
        { "NARROW", { "BASE" }, {}, true },
        { "OTHER", {}, {} },
    };
    recipe.explicitRedeclarations.push_back({ "NARROW", "BASE", "the_value", "REAL", FieldKind::Real });
    if constexpr (STEP_EXPLICIT_CASE == 1)
    {
        recipe.explicitRedeclarations[0].recordType = "MISSING";
    }
    if constexpr (STEP_EXPLICIT_CASE == 2)
    {
        recipe.explicitRedeclarations[0].declaredIn = "OTHER";
    }
    if constexpr (STEP_EXPLICIT_CASE == 3)
    {
        recipe.explicitRedeclarations[0].field = "missing";
    }
    if constexpr (STEP_EXPLICIT_CASE == 4)
    {
        recipe.explicitRedeclarations[0].type = "INTEGER";
        recipe.explicitRedeclarations[0].kind = FieldKind::Integer;
    }
    if constexpr (STEP_EXPLICIT_CASE == 5)
    {
        recipe.explicitRedeclarations.push_back(recipe.explicitRedeclarations[0]);
    }
    if constexpr (STEP_EXPLICIT_CASE == 6)
    {
        recipe.derivedSlots.push_back({ "NARROW", "BASE", "the_value" });
    }
    if constexpr (STEP_EXPLICIT_CASE == 7)
    {
        recipe.declarations.push_back({ "SIBLING", { "BASE" }, {} });
        recipe.declarations.push_back({ "BOTH", { "NARROW", "SIBLING" }, {}, true });
        recipe.explicitRedeclarations.push_back({ "SIBLING", "BASE", "the_value", "REAL", FieldKind::Real });
    }
    if constexpr (STEP_EXPLICIT_CASE == 8)
    {
        recipe.declarations[0].fields[0].kind = FieldKind::Real;
    }
    if constexpr (STEP_EXPLICIT_CASE == 9)
    {
        recipe.explicitRedeclarations[0].recordType = "BASE";
    }
    return recipe;
}
using Compiled = CompiledCatalog<Description>;
[[maybe_unused]] constexpr auto checked = &Compiled::catalog;
#if STEP_EXPLICIT_CASE == 0
static_assert(Compiled::store.explicitRedeclarations.size() == 1);
static_assert(Compiled::fields[0].type == "NUMBER" && Compiled::fields[0].kind == FieldKind::Number);
static_assert(Compiled::explicitFields[0].type == "REAL" && Compiled::explicitFields[0].kind == FieldKind::Real);
static_assert(Compiled::store.flattened[1].count == 1 && Compiled::store.compiledDerived.empty());
#endif
