// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepPlanarInternal.h"

#include <algorithm>
#include <set>

namespace step_planar
{
bool Validator::LocalValue(const Clause& c, std::size_t r, RuleEvidence& e)
{
    const std::string type(c.type), label(c.label), kind(c.kind);
    const auto has = [&](std::size_t index, std::string_view name)
    {
        const auto types = sem.TypesOf(index);
        return std::find(types.begin(), types.end(), name) != types.end();
    };
    const auto countTypes = [&](std::size_t index, std::initializer_list<std::string_view> names)
    {
        std::size_t count = 0;
        for (const auto name : names)
        {
            count += has(index, name) ? 1 : 0;
        }
        return count;
    };
    const auto forall = [&](const std::vector<std::size_t>& population, const auto& predicate)
    {
        bool value = true;
        for (const auto member : population)
        {
            Tick(member);
            value = predicate(member) && value;
        }
        return value;
    };
    if (kind == "UNIQUE")
    {
        e.proof = "Singleton full-file supertype population; pairwise UNIQUE has no distinct pair";
        e.populations[type] = Population(type);
        return e.populations[type].size() == 1;
    }
    if (kind == "INVERSE")
    {
        const bool ac = type == "APPLICATION_CONTEXT";
        const auto uses = sem.UsedIn(
            Record(r).id,
            ac ? "APPLICATION_CONTEXT_ELEMENT" : "REPRESENTATION",
            ac ? "frame_of_reference" : "context_of_items"
        );
        for (const auto index : uses)
        {
            e.witnesses.push_back(src.References()[index].recordIndex);
        }
        e.facts["count"] = std::to_string(uses.size());
        e.proof = "Complete DATA reverse index with declaring role; SET[1:?] cardinality";
        return !uses.empty();
    }
    // These original WHEREs count metadata assignments. All actual signatures were already admitted by
    // ticket 03, which excludes these assignment types and all their subtypes/facets over the complete file.
    if (type == "APPLICATION_CONTEXT" || type == "PRODUCT_CATEGORY" || type == "PRODUCT_DEFINITION" || type == "PROPERTY_DEFINITION" ||
        type == "PROPERTY_DEFINITION_REPRESENTATION" || type == "REPRESENTATION")
    {
        e.proof = "ClosedProfileTypeExclusion: ID_ATTRIBUTE/DESCRIPTION_ATTRIBUTE/NAME_ATTRIBUTE; inverse count 0 <= 1";
        e.facts["assignment_count"] = "0";
        e.facts["derived_getter"] = "INDETERMINATE_VALUE(?) from zero assignments, not an empty string";
        return true;
    }
    if (type == "REPRESENTATION_ITEM")
    {
        e.witnesses = { Role("representation") };
        e.facts["using_representation_count"] = sem.UsedByRepresentation(r) ? "1" : "0";
        e.proof = "using_representations: full RI-filtered transitive uses plus REPRESENTATION.items, management users excluded";
        return sem.UsedByRepresentation(r);
    }
    if (type == "GEOMETRIC_REPRESENTATION_ITEM")
    {
        e.witnesses = { Role("representation"), Role("unit_context") };
        e.proof = "Single admitted representation; every collected using representation has a GRC context";
        return Is(Role("unit_context"), "GEOMETRIC_REPRESENTATION_CONTEXT");
    }
    if (type == "DIRECTION")
    {
        e.proof = "QUERY of source ratios <> 0, using exact decimal values, no epsilon";
        return std::any_of(
            Field(r, "direction_ratios").children.begin(),
            Field(r, "direction_ratios").children.end(),
            [](const Value& v)
            {
                return Rational::Decimal(v.text) != Rational();
            }
        );
    }
    if (type == "VECTOR")
    {
        e.proof = "Source magnitude >= 0; zero is allowed by this WHERE";
        return !(Rational::Decimal(Field(r, "magnitude").text) < Rational());
    }
    if (type == "AXIS2_PLACEMENT_3D")
    {
        const auto present = [&](const char* name)
        {
            return Field(r, name).kind != step_source::ValueKind::Unset;
        };
        e.proof = "Original optional predicates; WR4 requires cross magnitude > 0 only when both fields exist";
        if (label == "WR1")
        {
            return Field(Ref(r, "location"), "coordinates").children.size() == 3;
        }
        if (label == "WR2")
        {
            return !present("axis") || Field(Ref(r, "axis"), "direction_ratios").children.size() == 3;
        }
        if (label == "WR3")
        {
            return !present("ref_direction") || Field(Ref(r, "ref_direction"), "direction_ratios").children.size() == 3;
        }
        if (label == "WR4")
        {
            return !present("axis") || !present("ref_direction") ||
                   Squared(Cross(Direction(Ref(r, "axis")), Direction(Ref(r, "ref_direction")))) > Rational();
        }
    }
    if (type == "LINE")
    {
        e.proof = "Dimension of dir.orientation ratios equals number of pnt coordinates";
        return Field(Ref(Ref(r, "dir"), "orientation"), "direction_ratios").children.size() == Field(Ref(r, "pnt"), "coordinates").children.size();
    }
    if (type == "ORIENTED_EDGE")
    {
        e.proof = "edge_element TYPEOF excludes ORIENTED_EDGE";
        return !Is(Ref(r, "edge_element"), "ORIENTED_EDGE");
    }
    if (type == "PATH" || type == "EDGE_LOOP")
    {
        const auto edges = Refs(r, "edge_list");
        e.witnesses = edges;
        if (type == "EDGE_LOOP")
        {
            const auto start = Ends(edges.front()).first, end = Ends(edges.back()).second;
            e.facts["ne"] = std::to_string(edges.size());
            e.facts["first_start_id"] = Record(start).id;
            e.facts["last_end_id"] = Record(end).id;
            e.proof = "First derived OE.start :=: last derived OE.end; instance identity";
            return start == end;
        }
        std::size_t bad = 0;
        for (std::size_t i = 1; i < edges.size(); ++i)
        {
            Tick(r);
            bad += Ends(edges[i - 1]).second != Ends(edges[i]).first ? 1 : 0;
        }
        e.facts["adjacent_identity_mismatches"] = std::to_string(bad);
        e.proof = "path_head_to_tail checks adjacent identities only; final closure belongs to EDGE_LOOP.WR1";
        return bad == 0;
    }
    if (type == "FACE")
    {
        std::size_t outer = 0, poly = 0;
        for (const auto bound : Refs(r, "bounds"))
        {
            outer += Is(bound, "FACE_OUTER_BOUND") ? 1 : 0;
            poly += has(Ref(bound, "bound"), "POLY_LOOP") ? 1 : 0;
        }
        e.proof = label == "WR1" ? "mixed_loop_type_set: all actual bounds use EDGE_LOOP" :
                                   "Explicit FACE_OUTER_BOUND count <= 1; not the profile's count = 1";
        e.facts["outer_count"] = std::to_string(outer);
        e.facts["poly_loop_count"] = std::to_string(poly);
        return label == "WR1" ? (poly == 0 || poly == Refs(r, "bounds").size()) : outer <= 1;
    }
    if (type == "FACE_SURFACE")
    {
        e.proof = "Direct PLANE TYPEOF excludes ORIENTED_SURFACE";
        return !has(Ref(r, "face_geometry"), "ORIENTED_SURFACE");
    }
    if (type == "ADVANCED_FACE")
    {
        const auto geometry = Ref(r, "face_geometry");
        std::vector<std::size_t> oe;
        for (const auto b : Refs(r, "bounds"))
        {
            for (const auto edge : Refs(Ref(b, "bound"), "edge_list"))
            {
                oe.push_back(edge);
            }
        }
        e.witnesses = Refs(r, "bounds");
        e.witnesses.push_back(geometry);
        e.proof = "Actual direct PLANE / EDGE_LOOP / EDGE_CURVE / LINE / VERTEX_POINT / CARTESIAN_POINT types, including ancestors";
        if (label == "WR1")
        {
            return countTypes(geometry, { "ELEMENTARY_SURFACE", "B_SPLINE_SURFACE", "SWEPT_SURFACE" }) == 1;
        }
        if (label == "WR2")
        {
            return forall(
                oe,
                [&](auto x)
                {
                    return Is(Ref(x, "edge_element"), "EDGE_CURVE");
                }
            );
        }
        if (label == "WR3")
        {
            return forall(
                oe,
                [&](auto x)
                {
                    return countTypes(
                               Ref(Ref(x, "edge_element"), "edge_geometry"),
                               { "LINE", "CONIC", "POLYLINE", "SURFACE_CURVE", "B_SPLINE_CURVE" }
                           ) == 1;
                }
            );
        }
        if (label == "WR4")
        {
            return forall(
                oe,
                [&](auto x)
                {
                    const auto ends = Ends(x);
                    return Is(ends.first, "VERTEX_POINT") && Is(ends.second, "VERTEX_POINT") &&
                           Is(Ref(ends.first, "vertex_geometry"), "CARTESIAN_POINT") && Is(Ref(ends.second, "vertex_geometry"), "CARTESIAN_POINT");
                }
            );
        }
        if (label == "WR5")
        {
            return forall(
                Refs(r, "bounds"),
                [&](auto b)
                {
                    return !has(Ref(b, "bound"), "ORIENTED_PATH");
                }
            );
        }
        if (label == "WR6" || label == "WR10")
        {
            e.proof = "No SWEPT_SURFACE; for WR10 also no POLYLINE edge geometry, proven by actual types";
            return !has(geometry, "SWEPT_SURFACE") && forall(
                                                          oe,
                                                          [&](auto x)
                                                          {
                                                              return !has(Ref(Ref(x, "edge_element"), "edge_geometry"), "POLYLINE");
                                                          }
                                                      );
        }
        if (label == "WR7")
        {
            e.proof = "Actual bounds have no VERTEX_LOOP, so failing QUERY is empty";
            return forall(
                Refs(r, "bounds"),
                [&](auto b)
                {
                    return !has(Ref(b, "bound"), "VERTEX_LOOP");
                }
            );
        }
        if (label == "WR8")
        {
            return forall(
                Refs(r, "bounds"),
                [&](auto b)
                {
                    return countTypes(Ref(b, "bound"), { "EDGE_LOOP", "VERTEX_LOOP" }) == 1;
                }
            );
        }
        if (label == "WR9")
        {
            e.proof = "All actual EC geometries are direct LINE; SURFACE_CURVE query empty";
            return forall(
                oe,
                [&](auto x)
                {
                    return !has(Ref(Ref(x, "edge_element"), "edge_geometry"), "SURFACE_CURVE");
                }
            );
        }
    }
    if (type == "ADVANCED_BREP_SHAPE_REPRESENTATION" || type == "MANIFOLD_SURFACE_SHAPE_REPRESENTATION")
    {
        const bool solid = type == "ADVANCED_BREP_SHAPE_REPRESENTATION";
        const auto items = Refs(r, "items");
        const auto shell = Role("shell");
        const int number = std::stoi(label.substr(2));
        e.witnesses = items;
        e.witnesses.push_back(shell);
        e.proof = "Complete admitted items and shell face population; mapped/void/oriented wrapper populations excluded by exact profile signatures";
        if (number == 1)
        {
            return forall(
                items,
                [&](auto x)
                {
                    return solid ? countTypes(x, { "MANIFOLD_SOLID_BREP", "FACETED_BREP", "MAPPED_ITEM", "AXIS2_PLACEMENT_3D" }) == 1 :
                                   countTypes(x, { "SHELL_BASED_SURFACE_MODEL", "MAPPED_ITEM", "AXIS2_PLACEMENT_3D" }) == 1;
                }
            );
        }
        if (number == 2)
        {
            return std::any_of(
                items.begin(),
                items.end(),
                [&](auto x)
                {
                    return Is(x, solid ? "MANIFOLD_SOLID_BREP" : "SHELL_BASED_SURFACE_MODEL");
                }
            );
        }
        if (solid && number == 3)
        {
            return forall(
                Refs(shell, "cfs_faces"),
                [&](auto x)
                {
                    return Is(x, "ADVANCED_FACE");
                }
            );
        }
        if (solid && number == 4)
        {
            return !has(shell, "ORIENTED_CLOSED_SHELL");
        }
        if (solid && (number == 5 || number == 6))
        {
            return forall(
                items,
                [&](auto x)
                {
                    return !has(x, number == 5 ? "BREP_WITH_VOIDS" : "MAPPED_ITEM");
                }
            );
        }
        if (!solid && number == 3)
        {
            return forall(
                items,
                [&](auto x)
                {
                    return !has(x, "MAPPED_ITEM");
                }
            );
        }
        if (!solid && number == 4)
        {
            return countTypes(shell, { "OPEN_SHELL", "ORIENTED_CLOSED_SHELL", "CLOSED_SHELL" }) == 1;
        }
        if (!solid && number == 5)
        {
            return forall(
                Refs(shell, "cfs_faces"),
                [&](auto x)
                {
                    return Is(x, "FACE_SURFACE");
                }
            );
        }
        if (!solid && number >= 6 && number <= 14)
        {
            e.proof = "Every face is ADVANCED_FACE: original OR first term TRUE. Plain-FS branch unexecuted; N2635 WR8-WR14 access paths unchanged";
            e.facts["plain_face_surface_branch"] = "NotRun";
            return forall(
                Refs(shell, "cfs_faces"),
                [&](auto x)
                {
                    return Is(x, "ADVANCED_FACE");
                }
            );
        }
    }
    if (type == "SHELL_BASED_SURFACE_MODEL")
    {
        e.proof = "constraints_geometry_shell_based_surface_model: every boundary is OPEN_SHELL or CLOSED_SHELL";
        return forall(
            Refs(r, "sbsm_boundary"),
            [&](auto x)
            {
                return Is(x, "OPEN_SHELL") || Is(x, "CLOSED_SHELL");
            }
        );
    }
    if (type == "PRODUCT_DEFINITION_SHAPE")
    {
        e.proof = "definition is PRODUCT_DEFINITION, in characterized_product_definition SELECT";
        return Is(Ref(r, "definition"), "PRODUCT_DEFINITION");
    }
    if (type == "SHAPE_DEFINITION_REPRESENTATION")
    {
        e.proof = "Original direct-PDS first disjunct or shape representation type";
        return label == "WR1" ? Is(Ref(r, "definition"), "PRODUCT_DEFINITION_SHAPE") : Is(Ref(r, "used_representation"), "SHAPE_REPRESENTATION");
    }
    if (type == "SI_UNIT")
    {
        e.proof = "Not MASS_UNIT and no DERIVED_UNIT_ELEMENT population; original antecedent FALSE";
        return !has(r, "MASS_UNIT") && Population("DERIVED_UNIT_ELEMENT").empty();
    }
    if (type == "LENGTH_UNIT" || type == "PLANE_ANGLE_UNIT" || type == "SOLID_ANGLE_UNIT")
    {
        for (const auto& unit : sem.UnitContext().siDefinitions)
        {
            if (unit.recordIndex == r)
            {
                e.proof = "dimensions_for_si_unit from actual SI name, tested against seven physical exponents";
                std::array<int, 7> expected{};
                if (type == "LENGTH_UNIT")
                {
                    expected[0] = 1;
                }
                return unit.dimensions == expected;
            }
        }
        throw std::logic_error("Conversion unit must have been explicitly deferred");
    }
    if (type == "MEASURE_WITH_UNIT" || type == "LENGTH_MEASURE_WITH_UNIT")
    {
        e.proof = "Length-family measure value retains base LENGTH_MEASURE type; actual unit has LENGTH_UNIT and length dimensions";
        return Is(Ref(r, "unit_component"), "LENGTH_UNIT");
    }
    if (type == "UNCERTAINTY_MEASURE_WITH_UNIT")
    {
        e.proof = "valid_measure_value: length-family value has REAL base type, numeric value > 0";
        return Rational::Decimal(Field(r, "value_component").children.at(0).text) > Rational();
    }
    if (type == "MECHANICAL_CONTEXT")
    {
        e.proof = "Actual inherited discipline_type = mechanical";
        return Field(r, "discipline_type").text == "mechanical";
    }
    if (type == "DESIGN_CONTEXT")
    {
        e.proof = "Actual inherited life_cycle_stage = design";
        return Field(r, "life_cycle_stage").text == "design";
    }
    throw std::logic_error("Unimplemented active source clause: " + type + "." + label);
}

void Validator::Local()
{
    // Prerequisites precede consumers of derived direction/endpoints. PATH and closure have distinct responsibilities.
    const std::vector<std::string> order = { "DIRECTION", "VECTOR", "AXIS2_PLACEMENT_3D", "LINE", "ORIENTED_EDGE", "PATH", "EDGE_LOOP" };
    std::vector<const Clause*> clauses;
    for (const auto& type : order)
    {
        for (const auto& c : Clauses)
        {
            if (c.type == type)
            {
                clauses.push_back(&c);
            }
        }
    }
    for (const auto& c : Clauses)
    {
        if (std::find(order.begin(), order.end(), c.type) == order.end())
        {
            clauses.push_back(&c);
        }
    }
    for (const auto* clause : clauses)
    {
        const auto population = Population(clause->type);
        RuleEvidence empty;
        empty.name = std::string(clause->type) + "." + clause->label;
        empty.kind = clause->kind;
        empty.schemaLine = clause->line;
        empty.applicability = "Mandatory";
        // The complete population already belongs to the semantic snapshot. A local row identifies
        // its owner, rather than duplicating the entire class in each row (quadratic evidence memory).
        empty.populations[clause->type] = {};
        if (population.empty())
        {
            empty.applicability = "Guarded";
            empty.evaluation = "Evaluated";
            empty.value = Logical::True;
            empty.proof = "Full-file supertype population empty under admitted complete signatures";
            out.evidence.rules.push_back(std::move(empty));
            continue;
        }
        for (const auto r : population)
        {
            Tick(r);
            auto e = empty;
            e.owner = r;
            e.populations[clause->type] = { r };
            out.evidence.rules.push_back(std::move(e));
            auto& active = out.evidence.rules.back();
            const bool value = LocalValue(*clause, r, active);
            active.evaluation = "Evaluated";
            active.value = value ? Logical::True : Logical::False;
            if (!value)
            {
                std::string field;
                const std::string type(clause->type);
                if (type == "DIRECTION")
                {
                    field = "direction_ratios";
                }
                if (type == "VECTOR")
                {
                    field = "magnitude";
                }
                if (type == "AXIS2_PLACEMENT_3D")
                {
                    field = "ref_direction";
                }
                if (type == "LINE")
                {
                    field = "dir";
                }
                if (type == "ORIENTED_EDGE")
                {
                    field = "edge_element";
                }
                if (type == "PATH" || type == "EDGE_LOOP")
                {
                    field = "edge_list";
                }
                if (type == "UNCERTAINTY_MEASURE_WITH_UNIT")
                {
                    field = "value_component";
                }
                Fail("SourceRuleViolation:" + active.name, "D3", r, field, active.proof);
            }
        }
    }
    // Named TYPE predicates apply to values, not merely to entity populations.
    for (const auto r : Population("MEASURE_WITH_UNIT"))
    {
        const auto& value = Field(r, "value_component");
        if (value.text == "LENGTH_MEASURE")
        {
            continue;
        }
        std::vector<std::string> predicates{ "NON_NEGATIVE_LENGTH_MEASURE" };
        if (value.text == "POSITIVE_LENGTH_MEASURE")
        {
            predicates.push_back(value.text);
        }
        for (const auto& type : predicates)
        {
            Tick(r);
            RuleEvidence e;
            e.name = type + ".WR1";
            e.kind = "TYPE_WHERE";
            e.owner = r;
            e.schemaLine = type == "POSITIVE_LENGTH_MEASURE" ? 1485 : 1359;
            const auto number = Rational::Decimal(value.children.at(0).text);
            const bool good = type == "POSITIVE_LENGTH_MEASURE" ? number > Rational() : !(number < Rational());
            e.applicability = "Mandatory";
            e.evaluation = "Evaluated";
            e.value = good ? Logical::True : Logical::False;
            e.proof = "Named length value and inherited TYPE WHERE; base LENGTH_MEASURE itself has no positivity WHERE";
            out.evidence.rules.push_back(e);
            if (!good)
            {
                Fail("SourceRuleViolation:" + e.name, "D3", r, "value_component", e.proof);
            }
        }
    }
    RuleEvidence dimension;
    dimension.name = "DIMENSION_COUNT.WR1";
    dimension.kind = "TYPE_WHERE";
    dimension.schemaLine = 841;
    dimension.owner = Role("unit_context");
    dimension.applicability = "Mandatory";
    dimension.evaluation = "Evaluated";
    dimension.value = Rational::Decimal(Field(*dimension.owner, "coordinate_space_dimension").text) > Rational() ? Logical::True : Logical::False;
    dimension.proof = "Positive INTEGER; 3D capability guard already checked independently by ticket 03";
    out.evidence.rules.push_back(std::move(dimension));
}

RuleEvidence Validator::GlobalValue(const Global& meta)
{
    RuleEvidence e;
    e.name = meta.name;
    e.kind = "GLOBAL";
    e.schemaLine = meta.line;
    e.applicability = "Guarded";
    e.evaluation = "Evaluated";
    e.value = Logical::True;
    for (const auto type : meta.types)
    {
        // This is an explicit *closed-profile* exclusion proof, not the public unknown-type query API.
        const auto* population = sem.Instances(type);
        e.populations[std::string(type)] = population ? *population : std::vector<std::size_t>{};
    }
    const auto setValue = [&](bool good)
    {
        e.value = good ? Logical::True : Logical::False;
    };
    const auto category = Population("PRODUCT_RELATED_PRODUCT_CATEGORY");
    const auto context = Role("definition_context");
    const auto rep = Role("representation");
    const auto ordinaryCategory = [&]()
    {
        return std::all_of(
            category.begin(),
            category.end(),
            [&](auto c)
            {
                return Field(c, "name").text == "part";
            }
        );
    };
    const auto ordinaryContext = [&]()
    {
        return Field(context, "name").text == "part definition";
    };
    switch (meta.id)
    {
        case GlobalRuleId::ApplicationProtocolDefinitionRequired:
        {
            e.applicability = "Mandatory";
            const auto ac = Role("application_context");
            const auto uses = sem.UsedIn(Record(ac).id, "APPLICATION_PROTOCOL_DEFINITION", "application");
            std::size_t matches = 0;
            for (const auto ref : uses)
            {
                const auto apd = src.References()[ref].recordIndex;
                Tick(apd);
                e.witnesses.push_back(apd);
                matches += Field(apd, "application_interpreted_model_schema_name").text ==
                                   "ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies" ?
                               1 :
                               0;
            }
            e.facts["matching_apd_count"] = std::to_string(matches);
            e.proof = "At least one AC with at least one matching reverse APD schema name; HEADER and APD year are not substitutes";
            setValue(matches > 0);
            return e;
        }
        case GlobalRuleId::CompatibleDimension:
        {
            e.applicability = "Mandatory";
            const auto dimension = Field(Role("unit_context"), "coordinate_space_dimension").text;
            bool good = true;
            for (const auto type : { "CARTESIAN_POINT", "DIRECTION" })
            {
                for (const auto r : Population(type))
                {
                    Tick(r);
                    if (sem.UsedByRepresentation(r))
                    {
                        e.witnesses.push_back(r);
                        const auto size = Field(r, type == std::string("DIRECTION") ? "direction_ratios" : "coordinates").children.size();
                        good = (Rational(static_cast<std::int64_t>(size)) == Rational::Decimal(dimension)) && good;
                    }
                }
            }
            e.facts["context_id"] = Record(Role("unit_context")).id;
            e.facts["dimension"] = dimension;
            e.proof =
                "item_in_context uses direct representation membership and RI reverse reachability; all indirectly used points and directions counted";
            setValue(good);
            return e;
        }
        case GlobalRuleId::ConsistentUncertainty:
        {
            e.applicability = "Conditional";
            bool good = true;
            for (const auto c : Population("GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT"))
            {
                std::set<std::string> names;
                for (const auto u : Refs(c, "uncertainty"))
                {
                    Tick(u);
                    e.witnesses.push_back(u);
                    good = names.insert(Field(u, "name").text).second && good;
                }
            }
            e.facts["WR2_UAR_count"] = "0";
            e.facts["WR3_QRI_count"] = "0";
            e.proof = "WR1 compares uncertainty.name, not identity/value/unit; WR2/WR3 populations excluded by complete profile signatures";
            setValue(good);
            return e;
        }
        case GlobalRuleId::RestrictRepresentationForSurfaceCondition:
        {
            e.applicability = "Mandatory";
            std::size_t failures = 0, unknown = 0;
            const std::set<std::string> special = { "visual appearance", "tactile appearance", "contact ratio",
                                                    "hardness",          "treatment result",   "surface texture" };
            for (const auto pdr : Population("PROPERTY_DEFINITION_REPRESENTATION"))
            {
                Tick(pdr);
                const auto definition = Ref(pdr, "definition"), used = Ref(pdr, "used_representation");
                const auto correlation = !special.count(Field(definition, "name").text)             ? Logical::Unknown :
                                         Field(definition, "name").text == Field(used, "name").text ? Logical::True :
                                                                                                      Logical::False;
                failures += QuerySelects(Not(correlation)) ? 1 : 0;
                unknown += correlation == Logical::Unknown ? 1 : 0;
                e.witnesses.push_back(pdr);
                e.witnesses.push_back(definition);
                e.witnesses.push_back(used);
            }
            e.facts["correlation_UNKNOWN_count"] = std::to_string(unknown);
            e.facts["failure_QUERY_count"] = std::to_string(failures);
            e.proof =
                "SDR is in FOR PDR; ordinary PDS.name yields source LOGICAL UNKNOWN; NOT UNKNOWN = UNKNOWN; QUERY selects only TRUE; outer count = 0";
            setValue(failures == 0);
            return e;
        }
        case GlobalRuleId::SubtypeExclusivenessRepresentationItem:
        {
            e.applicability = "Mandatory";
            bool good = true;
            for (const auto r : Population("REPRESENTATION_ITEM"))
            {
                Tick(r);
                const auto types = sem.TypesOf(r);
                std::size_t matches = 0;
                for (const auto type : { "MEASURE_REPRESENTATION_ITEM", "VALUE_REPRESENTATION_ITEM", "COMPOUND_REPRESENTATION_ITEM" })
                {
                    matches += std::find(types.begin(), types.end(), type) != types.end() ? 1 : 0;
                }
                good = matches <= 1 && good;
            }
            e.facts["criterion"] = "3";
            e.proof =
                "type_check_function criterion 3: every full RI TYPEOF intersects the three subtype names in <=1; parameters are defined and sub_names nonempty";
            setValue(good);
            return e;
        }
        case GlobalRuleId::ValidateDependentlyInstantiableEntityDataTypes:
        {
            e.applicability = "Mandatory";
            std::set<std::string> restricted;
            for (const auto& entry : e.populations)
            {
                for (const auto r : entry.second)
                {
                    for (const auto& type : sem.TypesOf(r))
                    {
                        restricted.insert(type);
                    }
                }
            }
            restricted.insert("GENERIC");
            bool good = true;
            for (const auto loop : Population("LOOP"))
            {
                std::vector<std::size_t> pending{ loop };
                std::set<std::size_t> visited;
                bool anchored = false;
                while (!pending.empty() && !anchored)
                {
                    const auto r = pending.back();
                    pending.pop_back();
                    if (!visited.insert(r).second)
                    {
                        continue;
                    }
                    for (const auto index : src.Incoming(Record(r).id))
                    {
                        const auto owner = src.References()[index].recordIndex;
                        Tick(owner);
                        const auto types = sem.TypesOf(owner);
                        const bool subset = std::all_of(
                            types.begin(),
                            types.end(),
                            [&](const auto& t)
                            {
                                return restricted.count(t) > 0;
                            }
                        );
                        if (!subset)
                        {
                            e.witnesses.push_back(loop);
                            e.witnesses.push_back(owner);
                            anchored = true;
                            break;
                        }
                        pending.push_back(owner);
                    }
                }
                good = anchored && good;
            }
            e.facts["restricted_type_count_with_GENERIC"] = std::to_string(restricted.size());
            e.proof =
                "59 FOR populations; LOOP includes EDGE_LOOP. Each input reaches a referring instance whose TYPEOF is not a subset of the restricted type union; cycles alone cannot anchor";
            setValue(good);
            return e;
        }
        case GlobalRuleId::AlternativeSolutionRequiresSolutionDefinition:
        case GlobalRuleId::BreakdownElementRequiresProductDefinition:
        case GlobalRuleId::DesignConstraintRequiresProductDefinition:
        case GlobalRuleId::PlyStockMaterialSelect:
        {
            e.witnesses = category;
            e.proof = "PRPC population may be nonempty; every actual category.name is part, so source special-category/IF-ply filter is empty";
            setValue(ordinaryCategory());
            e.facts["category_name_filter"] = "part only or absent";
            return e;
        }
        case GlobalRuleId::ConstraintDefinitionRequiresConstraintCategory:
        case GlobalRuleId::RestrictAlternativeDefinition:
        case GlobalRuleId::RestrictPartOccurrence:
        case GlobalRuleId::RestrictPartOccurrenceCategory:
        case GlobalRuleId::SolutionDefinitionRequiresSolutionCategory:
        {
            e.witnesses = { context };
            e.proof = "Actual PD context.name is part definition, so constraint/alternative/part occurrence filters are empty";
            setValue(ordinaryContext());
            e.facts["context_name"] = Field(context, "name").text;
            return e;
        }
        case GlobalRuleId::RestrictAssemblyCategory:
        case GlobalRuleId::RestrictCollectionCategory:
        {
            e.witnesses = { context };
            e.proof =
                "ClosedProfileTypeExclusion: PRODUCT_DEFINITION_CONTEXT_ASSOCIATION; original reverse association filter empty (not a direct-context test)";
            e.facts["association_population"] = "0";
            return e;
        }
        case GlobalRuleId::RestrictCentreOfMassRepresentation:
        case GlobalRuleId::RestrictTreatmentResult:
        {
            const auto name = Field(rep, "name").text;
            e.witnesses = { rep };
            e.proof = "REPRESENTATION population nonempty; actual name does not equal the original special trigger";
            e.facts["representation_name"] = name;
            setValue(name != (meta.id == GlobalRuleId::RestrictCentreOfMassRepresentation ? "centre of mass" : "treatment result"));
            return e;
        }
        case GlobalRuleId::ComponentClassForAssemblySelect:
        case GlobalRuleId::DraughtingModelItemsConstraint:
        case GlobalRuleId::ExternalVersionAssignmentsAreValid:
        case GlobalRuleId::MaterialForCoatingLayer:
        case GlobalRuleId::PlibPropertyReferenceRequiresNameScope:
        case GlobalRuleId::PlibPropertyReferenceRequiresVersion:
        case GlobalRuleId::PlyReference:
        case GlobalRuleId::ProductConceptFeatureRequiresCategory:
        case GlobalRuleId::ProductDefinitionReplacementRequiresEffectivityAssignment:
        case GlobalRuleId::RestrictClassificationAssignments:
        case GlobalRuleId::RestrictConceptFeatureOperator:
        case GlobalRuleId::RestrictConfigurationDesignForClassBreakdownAssociation:
        case GlobalRuleId::RestrictConfigurationDesignForDesignConstraint:
        case GlobalRuleId::RestrictGroupRelationshipForClassificationHierarchy:
        case GlobalRuleId::RestrictGroupRelationshipForSpecificationCategory:
        case GlobalRuleId::RestrictLanguageAssignmentPerAttribute:
        case GlobalRuleId::RestrictProductDefinitionsForBaseElement:
        case GlobalRuleId::RestrictProductDefinitionsForCollection:
        case GlobalRuleId::RestrictProductDefinitionsForDefinitionUsage:
        case GlobalRuleId::RestrictProductDefinitionsForDesignConstraintAssociation:
        case GlobalRuleId::RestrictProductDefinitionsForPartDefinitionRelationship:
        case GlobalRuleId::SelectedInstanceUsageRequiresRepresentation:
        case GlobalRuleId::StockMaterialReference:
        case GlobalRuleId::StyledCurve:
        case GlobalRuleId::SubtypeExclusivenessGeometricTolerance:
        case GlobalRuleId::SubtypeMandatoryGeometricTolerance:
        case GlobalRuleId::TextFontUsage:
        case GlobalRuleId::ThicknessLaminateTableComponentSelect:
        {
            // Remaining rules have an empty first FOR population / outer loop. Other FOR sets are still reported.
            e.proof = "Full-file closed-profile type exclusion proves first FOR/outer-loop population empty; other FOR populations retained separately";
            setValue(e.populations.at(std::string(meta.types.front())).empty());
            return e;
        }
    }
    throw std::logic_error("Unimplemented global rule identity");
}
void Validator::GlobalRules()
{
    for (const auto id : GlobalExecutionOrder)
    {
        const auto& rule = GlobalMetadata(id);
        Tick(Role("representation"));
        out.evidence.rules.push_back(GlobalValue(rule));
        const auto& e = out.evidence.rules.back();
        if (e.value != Logical::True)
        {
            Fail("SourceRuleViolation:" + e.name + ".WR1", "D3", Role(rule.failureRole), std::string(rule.failureField), e.proof);
        }
    }
}
} // namespace step_planar
