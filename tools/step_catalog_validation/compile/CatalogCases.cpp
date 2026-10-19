// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalogBuilder.h"

namespace cases
{
using namespace step_semantics;
using namespace step_semantics::catalog_build;
using RawField = catalog_build::Field;
inline constexpr std::string_view A[] = { "A" }, Missing[] = { "ABSENT" }, Twice[] = { "A", "A" };
inline constexpr std::string_view AB[] = { "A", "B" }, BA[] = { "B", "A" }, B[] = { "B" };
inline constexpr std::string_view Values[] = { "X", "Y" }, DuplicateValues[] = { "X", "X" };
inline constexpr std::span<const std::string_view> MissingGroup[] = { Missing }, DuplicateGroup[] = { Twice };
inline constexpr SiPrefix BadPrefix[] = { { "MILLI", -3 } }, DuplicatePrefix[] = { { "", 0 }, { "", 0 } };
inline constexpr auto CapacityNames = []() consteval
{
    std::array<std::array<char, 4>, 257> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        out[i] = { 'T', static_cast<char>('0' + i / 100), static_cast<char>('0' + (i / 10) % 10), static_cast<char>('0' + i % 10) };
    }
    return out;
}();
constexpr Recipe Diamond()
{
    Recipe recipe;
    recipe.declarations = { { "A", {}, { { "x", "REAL", FieldKind::Real } } },
                            { "B", { "A" }, { { "b", "REAL", FieldKind::Real } } },
                            { "C", { "A" }, { { "c", "REAL", FieldKind::Real } } },
                            { "D", { "B", "C" }, { { "d", "REAL", FieldKind::Real } }, true } };
    recipe.derivedSlots.push_back({ "D", "A", "x" });
    RawField grid{ "grid", "REAL", FieldKind::Aggregate };
    grid.elementKind = FieldKind::Real;
    grid.minimum = 2;
    grid.nestedAggregates.push_back({ "LIST", 2, {}, false });
    recipe.declarations[3].fields.push_back(grid);
    return recipe;
}
constexpr Recipe MakeCase()
{
    auto recipe = Diamond();
#if STEP_CATALOG_CASE == 1
    recipe.declarations.push_back(recipe.declarations.front());
#elif STEP_CATALOG_CASE == 2
    recipe.declarations[1].parents = { "ABSENT" };
#elif STEP_CATALOG_CASE == 3
    recipe.declarations[0].parents = { "D" };
#elif STEP_CATALOG_CASE == 4 || STEP_CATALOG_CASE == 33
    recipe = Recipe{};
    constexpr std::size_t count = STEP_CATALOG_CASE == 4 ? 257 : 256;
    for (std::size_t i = 0; i < count; ++i)
    {
        recipe.declarations.push_back({ std::string_view(CapacityNames[i].data(), 4), {}, {} });
    }
#elif STEP_CATALOG_CASE == 5
    recipe.declarations[0].fields[0] = { "x", "ABSENT", FieldKind::Reference };
#elif STEP_CATALOG_CASE == 6
    recipe.referenceSelects.push_back({ "S", Missing });
#elif STEP_CATALOG_CASE == 7
    recipe.declarations[0].fields[0] = { "x", "ABSENT", FieldKind::Enumeration };
#elif STEP_CATALOG_CASE == 8
    recipe.complexSignatures.push_back(BA);
#elif STEP_CATALOG_CASE == 9
    recipe.complexSignatures.push_back(Twice);
#elif STEP_CATALOG_CASE == 10
    recipe.complexSignatures.push_back(B);
#elif STEP_CATALOG_CASE == 11
    recipe.complexSignatures = { A, A };
#elif STEP_CATALOG_CASE == 12
    recipe.derivedSlots = { { "ABSENT", "A", "x" } };
#elif STEP_CATALOG_CASE == 13
    recipe.derivedSlots = { { "D", "ABSENT", "x" } };
#elif STEP_CATALOG_CASE == 14
    recipe.declarations.push_back({ "E", {}, { { "x", "REAL", FieldKind::Real } } });
    recipe.derivedSlots = { { "D", "E", "x" } };
#elif STEP_CATALOG_CASE == 15
    recipe.derivedSlots = { { "D", "A", "absent" } };
#elif STEP_CATALOG_CASE == 16
    recipe.declarations[0].fields.push_back(recipe.declarations[0].fields.front());
#elif STEP_CATALOG_CASE == 17
    recipe.referenceSelects = { { "S", A }, { "S", A } };
#elif STEP_CATALOG_CASE == 18
    recipe.enumerations = { { "E", Values }, { "E", Values } };
#elif STEP_CATALOG_CASE == 19
    recipe.referenceSelects.push_back({ "S", {} });
#elif STEP_CATALOG_CASE == 20
    recipe.referenceSelects.push_back({ "S", Twice });
#elif STEP_CATALOG_CASE == 21
    recipe.enumerations.push_back({ "E", {} });
#elif STEP_CATALOG_CASE == 22
    recipe.enumerations.push_back({ "E", DuplicateValues });
#elif STEP_CATALOG_CASE == 23
    recipe.declarations[3].fields.back().maximum = 1;
#elif STEP_CATALOG_CASE == 24
    recipe.declarations[3].fields.back().nestedAggregates[0].maximum = 1;
#elif STEP_CATALOG_CASE == 25
    RequireTarget(recipe, recipe.declarations, "ABSENT");
#elif STEP_CATALOG_CASE == 26
    recipe.prefixes = BadPrefix;
#elif STEP_CATALOG_CASE == 27
    recipe.prefixes = DuplicatePrefix;
#elif STEP_CATALOG_CASE == 28
    recipe.exclusiveGroups = MissingGroup;
#elif STEP_CATALOG_CASE == 29
    recipe.exclusiveGroups = DuplicateGroup;
#elif STEP_CATALOG_CASE == 30
    return Recipe{};
#elif STEP_CATALOG_CASE == 31
    recipe.complexSignatures.push_back(Missing);
#elif STEP_CATALOG_CASE == 32
    recipe.complexSignatures.push_back({});
#elif STEP_CATALOG_CASE == 34
    recipe.declarations[0].fields[0] = { "x", "ABSENT", FieldKind::TrimmingSelect };
#elif STEP_CATALOG_CASE >= 35 && STEP_CATALOG_CASE <= 37
    recipe.declarations[0].fields[0] = { "x", "MARKER_SELECT", FieldKind::MarkerSelect };
    if constexpr (STEP_CATALOG_CASE != 36) recipe.referenceSelects.push_back({ "MARKER_SELECT", B });
    if constexpr (STEP_CATALOG_CASE != 37) recipe.enumerations.push_back({ "MARKER_TYPE", Values });
#endif
    return recipe;
}
using Result = CompiledCatalog<MakeCase>;
static_assert(Result::catalog.names.size() != 0);
#if STEP_CATALOG_CASE == 0
static_assert(Result::store.flattened[3].count == 5);
static_assert(Result::store.bindings[Result::store.flattened[3].offset] == FieldBinding{ 0, 0 });
static_assert(Result::store.bindings[Result::store.flattened[3].offset + 1] == FieldBinding{ 1, 0 });
static_assert(Result::store.bindings[Result::store.flattened[3].offset + 2] == FieldBinding{ 2, 0 });
static_assert(Result::store.compiledDerived[0].origin == 0);
static_assert(Result::catalog.declarations[3].fields[1].nestedAggregates[0].minimum == 2);
static_assert(
    []() consteval
    {
        TypeMembership bits;
        for (const auto id : { 63, 64, 127, 128, 255 })
        {
            bits.set(id);
        }
        TypeMembership same = bits;
        return bits.test(255) && bits.SharedCount(same) == 5 && !bits.test(254);
    }()
);
#elif STEP_CATALOG_CASE == 33
static_assert(Result::catalog.names.size() == 256 && Result::catalog.ancestors[255].test(255));
#endif
} // namespace cases
