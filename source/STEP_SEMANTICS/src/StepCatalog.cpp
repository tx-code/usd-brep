// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalog.h"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace step_semantics
{
namespace
{
using Names = std::span<const std::string_view>;
constexpr std::string_view Context[] = { "GEOMETRIC_REPRESENTATION_CONTEXT", "GLOBAL_UNIT_ASSIGNED_CONTEXT", "REPRESENTATION_CONTEXT" };
constexpr std::string_view ContextWithUncertainty[] = { "GEOMETRIC_REPRESENTATION_CONTEXT",
                                                        "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT",
                                                        "GLOBAL_UNIT_ASSIGNED_CONTEXT",
                                                        "REPRESENTATION_CONTEXT" };
constexpr std::string_view SiLength[] = { "LENGTH_UNIT", "NAMED_UNIT", "SI_UNIT" };
constexpr std::string_view SiPlaneAngle[] = { "NAMED_UNIT", "PLANE_ANGLE_UNIT", "SI_UNIT" };
constexpr std::string_view SiSolidAngle[] = { "NAMED_UNIT", "SI_UNIT", "SOLID_ANGLE_UNIT" };
constexpr std::string_view ConversionLength[] = { "CONVERSION_BASED_UNIT", "LENGTH_UNIT", "NAMED_UNIT" };
constexpr Names ComplexSignatures[] = { Context, ContextWithUncertainty, SiLength, SiPlaneAngle, SiSolidAngle, ConversionLength };
static_assert(std::size(ComplexSignatures) == 6);

constexpr std::string_view UnitDefinitions[] = { "SI_UNIT", "CONVERSION_BASED_UNIT" };
constexpr std::string_view NominalQuantities[] = { "LENGTH_UNIT", "PLANE_ANGLE_UNIT", "SOLID_ANGLE_UNIT", "MASS_UNIT" };
constexpr std::string_view ContextBranches[] = { "PRODUCT_CONTEXT", "PRODUCT_DEFINITION_CONTEXT" };
constexpr Names ExclusiveGroups[] = { UnitDefinitions, NominalQuantities, ContextBranches };

struct NamedBranches
{
    std::string_view name;
    Names branches;
};
constexpr std::string_view UnitBranches[] = { "NAMED_UNIT", "DERIVED_UNIT" };
constexpr std::string_view ShellBranches[] = { "OPEN_SHELL", "CLOSED_SHELL" };
constexpr std::string_view CharacterizedBranches[] = { "PRODUCT_DEFINITION", "PRODUCT_DEFINITION_SHAPE" };
constexpr std::string_view RepresentedBranches[] = { "PROPERTY_DEFINITION" };
// Only branches possible under the complete, closed profile are listed here.
constexpr NamedBranches ReferenceSelects[] = { { "UNIT", UnitBranches },
                                               { "SHELL", ShellBranches },
                                               { "CHARACTERIZED_DEFINITION", CharacterizedBranches },
                                               { "REPRESENTED_DEFINITION", RepresentedBranches } };

struct SiPrefix
{
    std::string_view name;
    int power;
};
// Empty is the explicit absent-prefix interpretation, never a guessed source unit.
constexpr SiPrefix Prefixes[] = { { "", 0 },       { "EXA", 18 },  { "PETA", 15 },  { "TERA", 12 },   { "GIGA", 9 },   { "MEGA", 6 },
                                  { "KILO", 3 },   { "HECTO", 2 }, { "DECA", 1 },   { "DECI", -1 },   { "CENTI", -2 }, { "MILLI", -3 },
                                  { "MICRO", -6 }, { "NANO", -9 }, { "PICO", -12 }, { "FEMTO", -15 }, { "ATTO", -18 } };
constexpr std::string_view SiNames[] = { "METRE",   "GRAM",           "SECOND", "AMPERE", "KELVIN",    "MOLE",  "CANDELA",
                                         "RADIAN",  "STERADIAN",      "HERTZ",  "NEWTON", "PASCAL",    "JOULE", "WATT",
                                         "COULOMB", "VOLT",           "FARAD",  "OHM",    "SIEMENS",   "WEBER", "TESLA",
                                         "HENRY",   "DEGREE_CELSIUS", "LUMEN",  "LUX",    "BECQUEREL", "GRAY",  "SIEVERT" };
constexpr std::string_view SourceNames[] = { "MADE", "BOUGHT", "NOT_KNOWN" };
constexpr NamedBranches Enumerations[] = { { "SI_UNIT_NAME", SiNames }, { "SOURCE", SourceNames } };
constexpr std::string_view MeasureBranches[] = { "LENGTH_MEASURE", "NON_NEGATIVE_LENGTH_MEASURE", "POSITIVE_LENGTH_MEASURE" };

constexpr DerivedSlot CommonDerivedSlots[] = { { "ORIENTED_EDGE", "EDGE", "edge_start" },
                                               { "ORIENTED_EDGE", "EDGE", "edge_end" },
                                               { "SI_UNIT", "NAMED_UNIT", "dimensions" } };

bool Contains(Names names, std::string_view value)
{
    return std::find(names.begin(), names.end(), value) != names.end();
}

void ValidateProfile(const Catalog& catalog)
{
    const auto typeId = [&](std::string_view name)
    {
        const auto id = catalog.Find(name);
        if (!id)
        {
            throw std::logic_error("Profile refers to an unregistered declaration");
        }
        return *id;
    };
    for (std::size_t i = 0; i < std::size(ComplexSignatures); ++i)
    {
        const auto signature = ComplexSignatures[i];
        if (!std::is_sorted(signature.begin(), signature.end()) || std::adjacent_find(signature.begin(), signature.end()) != signature.end())
        {
            throw std::logic_error("Complex profile facets must be unique and sorted");
        }
        for (const auto facet : signature)
        {
            const auto id = typeId(facet);
            for (std::size_t parent = 0; parent < catalog.names.size(); ++parent)
            {
                if (catalog.ancestors[id].test(parent) && !Contains(signature, catalog.names[parent]))
                {
                    throw std::logic_error("Complex profile omits an inherited facet");
                }
            }
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (std::equal(signature.begin(), signature.end(), ComplexSignatures[j].begin(), ComplexSignatures[j].end()))
            {
                throw std::logic_error("Duplicate complex profile signature");
            }
        }
    }
    for (const auto group : ExclusiveGroups)
    {
        for (const auto name : group)
        {
            typeId(name);
        }
    }
    for (const auto& select : ReferenceSelects)
    {
        for (const auto name : select.branches)
        {
            typeId(name);
        }
    }
    for (const auto& slot : catalog.derivedSlots)
    {
        const auto owner = typeId(slot.recordType), origin = typeId(slot.declaredIn);
        const auto& fields = catalog.declarations[origin].fields;
        if (!catalog.ancestors[owner].test(origin) || std::none_of(
                                                          fields.begin(),
                                                          fields.end(),
                                                          [&](const Field& field)
                                                          {
                                                              return field.name == slot.field;
                                                          }
                                                      ))
        {
            throw std::logic_error("Derived profile slot has no matching inherited declaration");
        }
    }
    for (const auto& declaration : catalog.declarations)
    {
        for (const auto& field : declaration.fields)
        {
            if (field.kind == FieldKind::Reference || (field.kind == FieldKind::Aggregate && field.elementKind == FieldKind::Reference))
            {
                const bool select = std::any_of(
                    std::begin(ReferenceSelects),
                    std::end(ReferenceSelects),
                    [&](const NamedBranches& entry)
                    {
                        return entry.name == field.type;
                    }
                );
                if (!catalog.Find(field.type) && !select)
                {
                    throw std::logic_error("Reference field has no registered type or SELECT");
                }
            }
            if (field.kind == FieldKind::Enumeration && field.type != "SI_PREFIX" &&
                std::none_of(
                    std::begin(Enumerations),
                    std::end(Enumerations),
                    [&](const NamedBranches& entry)
                    {
                        return entry.name == field.type;
                    }
                ))
            {
                throw std::logic_error("Enumeration field has no registered values");
            }
        }
    }
}

Field Text(const char* name, const char* type = "LABEL", bool optional = false)
{
    return { name, type, FieldKind::String, optional };
}
Field Ref(const char* name, const char* type, bool optional = false)
{
    return { name, type, FieldKind::Reference, optional };
}
Field Real(const char* name, const char* type = "REAL")
{
    return { name, type, FieldKind::Real };
}
Field Enum(const char* name, const char* type, bool optional = false)
{
    return { name, type, FieldKind::Enumeration, optional };
}
Field Bool(const char* name)
{
    return { name, "BOOLEAN", FieldKind::Boolean };
}
Field Aggregate(const char* name, const char* type, std::size_t min, std::size_t max, Unique unique, FieldKind element = FieldKind::Reference)
{
    Field field{ name, type, FieldKind::Aggregate };
    field.elementKind = element;
    field.minimum = min;
    field.maximum = max;
    field.unique = unique;
    return field;
}
Field Set(const char* name, const char* type)
{
    return Aggregate(name, type, 1, static_cast<std::size_t>(-1), Unique::Set);
}
} // namespace

Catalog::Catalog(ProfileKind profile)
{
    // Share the admitted common transport fields/parents; remove the AP203-only legacy contexts below.
    // Registered guard entries are not serialized leaves of this closed profile.
    // Apply protocol-specific removals and DERIVE overrides before creating indexes or field pointers;
    // equal transport layouts do not establish equal WHERE/UNIQUE rules.
    // N2635 entity declarations, audited context/geometry tables. WHERE/UNIQUE/INVERSE evaluation is not implied.
    declarations = {
        { "REPRESENTATION_ITEM", {}, { Text("name") } },
        { "GEOMETRIC_REPRESENTATION_ITEM", { "REPRESENTATION_ITEM" }, {} },
        { "TOPOLOGICAL_REPRESENTATION_ITEM", { "REPRESENTATION_ITEM" }, {} },
        { "POINT", { "GEOMETRIC_REPRESENTATION_ITEM" }, {} },
        { "CARTESIAN_POINT", { "POINT" }, { Aggregate("coordinates", "LENGTH_MEASURE", 1, 3, Unique::None, FieldKind::Real) }, true },
        { "DIRECTION", { "GEOMETRIC_REPRESENTATION_ITEM" }, { Aggregate("direction_ratios", "REAL", 2, 3, Unique::None, FieldKind::Real) }, true },
        { "VECTOR", { "GEOMETRIC_REPRESENTATION_ITEM" }, { Ref("orientation", "DIRECTION"), Real("magnitude", "LENGTH_MEASURE") }, true },
        { "PLACEMENT", { "GEOMETRIC_REPRESENTATION_ITEM" }, { Ref("location", "CARTESIAN_POINT") } },
        { "AXIS2_PLACEMENT_3D", { "PLACEMENT" }, { Ref("axis", "DIRECTION", true), Ref("ref_direction", "DIRECTION", true) }, true },
        { "CURVE", { "GEOMETRIC_REPRESENTATION_ITEM" }, {} },
        { "LINE", { "CURVE" }, { Ref("pnt", "CARTESIAN_POINT"), Ref("dir", "VECTOR") }, true },
        { "SURFACE", { "GEOMETRIC_REPRESENTATION_ITEM" }, {} },
        { "ELEMENTARY_SURFACE", { "SURFACE" }, { Ref("position", "AXIS2_PLACEMENT_3D") } },
        { "PLANE", { "ELEMENTARY_SURFACE" }, {}, true },
        { "VERTEX", { "TOPOLOGICAL_REPRESENTATION_ITEM" }, {} },
        { "VERTEX_POINT", { "VERTEX", "GEOMETRIC_REPRESENTATION_ITEM" }, { Ref("vertex_geometry", "POINT") }, true },
        { "EDGE", { "TOPOLOGICAL_REPRESENTATION_ITEM" }, { Ref("edge_start", "VERTEX"), Ref("edge_end", "VERTEX") } },
        { "EDGE_CURVE", { "EDGE", "GEOMETRIC_REPRESENTATION_ITEM" }, { Ref("edge_geometry", "CURVE"), Bool("same_sense") }, true },
        { "ORIENTED_EDGE", { "EDGE" }, { Ref("edge_element", "EDGE"), Bool("orientation") }, true },
        { "LOOP", { "TOPOLOGICAL_REPRESENTATION_ITEM" }, {} },
        { "PATH", { "TOPOLOGICAL_REPRESENTATION_ITEM" }, { Aggregate("edge_list", "ORIENTED_EDGE", 1, static_cast<std::size_t>(-1), Unique::List) } },
        { "EDGE_LOOP", { "LOOP", "PATH" }, {}, true },
        { "FACE_BOUND", { "TOPOLOGICAL_REPRESENTATION_ITEM" }, { Ref("bound", "LOOP"), Bool("orientation") }, true },
        { "FACE_OUTER_BOUND", { "FACE_BOUND" }, {}, true },
        { "FACE", { "TOPOLOGICAL_REPRESENTATION_ITEM" }, { Set("bounds", "FACE_BOUND") } },
        { "FACE_SURFACE", { "FACE", "GEOMETRIC_REPRESENTATION_ITEM" }, { Ref("face_geometry", "SURFACE"), Bool("same_sense") } },
        { "ADVANCED_FACE", { "FACE_SURFACE" }, {}, true },
        { "CONNECTED_FACE_SET", { "TOPOLOGICAL_REPRESENTATION_ITEM" }, { Set("cfs_faces", "FACE") } },
        { "OPEN_SHELL", { "CONNECTED_FACE_SET" }, {}, true },
        { "CLOSED_SHELL", { "CONNECTED_FACE_SET" }, {}, true },
        { "SOLID_MODEL", { "GEOMETRIC_REPRESENTATION_ITEM" }, {} },
        { "MANIFOLD_SOLID_BREP", { "SOLID_MODEL" }, { Ref("outer", "CLOSED_SHELL") }, true },
        { "SHELL_BASED_SURFACE_MODEL", { "GEOMETRIC_REPRESENTATION_ITEM" }, { Set("sbsm_boundary", "SHELL") }, true },
        { "REPRESENTATION", {}, { Text("name"), Set("items", "REPRESENTATION_ITEM"), Ref("context_of_items", "REPRESENTATION_CONTEXT") } },
        { "SHAPE_REPRESENTATION", { "REPRESENTATION" }, {} },
        { "ADVANCED_BREP_SHAPE_REPRESENTATION", { "SHAPE_REPRESENTATION" }, {}, true },
        { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", { "SHAPE_REPRESENTATION" }, {}, true },
        { "APPLICATION_CONTEXT", {}, { Text("application") }, true },
        { "APPLICATION_CONTEXT_ELEMENT", {}, { Text("name"), Ref("frame_of_reference", "APPLICATION_CONTEXT") } },
        { "APPLICATION_PROTOCOL_DEFINITION",
          {},
          { Text("status"),
            Text("application_interpreted_model_schema_name"),
            { "application_protocol_year", "YEAR_NUMBER", FieldKind::Integer },
            Ref("application", "APPLICATION_CONTEXT") },
          true },
        { "PRODUCT_CONTEXT", { "APPLICATION_CONTEXT_ELEMENT" }, { Text("discipline_type") }, true },
        { "MECHANICAL_CONTEXT", { "PRODUCT_CONTEXT" }, {}, true },
        { "PRODUCT_DEFINITION_CONTEXT", { "APPLICATION_CONTEXT_ELEMENT" }, { Text("life_cycle_stage") }, true },
        { "DESIGN_CONTEXT", { "PRODUCT_DEFINITION_CONTEXT" }, {}, true },
        { "PRODUCT",
          {},
          { Text("id", "IDENTIFIER"), Text("name"), Text("description", "TEXT", true), Set("frame_of_reference", "PRODUCT_CONTEXT") },
          true },
        { "PRODUCT_DEFINITION_FORMATION", {}, { Text("id", "IDENTIFIER"), Text("description", "TEXT", true), Ref("of_product", "PRODUCT") }, true },
        { "PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE", { "PRODUCT_DEFINITION_FORMATION" }, { Enum("make_or_buy", "SOURCE") }, true },
        { "PRODUCT_DEFINITION",
          {},
          { Text("id", "IDENTIFIER"),
            Text("description", "TEXT", true),
            Ref("formation", "PRODUCT_DEFINITION_FORMATION"),
            Ref("frame_of_reference", "PRODUCT_DEFINITION_CONTEXT") },
          true },
        { "PROPERTY_DEFINITION", {}, { Text("name"), Text("description", "TEXT", true), Ref("definition", "CHARACTERIZED_DEFINITION") } },
        { "PRODUCT_DEFINITION_SHAPE", { "PROPERTY_DEFINITION" }, {}, true },
        { "PROPERTY_DEFINITION_REPRESENTATION", {}, { Ref("definition", "REPRESENTED_DEFINITION"), Ref("used_representation", "REPRESENTATION") } },
        { "SHAPE_DEFINITION_REPRESENTATION", { "PROPERTY_DEFINITION_REPRESENTATION" }, {}, true },
        { "PRODUCT_CATEGORY", {}, { Text("name"), Text("description", "TEXT", true) } },
        { "PRODUCT_RELATED_PRODUCT_CATEGORY", { "PRODUCT_CATEGORY" }, { Set("products", "PRODUCT") }, true },
        { "REPRESENTATION_CONTEXT", {}, { Text("context_identifier", "IDENTIFIER"), Text("context_type", "TEXT") } },
        { "GEOMETRIC_REPRESENTATION_CONTEXT",
          { "REPRESENTATION_CONTEXT" },
          { { "coordinate_space_dimension", "DIMENSION_COUNT", FieldKind::Integer } } },
        { "GLOBAL_UNIT_ASSIGNED_CONTEXT", { "REPRESENTATION_CONTEXT" }, { Set("units", "UNIT") } },
        { "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT", { "REPRESENTATION_CONTEXT" }, { Set("uncertainty", "UNCERTAINTY_MEASURE_WITH_UNIT") } },
        { "NAMED_UNIT", {}, { Ref("dimensions", "DIMENSIONAL_EXPONENTS") } },
        { "SI_UNIT", { "NAMED_UNIT" }, { Enum("prefix", "SI_PREFIX", true), Enum("name", "SI_UNIT_NAME") } },
        { "LENGTH_UNIT", { "NAMED_UNIT" }, {} },
        { "PLANE_ANGLE_UNIT", { "NAMED_UNIT" }, {} },
        { "SOLID_ANGLE_UNIT", { "NAMED_UNIT" }, {} },
        { "CONVERSION_BASED_UNIT", { "NAMED_UNIT" }, { Text("name"), Ref("conversion_factor", "MEASURE_WITH_UNIT") } },
        { "DIMENSIONAL_EXPONENTS",
          {},
          { Real("length_exponent"),
            Real("mass_exponent"),
            Real("time_exponent"),
            Real("electric_current_exponent"),
            Real("thermodynamic_temperature_exponent"),
            Real("amount_of_substance_exponent"),
            Real("luminous_intensity_exponent") },
          true },
        { "MEASURE_WITH_UNIT", {}, { { "value_component", "MEASURE_VALUE", FieldKind::Measure }, Ref("unit_component", "UNIT") }, true },
        { "LENGTH_MEASURE_WITH_UNIT", { "MEASURE_WITH_UNIT" }, {}, true },
        { "UNCERTAINTY_MEASURE_WITH_UNIT", { "MEASURE_WITH_UNIT" }, { Text("name"), Text("description", "TEXT", true) }, true },
        { "MASS_UNIT", { "NAMED_UNIT" }, {} }, // Registered guard populations; these are never admitted DATA leaves here.
        { "FOUNDED_ITEM", {}, {} },
        { "DERIVED_UNIT", {}, {} },
        { "DERIVED_UNIT_ELEMENT", {}, {} },
    };
    derivedSlots.assign(std::begin(CommonDerivedSlots), std::end(CommonDerivedSlots));
    if (profile == ProfileKind::Ap214e3Source)
    {
        // These two deprecated AP203 types do not occur in the pinned AP214e3 artifact.
        declarations.erase(
            std::remove_if(
                declarations.begin(),
                declarations.end(),
                [](const Declaration& declaration)
                {
                    return declaration.name == "MECHANICAL_CONTEXT" || declaration.name == "DESIGN_CONTEXT";
                }
            ),
            declarations.end()
        );
        // AP214E3_2010.exp, CONVERSION_BASED_UNIT DERIVE: dimensions is transmitted as '*'.
        // The source value stays a marker; this descriptor must not invent the factor unit's dimensions reference.
        derivedSlots.push_back({ "CONVERSION_BASED_UNIT", "NAMED_UNIT", "dimensions" });
    }
    if (declarations.size() > 128)
    {
        throw std::logic_error("Catalog exceeds compact type capacity");
    }
    for (std::size_t i = 0; i < declarations.size(); ++i)
    {
        if (!ids.emplace(declarations[i].name, i).second)
        {
            throw std::logic_error("Duplicate schema declaration");
        }
        names.push_back(declarations[i].name);
    }
    ancestors.resize(declarations.size());
    flattened.resize(declarations.size());
    std::vector<unsigned char> state(declarations.size(), 0);
    std::function<void(std::size_t)> visit = [&](std::size_t id)
    {
        if (state[id] == 2)
        {
            return;
        }
        if (state[id] == 1)
        {
            throw std::logic_error("Schema inheritance cycle");
        }
        state[id] = 1;
        ancestors[id].set(id);
        std::unordered_set<std::string> fields;
        for (const auto& name : declarations[id].parents)
        {
            const auto parent = ids.at(name);
            visit(parent);
            ancestors[id] |= ancestors[parent];
            for (const auto& binding : flattened[parent])
            {
                if (fields.insert(std::string(binding.origin) + "." + binding.field->name).second)
                {
                    flattened[id].push_back(binding);
                }
            }
        }
        for (const auto& field : declarations[id].fields)
        {
            flattened[id].push_back({ &field, declarations[id].name });
        }
        state[id] = 2;
    };
    for (std::size_t i = 0; i < declarations.size(); ++i)
    {
        visit(i);
    }
    ValidateProfile(*this);
}

std::optional<std::size_t> Catalog::Find(std::string_view name) const
{
    const auto found = ids.find(std::string(name));
    return found == ids.end() ? std::nullopt : std::optional<std::size_t>(found->second);
}
bool Catalog::HasField(std::size_t type, std::string_view name) const
{
    return std::any_of(
        flattened.at(type).begin(),
        flattened.at(type).end(),
        [&](const FieldBinding& binding)
        {
            return binding.field->name == name;
        }
    );
}
bool Catalog::AdmitsComplex(std::span<const std::string> facets) const
{
    return std::any_of(
        std::begin(ComplexSignatures),
        std::end(ComplexSignatures),
        [&](Names signature)
        {
            return std::equal(facets.begin(), facets.end(), signature.begin(), signature.end());
        }
    );
}
bool Catalog::HasSubtypeConflict(const std::bitset<128>& types) const
{
    for (const auto group : ExclusiveGroups)
    {
        std::size_t count = 0;
        for (const auto name : group)
        {
            count += types.test(ids.at(std::string(name))) ? 1 : 0;
        }
        if (count > 1)
        {
            return true;
        }
    }
    return false;
}
bool Catalog::MatchesReference(const std::bitset<128>& types, std::string_view expected) const
{
    for (const auto& select : ReferenceSelects)
    {
        if (select.name == expected)
        {
            return std::any_of(
                select.branches.begin(),
                select.branches.end(),
                [&](std::string_view name)
                {
                    return types.test(ids.at(std::string(name)));
                }
            );
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
            std::begin(Prefixes),
            std::end(Prefixes),
            [&](const SiPrefix& entry)
            {
                return entry.name == value;
            }
        );
    }
    for (const auto& entry : Enumerations)
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
    return Contains(MeasureBranches, type);
}
int Catalog::PrefixPower10(std::string_view prefix) const
{
    for (const auto& entry : Prefixes)
    {
        if (entry.name == prefix)
        {
            return entry.power;
        }
    }
    throw std::out_of_range("Unregistered SI prefix");
}
std::string_view Catalog::RedeclaredBy(const std::bitset<128>& types, const FieldBinding& binding) const
{
    for (const auto& slot : derivedSlots)
    {
        if (binding.origin == slot.declaredIn && binding.field->name == slot.field && types.test(ids.at(std::string(slot.recordType))))
        {
            return slot.recordType;
        }
    }
    return {};
}
const Catalog& Schema(ProfileKind profile)
{
    // Each snapshot keeps its own immutable catalog identity, so interleaved imports cannot change
    // existing type indexes or the lifetime of declaration pointers stored in field bindings.
    switch (profile)
    {
        case ProfileKind::Ap203e2Planar:
        {
            static const Catalog catalog(ProfileKind::Ap203e2Planar);
            return catalog;
        }
        case ProfileKind::Ap214e3Source:
        {
            static const Catalog catalog(ProfileKind::Ap214e3Source);
            return catalog;
        }
    }
    throw std::logic_error("Unimplemented source declaration catalog");
}
const Catalog& Schema(const SemanticData& data)
{
    // Failed binding never publishes a typed snapshot or enters field interpretation.
    // A missing catalog here is a programmer phase-order error, not a malformed STEP value.
    Expects(data.catalog.has_value());
    return **data.catalog;
}

AttributeView AttributeAt(const SemanticData& data, std::size_t r, std::size_t c, std::size_t p)
{
    const auto& catalog = Schema(data);
    const auto& record = data.source->Records().at(r);
    const auto& component = record.components.at(c);
    const auto type = catalog.ids.at(component.name);
    const auto binding = record.complex ? FieldBinding{ &catalog.declarations[type].fields.at(p), component.name } : catalog.flattened[type].at(p);
    AttributeView view;
    view.name = binding.field->name;
    view.declaredIn = binding.origin;
    view.declaredType = binding.field->type;
    view.optional = binding.field->optional;
    view.value = &component.parameters.at(p);
    if (binding.field->kind == FieldKind::Aggregate)
    {
        view.aggregate = binding.field->unique == Unique::Set ? "SET" : "LIST";
        view.minimum = binding.field->minimum;
        view.uniqueElements = binding.field->unique != Unique::None;
        if (binding.field->maximum != static_cast<std::size_t>(-1))
        {
            view.maximum = binding.field->maximum;
        }
    }
    view.redeclaredBy = catalog.RedeclaredBy(data.types[r], binding);
    view.derivedSlot = !view.redeclaredBy.empty();
    return view;
}
} // namespace step_semantics
