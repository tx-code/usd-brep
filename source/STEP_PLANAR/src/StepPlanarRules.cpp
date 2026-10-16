// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepPlanarInternal.h"

#include <algorithm>
#include <set>

namespace step_planar
{
std::array<Rational, 7> Validator::UnitDimensions(std::size_t r, std::vector<std::size_t>& witnesses) const
{
    for (const auto& unit : sem.UnitContext().siDefinitions)
    {
        if (unit.recordIndex == r)
        {
            std::array<Rational, 7> result{};
            for (std::size_t i = 0; i < result.size(); ++i)
            {
                result[i] = Rational(unit.dimensions[i]);
            }
            return result;
        }
    }
    if (!Is(r, "CONVERSION_BASED_UNIT"))
    {
        throw std::domain_error("UnsupportedNamedUnitDimensions");
    }
    const auto dims = Ref(r, "dimensions");
    for (const auto witness : { r, dims })
    {
        if (std::find(witnesses.begin(), witnesses.end(), witness) == witnesses.end())
        {
            witnesses.push_back(witness);
        }
    }
    constexpr std::array names{ "length_exponent",
                                "mass_exponent",
                                "time_exponent",
                                "electric_current_exponent",
                                "thermodynamic_temperature_exponent",
                                "amount_of_substance_exponent",
                                "luminous_intensity_exponent" };
    std::array<Rational, 7> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        result[i] = Rational::Decimal(Field(dims, names[i]).text);
    }
    return result;
}

