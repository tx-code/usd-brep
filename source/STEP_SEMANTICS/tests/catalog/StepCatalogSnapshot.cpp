// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalog.h"
#include "StepSourceReport.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace step_semantics;
using step_source::report::Writer;

template <class Range, class Emit>
void Array(Writer& out, const Range& range, Emit emit)
{
    out.Add("[");
    bool first = true;
    for (const auto& entry : range)
    {
        if (!first)
        {
            out.Add(",");
        }
        first = false;
        emit(entry);
    }
    out.Add("]");
}
template <class Range>
void Names(Writer& out, const Range& names)
{
    Array(
        out,
        names,
        [&](const auto& name)
        {
            out.String(name);
        }
    );
}
std::string_view Kind(FieldKind kind)
{
    switch (kind)
    {
        case FieldKind::String:
            return "String";
        case FieldKind::Real:
            return "Real";
        case FieldKind::Integer:
            return "Integer";
        case FieldKind::Boolean:
            return "Boolean";
        case FieldKind::Logical:
            return "Logical";
        case FieldKind::Enumeration:
            return "Enumeration";
        case FieldKind::Reference:
            return "Reference";
        case FieldKind::Aggregate:
            return "Aggregate";
        case FieldKind::Measure:
            return "Measure";
        case FieldKind::SizeSelect:
            return "SizeSelect";
        case FieldKind::TrimmingSelect:
            return "TrimmingSelect";
    }
    throw std::logic_error("Unserialized FieldKind");
}
std::string_view Uniqueness(Unique unique)
{
    switch (unique)
    {
        case Unique::None:
            return "None";
        case Unique::Set:
            return "Set";
        case Unique::List:
            return "List";
    }
    throw std::logic_error("Unserialized Unique");
}
void FieldJson(Writer& out, const Field& field)
{
    out.Add("{\"name\":");
    out.String(field.name);
    out.Add(",\"type\":");
    out.String(field.type);
    out.Add(",\"kind\":");
    out.String(Kind(field.kind));
    out.Add(",\"optional\":");
    out.Bool(field.optional);
    out.Add(",\"element_kind\":");
    out.String(Kind(field.elementKind));
    out.Add(",\"minimum\":");
    out.Number(field.minimum);
    out.Add(",\"maximum\":");
    if (field.maximum == std::numeric_limits<std::size_t>::max())
    {
        out.Add("null");
    }
    else
    {
        out.Number(field.maximum);
    }
    out.Add(",\"unique\":");
    out.String(Uniqueness(field.unique));
    out.Add(",\"nested_aggregates\":");
    Array(
        out,
        field.nestedAggregates,
        [&](const AggregateDimension& dimension)
        {
            out.Add("{\"aggregate\":");
            out.String(dimension.aggregate);
            out.Add(",\"minimum\":");
            out.Number(dimension.minimum);
            out.Add(",\"maximum\":");
            if (dimension.maximum)
            {
                out.Number(*dimension.maximum);
            }
            else
            {
                out.Add("null");
            }
            out.Add(",\"unique\":");
            out.Bool(dimension.uniqueElements);
            out.Add("}");
        }
    );
    out.Add("}");
}
struct ProfileEntry
{
    ProfileKind kind;
    std::string_view name, id, artifact;
};
constexpr ProfileEntry Profiles[] = {
    { ProfileKind::Ap203e2Planar, "Ap203e2Planar", Profile, SchemaSha256 },
    { ProfileKind::Ap203e2GeneralSource, "Ap203e2GeneralSource", Ap203GeneralSourceProfile, SchemaSha256 },
    { ProfileKind::Ap214e3Source, "Ap214e3Source", Ap214SourceProfile, Ap214SchemaSha256 },
    { ProfileKind::Ap214MbxSource, "Ap214MbxSource", Ap214MbxSourceProfile, Ap214SchemaSha256 },
    { ProfileKind::Ap203N916Source, "Ap203N916Source", Ap203N916SourceProfile, Ap203N916SchemaSha256 },
};
void ProfileJson(Writer& out, const ProfileEntry& profile)
{
    const auto& catalog = Schema(profile.kind);
    out.Add("{\"kind\":");
    out.String(profile.name);
    out.Add(",\"profile\":");
    out.String(profile.id);
    out.Add(",\"schema_sha256\":");
    out.String(profile.artifact);
    out.Add(",\"membership_capacity\":");
    out.Number(TypeMembership{}.size());
    out.Add(",\"registered_types\":");
    Names(out, catalog.names);
    out.Add(",\"name_index\":");
    // Only this derived lookup index is sorted. Declaration, parent, field and
    // registry order are observable and must survive the representation change.
    const auto index = catalog.nameIndex;
    Array(
        out,
        index,
        [&](const auto& item)
        {
            if (catalog.Find(item.name) != item.id)
            {
                throw std::logic_error("Find disagrees with name index");
            }
            out.Add("{\"name\":");
            out.String(item.name);
            out.Add(",\"id\":");
            out.Number(item.id);
            out.Add("}");
        }
    );
    out.Add(",\"declarations\":[");
    for (std::size_t id = 0; id < catalog.declarations.size(); ++id)
    {
        if (id)
        {
            out.Add(",");
        }
        const auto& declaration = catalog.declarations[id];
        out.Add("{\"id\":");
        out.Number(id);
        out.Add(",\"name\":");
        out.String(declaration.name);
        out.Add(",\"parents\":");
        Names(out, declaration.parents);
        out.Add(",\"simple\":");
        out.Bool(declaration.simple);
        out.Add(",\"fields\":");
        Array(
            out,
            declaration.fields,
            [&](const Field& field)
            {
                FieldJson(out, field);
            }
        );
        out.Add(",\"ancestors\":[");
        bool first = true;
        // Include unused high bits too: a truncated comparison would hide
        // garbage membership beyond the last registered declaration.
        for (std::size_t bit = 0; bit < TypeMembership{}.size(); ++bit)
        {
            if (!CatalogAt(catalog.ancestors, id).test(bit))
            {
                continue;
            }
            if (!first)
            {
                out.Add(",");
            }
            first = false;
            out.Number(bit);
        }
        out.Add("],\"flattened\":");
        Array(
            out,
            catalog.Flattened(id),
            [&](const FieldBinding& binding)
            {
                const auto origin = binding.origin;
                const auto& fields = CatalogAt(catalog.declarations, origin).fields;
                if (&catalog.FieldOf(binding) != &CatalogAt(fields, binding.field) ||
                    catalog.OriginOf(binding) != CatalogAt(catalog.declarations, origin).name)
                {
                    throw std::logic_error("Flattened field does not belong to its origin");
                }
                out.Add("{\"declaring_type\":");
                out.String(catalog.OriginOf(binding));
                out.Add(",\"declaring_type_id\":");
                out.Number(origin);
                out.Add(",\"local_field_index\":");
                out.Number(binding.field);
                out.Add("}");
            }
        );
        out.Add("}");
    }
    out.Add("],\"reference_selects\":");
    const auto branches = [&](const NamedBranches& entry)
    {
        out.Add("{\"name\":");
        out.String(entry.name);
        out.Add(",\"branches\":");
        Names(out, entry.branches);
        out.Add("}");
    };
    Array(out, catalog.referenceSelects, branches);
    out.Add(",\"enumerations\":");
    Array(out, catalog.enumerations, branches);
    out.Add(",\"complex_signatures\":");
    Array(
        out,
        catalog.complexSignatures,
        [&](const auto& signature)
        {
            Names(out, signature);
        }
    );
    out.Add(",\"measure_branches\":");
    Names(out, catalog.measureBranches);
    out.Add(",\"derived_slots\":");
    Array(
        out,
        catalog.derivedSlots,
        [&](const DerivedSlot& slot)
        {
            out.Add("{\"record_type\":");
            out.String(slot.recordType);
            out.Add(",\"declared_in\":");
            out.String(slot.declaredIn);
            out.Add(",\"field\":");
            out.String(slot.field);
            out.Add("}");
        }
    );
    out.Add(",\"exclusive_groups\":");
    Array(
        out,
        catalog.ExclusiveSubtypeGroups(),
        [&](const auto& group)
        {
            Names(out, group);
        }
    );
    out.Add(",\"si_prefixes\":");
    Array(
        out,
        catalog.PrefixEntries(),
        [&](const SiPrefix& prefix)
        {
            out.Add("{\"name\":");
            out.String(prefix.name);
            out.Add(",\"power\":");
            out.Add(std::to_string(prefix.power));
            out.Add("}");
        }
    );
    out.Add("}");
}
} // namespace

int main()
{
    try
    {
        std::string output;
        // A test artifact bound, independent of the input/report acceptance
        // budgets. Publish only a complete snapshot, never a partial baseline.
        if (!Writer::Publish(
                16 * 1024 * 1024,
                output,
                [](Writer& out)
                {
                    out.Add("{\"format\":\"step-catalog-snapshot.v2\",\"profiles\":");
                    Array(
                        out,
                        Profiles,
                        [&](const ProfileEntry& profile)
                        {
                            ProfileJson(out, profile);
                        }
                    );
                    out.Add("}");
                }
            ))
        {
            throw std::length_error("Catalog snapshot exceeds test artifact bound");
        }
        std::cout << output << '\n';
        return std::cout ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
