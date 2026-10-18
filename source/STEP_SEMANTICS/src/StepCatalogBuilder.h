// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepCatalog.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace step_semantics::catalog_build
{
enum class Error
{
    None,
    InvalidProfile,
    OverlayTarget,
    EmptyCatalog,
    Capacity,
    DuplicateDeclaration,
    MissingParent,
    Cycle,
    DuplicateField,
    DuplicateRegistry,
    EmptyBranches,
    DuplicateBranch,
    MissingReference,
    MissingEnumeration,
    AggregateBounds,
    ComplexOrder,
    ComplexClosure,
    DuplicateComplex,
    DerivedOwner,
    DerivedOrigin,
    DerivedField,
    PrefixSentinel
};

// Scratch descriptions may allocate only inside constant evaluation. Text and
// NamedBranches refer to static literals/arrays; no owning string can escape.
struct Field
{
    std::string_view name, type;
    FieldKind kind;
    bool optional = false;
    FieldKind elementKind = FieldKind::Reference;
    std::size_t minimum = 0, maximum = std::numeric_limits<std::size_t>::max();
    Unique unique = Unique::None;
    std::vector<AggregateDimension> nestedAggregates;
};
struct Declaration
{
    std::string_view name;
    std::vector<std::string_view> parents;
    std::vector<Field> fields;
    bool simple = false;
};
struct Recipe
{
    std::vector<Declaration> declarations;
    std::vector<DerivedSlot> derivedSlots;
    std::vector<NamedBranches> referenceSelects, enumerations;
    std::vector<std::span<const std::string_view>> complexSignatures;
    std::vector<std::string_view> measureBranches;
    std::span<const std::span<const std::string_view>> exclusiveGroups;
    std::span<const SiPrefix> prefixes;
    Error issue = Error::None;
};
template <class Range>
constexpr bool RequireTarget(Recipe& recipe, const Range& entries, std::string_view name)
{
    const auto count = std::count_if(
        entries.begin(),
        entries.end(),
        [&](const auto& entry)
        {
            return entry.name == name;
        }
    );
    if (count == 1)
    {
        return true;
    }
    recipe.issue = Error::OverlayTarget;
    return false;
}
constexpr void SetSimple(Recipe& recipe, std::string_view name)
{
    if (!RequireTarget(recipe, recipe.declarations, name))
    {
        return;
    }
    for (auto& declaration : recipe.declarations)
    {
        if (declaration.name == name)
        {
            declaration.simple = true;
        }
    }
}
struct Counts
{
    std::size_t types = 0, fields = 0, parents = 0, dimensions = 0, bindings = 0;
    std::size_t selects = 0, selectBranches = 0, enums = 0, enumValues = 0;
    std::size_t complexes = 0, facets = 0, derived = 0, measures = 0;
    std::size_t groups = 0, groupBranches = 0, prefixes = 0;
    constexpr bool operator==(const Counts&) const = default;
};
struct Analysis
{
    Counts counts;
    Error code = Error::None;
    std::size_t subject = 0, detail = 0;
};
struct Layout
{
    Analysis analysis;
    std::vector<NameId> index;
    std::vector<TypeMembership> ancestors;
    std::vector<std::vector<FieldBinding>> flattened;
};
constexpr void Fail(Analysis& report, Error code, std::size_t subject = 0, std::size_t detail = 0)
{
    if (report.code == Error::None)
    {
        report.code = code;
        report.subject = subject;
        report.detail = detail;
    }
}
template <class Range>
constexpr bool HasName(const Range& entries, std::string_view name)
{
    return std::any_of(
        entries.begin(),
        entries.end(),
        [&](const auto& entry)
        {
            return entry.name == name;
        }
    );
}
constexpr bool Contains(std::span<const std::string_view> names, std::string_view name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}
constexpr bool ValidDimension(const AggregateDimension& dimension)
{
    return (dimension.aggregate == "LIST" || dimension.aggregate == "SET") && (!dimension.maximum || dimension.minimum <= *dimension.maximum);
}
// Check closed program metadata, not user STEP values or general EXPRESS rules.
// Resolve names and reject cycles before following indexes. The same layout
// computation supplies both exact counts and emission, preserving source order.
constexpr Layout ComputeLayout(const Recipe& recipe)
{
    Layout layout;
    auto& report = layout.analysis;
    auto& counts = report.counts;
    counts.types = recipe.declarations.size();
    if (recipe.issue != Error::None)
    {
        Fail(report, recipe.issue);
        return layout;
    }
    if (!counts.types)
    {
        Fail(report, Error::EmptyCatalog);
        return layout;
    }
    if (counts.types > CatalogTypeCapacity)
    {
        Fail(report, Error::Capacity);
        return layout;
    }
    for (std::size_t id = 0; id < counts.types; ++id)
    {
        layout.index.push_back({ recipe.declarations[id].name, id });
    }
    std::sort(
        layout.index.begin(),
        layout.index.end(),
        [](const NameId& a, const NameId& b)
        {
            return a.name < b.name;
        }
    );
    for (std::size_t i = 0; i < counts.types; ++i)
    {
        if (layout.index[i].name.empty() || (i && layout.index[i - 1].name == layout.index[i].name))
        {
            Fail(report, Error::DuplicateDeclaration, i);
            return layout;
        }
    }
    const auto typeId = [&](std::string_view name)
    {
        return LookupCatalogName(layout.index, name);
    };
    for (std::size_t id = 0; id < counts.types; ++id)
    {
        const auto& declaration = recipe.declarations[id];
        counts.fields += declaration.fields.size();
        counts.parents += declaration.parents.size();
        for (const auto parent : declaration.parents)
        {
            if (!typeId(parent))
            {
                Fail(report, Error::MissingParent, id);
                return layout;
            }
        }
        for (std::size_t f = 0; f < declaration.fields.size(); ++f)
        {
            const auto& field = declaration.fields[f];
            for (std::size_t prior = 0; prior < f; ++prior)
            {
                if (declaration.fields[prior].name == field.name)
                {
                    Fail(report, Error::DuplicateField, id, f);
                    return layout;
                }
            }
            counts.dimensions += field.nestedAggregates.size();
            if ((field.kind == FieldKind::Aggregate && field.minimum > field.maximum) ||
                (!field.nestedAggregates.empty() && field.kind != FieldKind::Aggregate) ||
                !std::all_of(field.nestedAggregates.begin(), field.nestedAggregates.end(), ValidDimension))
            {
                Fail(report, Error::AggregateBounds, id, f);
                return layout;
            }
        }
    }
    layout.ancestors.resize(counts.types);
    layout.flattened.resize(counts.types);
    std::array<unsigned char, CatalogTypeCapacity> state{};
    const auto visit = [&](auto&& self, std::size_t id) -> bool
    {
        if (state[id] == 2)
        {
            return true;
        }
        if (state[id] == 1)
        {
            Fail(report, Error::Cycle, id);
            return false;
        }
        state[id] = 1;
        layout.ancestors[id].set(id);
        // Parent order and qualified origin/index determine diamond deduplication.
        // Never deduplicate just by an unqualified field name.
        for (const auto name : recipe.declarations[id].parents)
        {
            const auto parent = *typeId(name);
            if (!self(self, parent))
            {
                return false;
            }
            layout.ancestors[id] |= layout.ancestors[parent];
            for (const auto binding : layout.flattened[parent])
            {
                if (std::find(layout.flattened[id].begin(), layout.flattened[id].end(), binding) == layout.flattened[id].end())
                {
                    layout.flattened[id].push_back(binding);
                }
            }
        }
        for (std::size_t f = 0; f < recipe.declarations[id].fields.size(); ++f)
        {
            layout.flattened[id].push_back({ id, f });
        }
        state[id] = 2;
        return true;
    };
    for (std::size_t id = 0; id < counts.types; ++id)
    {
        if (!visit(visit, id))
        {
            return layout;
        }
    }
    for (const auto& fields : layout.flattened)
    {
        counts.bindings += fields.size();
    }
    const auto validateBranches = [&](const auto& entries, bool references) -> bool
    {
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].name.empty())
            {
                Fail(report, Error::DuplicateRegistry, i);
                return false;
            }
            for (std::size_t j = 0; j < i; ++j)
            {
                if (entries[j].name == entries[i].name)
                {
                    Fail(report, Error::DuplicateRegistry, i);
                    return false;
                }
            }
            const auto branches = entries[i].branches;
            if (branches.empty())
            {
                Fail(report, Error::EmptyBranches, i);
                return false;
            }
            for (std::size_t b = 0; b < branches.size(); ++b)
            {
                if (branches[b].empty() || std::find(branches.begin(), branches.begin() + b, branches[b]) != branches.begin() + b)
                {
                    Fail(report, Error::DuplicateBranch, i, b);
                    return false;
                }
                if (references && !typeId(branches[b]))
                {
                    Fail(report, Error::MissingReference, i, b);
                    return false;
                }
            }
        }
        return true;
    };
    if (!validateBranches(recipe.referenceSelects, true) || !validateBranches(recipe.enumerations, false))
    {
        return layout;
    }
    counts.selects = recipe.referenceSelects.size();
    counts.enums = recipe.enumerations.size();
    for (const auto& entry : recipe.referenceSelects)
    {
        counts.selectBranches += entry.branches.size();
    }
    for (const auto& entry : recipe.enumerations)
    {
        counts.enumValues += entry.branches.size();
    }
    for (std::size_t id = 0; id < counts.types; ++id)
    {
        for (std::size_t f = 0; f < recipe.declarations[id].fields.size(); ++f)
        {
            const auto& field = recipe.declarations[id].fields[f];
            const auto kind = field.kind == FieldKind::Aggregate ? field.elementKind : field.kind;
            if ((kind == FieldKind::Reference || kind == FieldKind::TrimmingSelect) && !typeId(field.type) &&
                !HasName(recipe.referenceSelects, field.type))
            {
                Fail(report, Error::MissingReference, id, f);
                return layout;
            }
            if (kind == FieldKind::Enumeration && !HasName(recipe.enumerations, field.type) &&
                !(field.type == "SI_PREFIX" && !recipe.prefixes.empty()))
            {
                Fail(report, Error::MissingEnumeration, id, f);
                return layout;
            }
        }
    }
    counts.complexes = recipe.complexSignatures.size();
    for (std::size_t i = 0; i < counts.complexes; ++i)
    {
        const auto signature = recipe.complexSignatures[i];
        counts.facets += signature.size();
        if (signature.empty() || !std::is_sorted(signature.begin(), signature.end()) ||
            std::adjacent_find(signature.begin(), signature.end()) != signature.end())
        {
            Fail(report, Error::ComplexOrder, i);
            return layout;
        }
        for (const auto facet : signature)
        {
            const auto id = typeId(facet);
            if (!id)
            {
                Fail(report, Error::MissingReference, i);
                return layout;
            }
            for (std::size_t parent = 0; parent < counts.types; ++parent)
            {
                if (layout.ancestors[*id].test(parent) && !Contains(signature, recipe.declarations[parent].name))
                {
                    Fail(report, Error::ComplexClosure, i, parent);
                    return layout;
                }
            }
        }
        for (std::size_t prior = 0; prior < i; ++prior)
        {
            if (std::equal(signature.begin(), signature.end(), recipe.complexSignatures[prior].begin(), recipe.complexSignatures[prior].end()))
            {
                Fail(report, Error::DuplicateComplex, i);
                return layout;
            }
        }
    }
    counts.derived = recipe.derivedSlots.size();
    for (std::size_t i = 0; i < counts.derived; ++i)
    {
        const auto& slot = recipe.derivedSlots[i];
        const auto owner = typeId(slot.recordType), origin = typeId(slot.declaredIn);
        if (!owner)
        {
            Fail(report, Error::DerivedOwner, i);
            return layout;
        }
        if (!origin || !layout.ancestors[*owner].test(*origin))
        {
            Fail(report, Error::DerivedOrigin, i);
            return layout;
        }
        if (!HasName(recipe.declarations[*origin].fields, slot.field))
        {
            Fail(report, Error::DerivedField, i);
            return layout;
        }
    }
    counts.groups = recipe.exclusiveGroups.size();
    for (std::size_t g = 0; g < counts.groups; ++g)
    {
        const auto group = recipe.exclusiveGroups[g];
        counts.groupBranches += group.size();
        if (group.empty())
        {
            Fail(report, Error::EmptyBranches, g);
            return layout;
        }
        for (std::size_t b = 0; b < group.size(); ++b)
        {
            if (!typeId(group[b]))
            {
                Fail(report, Error::MissingReference, g, b);
                return layout;
            }
            if (std::find(group.begin(), group.begin() + b, group[b]) != group.begin() + b)
            {
                Fail(report, Error::DuplicateBranch, g, b);
                return layout;
            }
        }
    }
    counts.prefixes = recipe.prefixes.size();
    if (counts.prefixes && (recipe.prefixes.front().name != "" || recipe.prefixes.front().power != 0))
    {
        Fail(report, Error::PrefixSentinel);
        return layout;
    }
    for (std::size_t i = 0; i < counts.prefixes; ++i)
    {
        for (std::size_t prior = 0; prior < i; ++prior)
        {
            if (recipe.prefixes[i].name == recipe.prefixes[prior].name)
            {
                Fail(report, Error::PrefixSentinel, i);
                return layout;
            }
        }
    }
    counts.measures = recipe.measureBranches.size();
    return layout;
}
constexpr Analysis Analyze(const Recipe& recipe)
{
    return ComputeLayout(recipe).analysis;
}