bool Validator::LocalValue(const Clause& c, std::size_t r, RuleEvidence& e)
{
    const std::string type(c.type);
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
    switch (c.id)
    {
        case LocalRuleId::ProductDefinitionFormationUr1:
        case LocalRuleId::ProductDefinitionShapeUr1:
        {
            e.proof = "Singleton full-file supertype population; pairwise UNIQUE has no distinct pair";
            e.populations[type] = Population(type);
            return e.populations[type].size() == 1;
        }
        case LocalRuleId::ApplicationContextContextElements:
        case LocalRuleId::RepresentationContextRepresentationsInContext:
        {
            const bool ac = c.id == LocalRuleId::ApplicationContextContextElements;
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
        // Complete admitted signatures exclude the metadata assignment types counted by these WHEREs.
        case LocalRuleId::ApplicationContextWr1:
        case LocalRuleId::ApplicationContextWr2:
        case LocalRuleId::ProductCategoryWr1:
        case LocalRuleId::ProductDefinitionWr1:
        case LocalRuleId::PropertyDefinitionWr1:
        case LocalRuleId::PropertyDefinitionRepresentationWr1:
        case LocalRuleId::PropertyDefinitionRepresentationWr2:
        case LocalRuleId::RepresentationWr1:
        case LocalRuleId::RepresentationWr2:
        {
            e.proof = "ClosedProfileTypeExclusion: ID_ATTRIBUTE/DESCRIPTION_ATTRIBUTE/NAME_ATTRIBUTE; inverse count 0 <= 1";
            e.facts["assignment_count"] = "0";
            e.facts["derived_getter"] = "INDETERMINATE_VALUE(?) from zero assignments, not an empty string";
            return true;
        }
        case LocalRuleId::RepresentationItemWr1:
        {
            e.witnesses = { Role("representation") };
            e.facts["using_representation_count"] = sem.UsedByRepresentation(r) ? "1" : "0";
            e.proof = "using_representations: full RI-filtered transitive uses plus REPRESENTATION.items, management users excluded";
            return sem.UsedByRepresentation(r);
        }
        case LocalRuleId::GeometricRepresentationItemWr1:
        {
            e.witnesses = { Role("representation"), Role("unit_context") };
            e.proof = "Single admitted representation; every collected using representation has a GRC context";
            return Is(Role("unit_context"), "GEOMETRIC_REPRESENTATION_CONTEXT");
        }
        case LocalRuleId::DirectionWr1:
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
        case LocalRuleId::VectorWr1:
        {
            e.proof = "Source magnitude >= 0; zero is allowed by this WHERE";
            return !(Rational::Decimal(Field(r, "magnitude").text) < Rational());
        }
        case LocalRuleId::Axis2Placement3dWr1:
        case LocalRuleId::Axis2Placement3dWr2:
        case LocalRuleId::Axis2Placement3dWr3:
        case LocalRuleId::Axis2Placement3dWr4:
        {
            const auto present = [&](const char* name)
            {
                return Field(r, name).kind != step_source::ValueKind::Unset;
            };
            e.proof = "Original optional predicates; WR4 requires cross magnitude > 0 only when both fields exist";
            if (c.id == LocalRuleId::Axis2Placement3dWr1)
            {
                return Field(Ref(r, "location"), "coordinates").children.size() == 3;
            }
            if (c.id == LocalRuleId::Axis2Placement3dWr2)
            {
                return !present("axis") || Field(Ref(r, "axis"), "direction_ratios").children.size() == 3;
            }
            if (c.id == LocalRuleId::Axis2Placement3dWr3)
            {
                return !present("ref_direction") || Field(Ref(r, "ref_direction"), "direction_ratios").children.size() == 3;
            }
            if (c.id == LocalRuleId::Axis2Placement3dWr4)
            {
                return !present("axis") || !present("ref_direction") ||
                       Squared(Cross(Direction(Ref(r, "axis")), Direction(Ref(r, "ref_direction")))) > Rational();
            }

            break;
        }
        case LocalRuleId::LineWr1:
        {
            e.proof = "Dimension of dir.orientation ratios equals number of pnt coordinates";
            return Field(Ref(Ref(r, "dir"), "orientation"), "direction_ratios").children.size() ==
                   Field(Ref(r, "pnt"), "coordinates").children.size();
        }
        case LocalRuleId::OrientedEdgeWr1:
        {
            e.proof = "edge_element TYPEOF excludes ORIENTED_EDGE";
            return !Is(Ref(r, "edge_element"), "ORIENTED_EDGE");
        }
        case LocalRuleId::EdgeLoopWr1:
        case LocalRuleId::PathWr1:
        {
            const auto edges = Refs(r, "edge_list");
            e.witnesses = edges;
            if (c.id == LocalRuleId::EdgeLoopWr1)
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
        case LocalRuleId::FaceWr1:
        case LocalRuleId::FaceWr2:
        {
            std::size_t outer = 0, poly = 0;
            for (const auto bound : Refs(r, "bounds"))
            {
                outer += Is(bound, "FACE_OUTER_BOUND") ? 1 : 0;
                poly += has(Ref(bound, "bound"), "POLY_LOOP") ? 1 : 0;
            }
            e.proof = c.id == LocalRuleId::FaceWr1 ? "mixed_loop_type_set: all actual bounds use EDGE_LOOP" :
                                                     "Explicit FACE_OUTER_BOUND count <= 1; not the profile's count = 1";
            e.facts["outer_count"] = std::to_string(outer);
            e.facts["poly_loop_count"] = std::to_string(poly);
            return c.id == LocalRuleId::FaceWr1 ? (poly == 0 || poly == Refs(r, "bounds").size()) : outer <= 1;
        }
        case LocalRuleId::FaceSurfaceWr1:
        {
            e.proof = "Direct PLANE TYPEOF excludes ORIENTED_SURFACE";
            return !has(Ref(r, "face_geometry"), "ORIENTED_SURFACE");
        }
        case LocalRuleId::AdvancedFaceWr1:
        case LocalRuleId::AdvancedFaceWr2:
        case LocalRuleId::AdvancedFaceWr3:
        case LocalRuleId::AdvancedFaceWr4:
        case LocalRuleId::AdvancedFaceWr5:
        case LocalRuleId::AdvancedFaceWr6:
        case LocalRuleId::AdvancedFaceWr7:
        case LocalRuleId::AdvancedFaceWr8:
        case LocalRuleId::AdvancedFaceWr9:
        case LocalRuleId::AdvancedFaceWr10:
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
            if (c.id == LocalRuleId::AdvancedFaceWr1)
            {
                return countTypes(geometry, { "ELEMENTARY_SURFACE", "B_SPLINE_SURFACE", "SWEPT_SURFACE" }) == 1;
            }
            if (c.id == LocalRuleId::AdvancedFaceWr2)
            {
                return forall(
                    oe,
                    [&](auto x)
                    {
                        return Is(Ref(x, "edge_element"), "EDGE_CURVE");
                    }
                );
            }
            if (c.id == LocalRuleId::AdvancedFaceWr3)
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
            if (c.id == LocalRuleId::AdvancedFaceWr4)
            {
                return forall(
                    oe,
                    [&](auto x)
                    {
                        const auto ends = Ends(x);
                        return Is(ends.first, "VERTEX_POINT") && Is(ends.second, "VERTEX_POINT") &&
                               Is(Ref(ends.first, "vertex_geometry"), "CARTESIAN_POINT") &&
                               Is(Ref(ends.second, "vertex_geometry"), "CARTESIAN_POINT");
                    }
                );
            }
            if (c.id == LocalRuleId::AdvancedFaceWr5)
            {
                return forall(
                    Refs(r, "bounds"),
                    [&](auto b)
                    {
                        return !has(Ref(b, "bound"), "ORIENTED_PATH");
                    }
                );
            }
            if (c.id == LocalRuleId::AdvancedFaceWr6 || c.id == LocalRuleId::AdvancedFaceWr10)
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
            if (c.id == LocalRuleId::AdvancedFaceWr7)
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
            if (c.id == LocalRuleId::AdvancedFaceWr8)
            {
                return forall(
                    Refs(r, "bounds"),
                    [&](auto b)
                    {
                        return countTypes(Ref(b, "bound"), { "EDGE_LOOP", "VERTEX_LOOP" }) == 1;
                    }
                );
            }
            if (c.id == LocalRuleId::AdvancedFaceWr9)
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

            break;
        }
        case LocalRuleId::AdvancedBrepShapeRepresentationWr1:
        case LocalRuleId::AdvancedBrepShapeRepresentationWr2:
        case LocalRuleId::AdvancedBrepShapeRepresentationWr3:
        case LocalRuleId::AdvancedBrepShapeRepresentationWr4:
        case LocalRuleId::AdvancedBrepShapeRepresentationWr5:
        case LocalRuleId::AdvancedBrepShapeRepresentationWr6:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr1:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr2:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr3:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr4:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr5:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr6:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr7:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr8:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr9:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr10:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr11:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr12:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr13:
        case LocalRuleId::ManifoldSurfaceShapeRepresentationWr14:
        {
            const bool solid = c.id == LocalRuleId::AdvancedBrepShapeRepresentationWr1 || c.id == LocalRuleId::AdvancedBrepShapeRepresentationWr2 ||
                               c.id == LocalRuleId::AdvancedBrepShapeRepresentationWr3 || c.id == LocalRuleId::AdvancedBrepShapeRepresentationWr4 ||
                               c.id == LocalRuleId::AdvancedBrepShapeRepresentationWr5 || c.id == LocalRuleId::AdvancedBrepShapeRepresentationWr6;
            const auto items = Refs(r, "items");
            const auto shell = Role("shell");
            e.witnesses = items;
            e.witnesses.push_back(shell);
            e.proof =
                "Complete admitted items and shell face population; mapped/void/oriented wrapper populations excluded by exact profile signatures";
            if (c.id == LocalRuleId::AdvancedBrepShapeRepresentationWr1 || c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr1)
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
            if (c.id == LocalRuleId::AdvancedBrepShapeRepresentationWr2 || c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr2)
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
            if (c.id == LocalRuleId::AdvancedBrepShapeRepresentationWr3)
            {
                return forall(
                    Refs(shell, "cfs_faces"),
                    [&](auto x)
                    {
                        return Is(x, "ADVANCED_FACE");
                    }
                );
            }
            if (c.id == LocalRuleId::AdvancedBrepShapeRepresentationWr4)
            {
                return !has(shell, "ORIENTED_CLOSED_SHELL");
            }
            if (c.id == LocalRuleId::AdvancedBrepShapeRepresentationWr5 || c.id == LocalRuleId::AdvancedBrepShapeRepresentationWr6)
            {
                return forall(
                    items,
                    [&](auto x)
                    {
                        return !has(x, c.id == LocalRuleId::AdvancedBrepShapeRepresentationWr5 ? "BREP_WITH_VOIDS" : "MAPPED_ITEM");
                    }
                );
            }
            if (c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr3)
            {
                return forall(
                    items,
                    [&](auto x)
                    {
                        return !has(x, "MAPPED_ITEM");
                    }
                );
            }
            if (c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr4)
            {
                return countTypes(shell, { "OPEN_SHELL", "ORIENTED_CLOSED_SHELL", "CLOSED_SHELL" }) == 1;
            }
            if (c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr5)
            {
                return forall(
                    Refs(shell, "cfs_faces"),
                    [&](auto x)
                    {
                        return Is(x, "FACE_SURFACE");
                    }
                );
            }
            if (c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr6 || c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr7 ||
                c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr8 || c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr9 ||
                c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr10 || c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr11 ||
                c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr12 || c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr13 ||
                c.id == LocalRuleId::ManifoldSurfaceShapeRepresentationWr14)
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

            break;
        }
        case LocalRuleId::ShellBasedSurfaceModelWr1:
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
        case LocalRuleId::ProductDefinitionShapeWr1:
        {
            e.proof = "definition is PRODUCT_DEFINITION, in characterized_product_definition SELECT";
            return Is(Ref(r, "definition"), "PRODUCT_DEFINITION");
        }
        case LocalRuleId::ShapeDefinitionRepresentationWr1:
        case LocalRuleId::ShapeDefinitionRepresentationWr2:
        {
            e.proof = "Original direct-PDS first disjunct or shape representation type";
            return c.id == LocalRuleId::ShapeDefinitionRepresentationWr1 ? Is(Ref(r, "definition"), "PRODUCT_DEFINITION_SHAPE") :
                                                                           Is(Ref(r, "used_representation"), "SHAPE_REPRESENTATION");
        }
        case LocalRuleId::SiUnitWr1:
        {
            e.proof = "Not MASS_UNIT and no DERIVED_UNIT_ELEMENT population; original antecedent FALSE";
            return !has(r, "MASS_UNIT") && Population("DERIVED_UNIT_ELEMENT").empty();
        }
        case LocalRuleId::LengthUnitWr1:
        case LocalRuleId::PlaneAngleUnitWr1:
        case LocalRuleId::SolidAngleUnitWr1:
        {
            for (const auto& unit : sem.UnitContext().siDefinitions)
            {
                if (unit.recordIndex == r)
                {
                    e.proof = "dimensions_for_si_unit from actual SI name, tested against seven physical exponents";
                    std::array<int, 7> expected{};
                    if (c.id == LocalRuleId::LengthUnitWr1)
                    {
                        expected[0] = 1;
                    }
                    return unit.dimensions == expected;
                }
            }
            std::array<Rational, 7> expected{};
            if (c.id == LocalRuleId::LengthUnitWr1)
            {
                expected[0] = Rational(1);
            }
            e.proof = "Explicit conversion dimensions must match the nominal quantity's seven physical exponents";
            return UnitDimensions(r, e.witnesses) == expected;
        }
        case LocalRuleId::LengthMeasureWithUnitWr1:
        case LocalRuleId::MeasureWithUnitWr1:
        {
            e.proof = "Length-family measure value retains base LENGTH_MEASURE type; actual unit has LENGTH_UNIT and length dimensions";
            std::array<Rational, 7> length{};
            length[0] = Rational(1);
            return Is(Ref(r, "unit_component"), "LENGTH_UNIT") && UnitDimensions(Ref(r, "unit_component"), e.witnesses) == length;
        }
        case LocalRuleId::UncertaintyMeasureWithUnitWr1:
        {
            e.proof = "valid_measure_value: length-family value has REAL base type, numeric value > 0";
            return Rational::Decimal(Field(r, "value_component").children.at(0).text) > Rational();
        }
        case LocalRuleId::MechanicalContextWr1:
        {
            e.proof = "Actual inherited discipline_type = mechanical";
            return Field(r, "discipline_type").text == "mechanical";
        }
        case LocalRuleId::DesignContextWr1:
        {
            e.proof = "Actual inherited life_cycle_stage = design";
            return Field(r, "life_cycle_stage").text == "design";
        }
        case LocalRuleId::ConversionBasedUnitWr1:
        {
            const auto factor = Ref(r, "conversion_factor"), base = Ref(factor, "unit_component");
            const auto declared = UnitDimensions(r, e.witnesses);
            e.witnesses.push_back(factor);
            e.witnesses.push_back(base);
            const auto actual = UnitDimensions(base, e.witnesses);
            e.proof = "Explicit conversion dimensions equal derive_dimensional_exponents(unit_component), by seven values; no factor recursion";
            std::array<Rational, 7> length{};
            length[0] = Rational(1);
            if (Is(r, "LENGTH_UNIT") && declared != length)
            {
                e.facts["nominal_length_rule"] = "FALSE";
            }
            for (std::size_t i = 0; i < declared.size(); ++i)
            {
                if (i)
                {
                    e.facts["declared_dimensions"] += ",";
                    e.facts["factor_unit_dimensions"] += ",";
                }
                e.facts["declared_dimensions"] += std::to_string(declared[i].Numerator()) + "/" + std::to_string(declared[i].Denominator());
                e.facts["factor_unit_dimensions"] += std::to_string(actual[i].Numerator()) + "/" + std::to_string(actual[i].Denominator());
            }
            return declared == actual;
        }
        // These clauses are retained through their empty-population guards, never an invented active pass.
        case LocalRuleId::DerivedUnitWr1:
        case LocalRuleId::DerivedUnitWr2:
        case LocalRuleId::FoundedItemWr1:
        case LocalRuleId::FoundedItemWr2:
        case LocalRuleId::MassUnitWr1:
        // TYPE values have their own drivers and cannot be evaluated as entity clauses.
        case LocalRuleId::NonNegativeLengthMeasureWr1:
        case LocalRuleId::PositiveLengthMeasureWr1:
        case LocalRuleId::DimensionCountWr1:
            break;
    }
    throw std::logic_error("Unimplemented active source clause: " + type + "." + c.label);
}

bool Validator::TypeValue(LocalRuleId id, const Value& value) const
{
    const auto number = Rational::Decimal(value.text);
    switch (id)
    {
        case LocalRuleId::NonNegativeLengthMeasureWr1:
            return !(number < Rational());
        case LocalRuleId::PositiveLengthMeasureWr1:
        case LocalRuleId::DimensionCountWr1:
            return number > Rational();
        default:
            throw std::logic_error("Entity identity used as a TYPE predicate");
    }
}
void Validator::Local()
{
    for (const auto id : EntityLocalExecutionOrder)
    {
        const auto& clause = LocalMetadata(id);
        const auto population = Population(clause.type);
        RuleEvidence empty;
        empty.name = std::string(clause.type) + "." + clause.label;
        empty.kind = clause.kind;
        empty.schemaLine = clause.line;
        empty.applicability = "Mandatory";
        // The complete population already belongs to the semantic snapshot. A local row identifies
        // its owner, rather than duplicating the entire class in each row (quadratic evidence memory).
        empty.populations[clause.type] = {};
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
            e.populations[clause.type] = { r };
            out.evidence.rules.push_back(std::move(e));
            auto& active = out.evidence.rules.back();
            const bool value = LocalValue(clause, r, active);
            active.evaluation = "Evaluated";
            active.value = value ? Logical::True : Logical::False;
            if (!value)
            {
                const bool conversion = clause.id == LocalRuleId::ConversionBasedUnitWr1;
                const auto key = conversion && active.facts.contains("nominal_length_rule") ?
                                     "SourceRuleViolation:LENGTH_UNIT.WR1+CONVERSION_BASED_UNIT.WR1" :
                                     "SourceRuleViolation:" + active.name;
                Fail(
                    key,
                    "D3",
                    r,
                    std::string(clause.failureField),
                    active.proof,
                    Status::Rejected,
                    { conversion ? active.witnesses : std::vector<std::size_t>{},
                      conversion ? active.facts["factor_unit_dimensions"] : "",
                      conversion ? active.facts["declared_dimensions"] : "" }
                );
            }
        }
    }
    // Named TYPE predicates apply per value, after all entity clauses. Keep successful evaluation before insertion.
    for (const auto r : Population("MEASURE_WITH_UNIT"))
    {
        const auto& value = Field(r, "value_component");
        for (const auto id : LengthPredicates(value.text))
        {
            const auto& predicate = LocalMetadata(id);
            Tick(r);
            RuleEvidence e;
            e.name = std::string(predicate.type) + "." + predicate.label;
            e.kind = predicate.kind;
            e.owner = r;
            e.schemaLine = predicate.line;
            const bool good = TypeValue(id, value.children.at(0));
            e.applicability = "Mandatory";
            e.evaluation = "Evaluated";
            e.value = good ? Logical::True : Logical::False;
            e.proof = "Named length value and inherited TYPE WHERE; base LENGTH_MEASURE itself has no positivity WHERE";
            out.evidence.rules.push_back(e);
            if (!good)
            {
                Fail("SourceRuleViolation:" + e.name, "D3", r, std::string(predicate.failureField), e.proof);
            }
        }
    }
    // This final context-value predicate has no Tick or source-violation Fail in the established contract.
    const auto& predicate = LocalMetadata(DimensionPredicate);
    RuleEvidence dimension;
    dimension.name = std::string(predicate.type) + "." + predicate.label;
    dimension.kind = predicate.kind;
    dimension.schemaLine = predicate.line;
    dimension.owner = Role("unit_context");
    dimension.applicability = "Mandatory";
    dimension.evaluation = "Evaluated";
    dimension.value = TypeValue(DimensionPredicate, Field(*dimension.owner, "coordinate_space_dimension")) ? Logical::True : Logical::False;
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
