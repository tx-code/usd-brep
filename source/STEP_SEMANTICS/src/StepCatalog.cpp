// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalog.h"

#include "StepCatalogRecipes.h"

#include <algorithm>
#include <stdexcept>

namespace step_semantics
{
namespace
{
using Names = std::span<const std::string_view>;
bool Contains(Names names, std::string_view value)
{
    return std::find(names.begin(), names.end(), value) != names.end();
}
// Every profile is instantiated here in the ordinary library build. All stores,
// their dependent views and the catalog roots are constexpr: no initialization
// guard, heap construction or metadata validation remains on first access.
using Planar = catalog_build::CompiledCatalog<catalog_build::ProfileRecipe<ProfileKind::Ap203e2Planar>>;
using General = catalog_build::CompiledCatalog<catalog_build::ProfileRecipe<ProfileKind::Ap203e2GeneralSource>>;
using Ap214 = catalog_build::CompiledCatalog<catalog_build::ProfileRecipe<ProfileKind::Ap214e3Source>>;
using Mbx = catalog_build::CompiledCatalog<catalog_build::ProfileRecipe<ProfileKind::Ap214MbxSource>>;
} // namespace
std::optional<std::size_t> Catalog::Find(std::string_view name) const
{
    return LookupCatalogName(nameIndex, name);
}
std::size_t Catalog::RequireType(std::string_view name) const
{
    const auto type = Find(name);
    if (!type)
    {
        throw std::out_of_range("Unregistered catalog type");
    }
    return *type;
}
std::span<const FieldBinding> Catalog::Flattened(std::size_t type) const
{
    const auto range = CatalogAt(flattenedRanges, type);
    return bindings.subspan(range.offset, range.count);
}
const Field& Catalog::FieldOf(FieldBinding binding) const
{
    return CatalogAt(CatalogAt(declarations, binding.origin).fields, binding.field);
}
std::string_view Catalog::OriginOf(FieldBinding binding) const
{
    return CatalogAt(names, binding.origin);
}
bool Catalog::HasField(std::size_t type, std::string_view name) const
{
    const auto fields = Flattened(type);
    return std::any_of(
        fields.begin(),
        fields.end(),
        [&](FieldBinding binding)
        {
            return FieldOf(binding).name == name;
        }
    );
}
bool Catalog::AdmitsComplex(std::span<const std::string> facets) const
{
    return std::any_of(
        complexSignatures.begin(),
        complexSignatures.end(),
        [&](Names signature)
        {
            return std::equal(facets.begin(), facets.end(), signature.begin(), signature.end());
        }
    );
}
bool Catalog::HasSubtypeConflict(const TypeMembership& types) const
{
    return std::any_of(
        exclusiveMasks.begin(),
        exclusiveMasks.end(),
        [&](const TypeMembership& group)
        {
            return types.SharedCount(group) > 1;
        }
    );
}
bool Catalog::MatchesReference(const TypeMembership& types, std::string_view expected) const
{
    for (std::size_t i = 0; i < referenceSelects.size(); ++i)
    {
        if (referenceSelects[i].name == expected)
        {
            return types.SharedCount(selectMasks[i]) != 0;
        }
    }
    const auto type = Find(expected);
    return type && types.test(*type);
}
bool Catalog::ContainsEnumeration(std::string_view type, std::string_view value) const
{
    if (type == "SI_PREFIX")
    {
        return std::any_of(
            prefixes.begin(),
            prefixes.end(),
            [&](const SiPrefix& prefix)
            {
                return prefix.name == value;
            }
        );
    }
    for (const auto& entry : enumerations)
    {
        if (entry.name == type)
        {
            return Contains(entry.branches, value);
        }
    }
    return false;
}
bool Catalog::AdmitsMeasure(std::string_view type) const
{
    return Contains(measureBranches, type);
}
int Catalog::PrefixPower10(std::string_view prefix) const
{
    for (const auto& entry : prefixes)
    {
        if (entry.name == prefix)
        {
            return entry.power;
        }
    }
    throw std::out_of_range("Unregistered SI prefix");
}
std::string_view Catalog::RedeclaredBy(const TypeMembership& types, const FieldBinding& binding) const
{
    // Preserve registration priority when more than one redeclaring subtype is
    // present. This describes a source '*' slot; it never evaluates DERIVE.
    for (const auto& slot : compiledDerivedSlots)
    {
        if (binding.origin == slot.origin && binding.field == slot.field && types.test(slot.owner))
        {
            return names[slot.owner];
        }
    }
    return {};
}
std::span<const SiPrefix> Catalog::PrefixEntries() const noexcept
{
    return prefixes;
}
std::span<const std::span<const std::string_view>> Catalog::ExclusiveSubtypeGroups() const noexcept
{
    return exclusiveGroups;
}
const Catalog& Schema(ProfileKind profile)
{
    // Independent static identities outlive every interleaved owning snapshot.
    switch (profile)
    {
        case ProfileKind::Ap203e2Planar:
            return Planar::catalog;
        case ProfileKind::Ap203e2GeneralSource:
            return General::catalog;
        case ProfileKind::Ap214e3Source:
            return Ap214::catalog;
        case ProfileKind::Ap214MbxSource:
            return Mbx::catalog;
    }
    throw std::logic_error("Unimplemented source declaration catalog");
}
const Catalog& Schema(const SemanticData& data)
{
    // Failed binding never publishes a snapshot or enters field interpretation.
    Expects(data.catalog.has_value());
    return **data.catalog;
}
AttributeView AttributeAt(const SemanticData& data, std::size_t r, std::size_t c, std::size_t p)
{
    const auto& catalog = Schema(data);
    const auto& record = data.source->Records().at(r);
    const auto& component = record.components.at(c);
    const auto type = catalog.RequireType(component.name);
    const auto binding = record.complex ? FieldBinding{ type, p } : CatalogAt(catalog.Flattened(type), p);
    const auto& field = catalog.FieldOf(binding);
    AttributeView view;
    view.name = field.name;
    view.declaredIn = catalog.OriginOf(binding);
    view.declaredType = field.type;
    view.optional = field.optional;
    view.nestedAggregates = field.nestedAggregates;
    // Metadata is static, but actual values and source locations still belong
    // to the immutable SourceSnapshot retained by Snapshot/FieldView.
    view.value = &component.parameters.at(p);
    if (field.kind == FieldKind::Aggregate)
    {
        view.aggregate = field.unique == Unique::Set ? "SET" : "LIST";
        view.minimum = field.minimum;
        view.uniqueElements = field.unique != Unique::None;
        if (field.maximum != static_cast<std::size_t>(-1))
        {
            view.maximum = field.maximum;
        }
    }
    view.redeclaredBy = catalog.RedeclaredBy(data.types[r], binding);
    view.derivedSlot = !view.redeclaredBy.empty();
    return view;
}
} // namespace step_semantics