// Only enum/integer facts become template arguments. These categories are the
// stable compiler-error contract; ordinary function parameters cannot directly
// drive static_assert, even when the function itself is consteval.
template <Analysis Report>
consteval bool RequireValid()
{
#define STEP_CATALOG_REJECT(Name)                                                                                                                    \
    if constexpr (Report.code == Error::Name)                                                                                                        \
    static_assert(Report.code == Error::None, "STEP_CATALOG_INVALID_" #Name)
    STEP_CATALOG_REJECT(InvalidProfile);
    STEP_CATALOG_REJECT(OverlayTarget);
    STEP_CATALOG_REJECT(EmptyCatalog);
    STEP_CATALOG_REJECT(Capacity);
    STEP_CATALOG_REJECT(DuplicateDeclaration);
    STEP_CATALOG_REJECT(MissingParent);
    STEP_CATALOG_REJECT(Cycle);
    STEP_CATALOG_REJECT(DuplicateField);
    STEP_CATALOG_REJECT(DuplicateRegistry);
    STEP_CATALOG_REJECT(EmptyBranches);
    STEP_CATALOG_REJECT(DuplicateBranch);
    STEP_CATALOG_REJECT(MissingReference);
    STEP_CATALOG_REJECT(MissingEnumeration);
    STEP_CATALOG_REJECT(AggregateBounds);
    STEP_CATALOG_REJECT(ComplexOrder);
    STEP_CATALOG_REJECT(ComplexClosure);
    STEP_CATALOG_REJECT(DuplicateComplex);
    STEP_CATALOG_REJECT(DerivedOwner);
    STEP_CATALOG_REJECT(DerivedOrigin);
    STEP_CATALOG_REJECT(DerivedField);
    STEP_CATALOG_REJECT(PrefixSentinel);
#undef STEP_CATALOG_REJECT
    return Report.code == Error::None;
}
struct FieldRecord
{
    std::string_view name, type;
    FieldKind kind;
    bool optional;
    FieldKind elementKind;
    std::size_t minimum, maximum;
    Unique unique;
    CatalogRange nested;
};
struct DeclarationRecord
{
    std::string_view name;
    CatalogRange parents, fields;
    bool simple;
};
struct NamedRange
{
    std::string_view name;
    CatalogRange branches;
};
template <Counts C>
struct Frozen
{
    std::array<DeclarationRecord, C.types> declarations{};
    std::array<std::string_view, C.types> names{};
    std::array<NameId, C.types> index{};
    std::array<TypeMembership, C.types> ancestors{};
    std::array<std::string_view, C.parents> parents{};
    std::array<FieldRecord, C.fields> fields{};
    std::array<AggregateDimension, C.dimensions> dimensions{};
    std::array<FieldBinding, C.bindings> bindings{};
    std::array<CatalogRange, C.types> flattened{};
    std::array<DerivedSlot, C.derived> derived{};
    std::array<CompiledDerivedSlot, C.derived> compiledDerived{};
    std::array<NamedRange, C.selects> selects{};
    std::array<TypeMembership, C.selects> selectMasks{};
    std::array<std::string_view, C.selectBranches> selectBranches{};
    std::array<NamedRange, C.enums> enums{};
    std::array<std::string_view, C.enumValues> enumValues{};
    std::array<CatalogRange, C.complexes> complexes{};
    std::array<std::string_view, C.facets> facets{};
    std::array<std::string_view, C.measures> measures{};
    std::array<CatalogRange, C.groups> groups{};
    std::array<std::string_view, C.groupBranches> groupBranches{};
    std::array<TypeMembership, C.groups> groupMasks{};
    std::array<SiPrefix, C.prefixes> prefixes{};
};
// CompiledCatalog supplies counts from a successful analysis of the same
// immutable recipe. This emitter is never a runtime fallback or a public seam.
template <Counts C>
consteval Frozen<C> Freeze(const Recipe& recipe)
{
    const auto layout = ComputeLayout(recipe);
    Frozen<C> out;
    std::size_t parent = 0, field = 0, dimension = 0, binding = 0;
    for (std::size_t id = 0; id < C.types; ++id)
    {
        const auto& declaration = recipe.declarations[id];
        out.names[id] = declaration.name;
        out.index[id] = layout.index[id];
        out.ancestors[id] = layout.ancestors[id];
        out.declarations[id] = { declaration.name, { parent, declaration.parents.size() }, { field, declaration.fields.size() }, declaration.simple };
        for (const auto name : declaration.parents)
        {
            out.parents[parent++] = name;
        }
        for (const auto& raw : declaration.fields)
        {
            out.fields[field++] = { raw.name,     raw.type,        raw.kind,
                                    raw.optional, raw.elementKind, raw.minimum,
                                    raw.maximum,  raw.unique,      { dimension, raw.nestedAggregates.size() } };
            for (const auto& nested : raw.nestedAggregates)
            {
                out.dimensions[dimension++] = nested;
            }
        }
        out.flattened[id] = { binding, layout.flattened[id].size() };
        for (const auto item : layout.flattened[id])
        {
            out.bindings[binding++] = item;
        }
    }
    for (std::size_t i = 0; i < C.derived; ++i)
    {
        const auto& slot = recipe.derivedSlots[i];
        out.derived[i] = slot;
        const auto owner = *LookupCatalogName(out.index, slot.recordType), origin = *LookupCatalogName(out.index, slot.declaredIn);
        const auto& fields = recipe.declarations[origin].fields;
        const auto found = std::find_if(
            fields.begin(),
            fields.end(),
            [&](const auto& entry)
            {
                return entry.name == slot.field;
            }
        );
        out.compiledDerived[i] = { owner, origin, static_cast<std::size_t>(found - fields.begin()) };
    }
    const auto named = [&](const auto& raw, auto& entries, auto& names)
    {
        std::size_t offset = 0;
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            entries[i] = { raw[i].name, { offset, raw[i].branches.size() } };
            for (const auto name : raw[i].branches)
            {
                names[offset++] = name;
            }
        }
    };
    named(recipe.referenceSelects, out.selects, out.selectBranches);
    named(recipe.enumerations, out.enums, out.enumValues);
    for (std::size_t i = 0; i < C.selects; ++i)
    {
        for (const auto name : recipe.referenceSelects[i].branches)
        {
            out.selectMasks[i].set(*LookupCatalogName(out.index, name));
        }
    }
    const auto spans = [&](const auto& raw, auto& ranges, auto& names)
    {
        std::size_t offset = 0;
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            ranges[i] = { offset, raw[i].size() };
            for (const auto name : raw[i])
            {
                names[offset++] = name;
            }
        }
    };
    spans(recipe.complexSignatures, out.complexes, out.facets);
    spans(recipe.exclusiveGroups, out.groups, out.groupBranches);
    for (std::size_t i = 0; i < C.groups; ++i)
    {
        for (const auto name : recipe.exclusiveGroups[i])
        {
            out.groupMasks[i].set(*LookupCatalogName(out.index, name));
        }
    }
    std::copy(recipe.measureBranches.begin(), recipe.measureBranches.end(), out.measures.begin());
    std::copy(recipe.prefixes.begin(), recipe.prefixes.end(), out.prefixes.begin());
    return out;
}
template <class T, std::size_t N>
constexpr auto Slice(const std::array<T, N>& values, CatalogRange range)
{
    return std::span<const T>(values).subspan(range.offset, range.count);
}
template <Counts C>
constexpr auto FieldViews(const Frozen<C>& store)
{
    std::array<step_semantics::Field, C.fields> out{};
    for (std::size_t i = 0; i < C.fields; ++i)
    {
        const auto& f = store.fields[i];
        out[i] = { f.name, f.type, f.kind, f.optional, f.elementKind, f.minimum, f.maximum, f.unique, Slice(store.dimensions, f.nested) };
    }
    return out;
}
template <Counts C>
constexpr auto DeclarationViews(const Frozen<C>& store, const std::array<step_semantics::Field, C.fields>& fields)
{
    std::array<step_semantics::Declaration, C.types> out{};
    for (std::size_t i = 0; i < C.types; ++i)
    {
        const auto& d = store.declarations[i];
        out[i] = { d.name, Slice(store.parents, d.parents), Slice(fields, d.fields), d.simple };
    }
    return out;
}
template <std::size_t N, std::size_t M>
constexpr auto NamedViews(const std::array<NamedRange, N>& entries, const std::array<std::string_view, M>& names)
{
    std::array<NamedBranches, N> out{};
    for (std::size_t i = 0; i < N; ++i)
    {
        out[i] = { entries[i].name, Slice(names, entries[i].branches) };
    }
    return out;
}
template <std::size_t N, std::size_t M>
constexpr auto SpanViews(const std::array<CatalogRange, N>& ranges, const std::array<std::string_view, M>& names)
{
    std::array<std::span<const std::string_view>, N> out{};
    for (std::size_t i = 0; i < N; ++i)
    {
        out[i] = Slice(names, ranges[i]);
    }
    return out;
}
template <auto Factory>
struct CompiledCatalog
{
    static constexpr auto analysis = Analyze(Factory());
    static_assert(RequireValid<analysis>());
    static constexpr auto store = []() consteval
    {
        // An invalid recipe must fail through its category, not a later bounds
        // error while emitting data. Keep the emission branch uninstantiated.
        if constexpr (analysis.code == Error::None)
        {
            return Freeze<analysis.counts>(Factory());
        }
        else
        {
            return Frozen<Counts{}>{};
        }
    }();
    // These views are formed only after their referenced arrays have permanent
    // static addresses. Frozen itself contains no self-pointers or scratch spans.
    static constexpr auto fields = FieldViews(store);
    static constexpr auto declarations = DeclarationViews(store, fields);
    static constexpr auto selects = NamedViews(store.selects, store.selectBranches);
    static constexpr auto enums = NamedViews(store.enums, store.enumValues);
    static constexpr auto complexes = SpanViews(store.complexes, store.facets);
    static constexpr auto groups = SpanViews(store.groups, store.groupBranches);
    static constexpr Catalog catalog{ declarations,    store.names,   store.index,           store.ancestors, store.bindings,
                                      store.flattened, store.derived, store.compiledDerived, selects,         store.selectMasks,
                                      enums,           complexes,     store.measures,        groups,          store.groupMasks,
                                      store.prefixes };
};
} // namespace step_semantics::catalog_build
