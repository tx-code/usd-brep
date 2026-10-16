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

Catalog::Catalog()
{
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
const Catalog& Schema()
{
    static const Catalog catalog;
    return catalog;
}

AttributeView AttributeAt(const SemanticData& data, std::size_t r, std::size_t c, std::size_t p)
{
    const auto& catalog = Schema();
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
    if (component.name == "ORIENTED_EDGE" && (view.name == "edge_start" || view.name == "edge_end"))
    {
        view.derivedSlot = true;
        view.redeclaredBy = "ORIENTED_EDGE";
    }
    if (component.name == "NAMED_UNIT" && data.types[r].test(catalog.ids.at("SI_UNIT")))
    {
        view.derivedSlot = true;
        view.redeclaredBy = "SI_UNIT";
    }
    return view;
}
} // namespace step_semantics
