// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAp203GeneralCatalogData.h"
#include "StepAp203N916CatalogData.h"
#include "StepAp214ConstructionCatalogData.h"
#include "StepAp214MbxCatalogData.h"
#include "StepAp214PointStyleCatalogData.h"
#include "StepAp214PresentationCatalogData.h"
#include "StepAp214SourceRolesCatalogData.h"
#include "StepAp214SurfaceRoleCatalogData.h"
#include "StepAp214ValueCatalogData.h"
#include "StepAp242CatalogData.h"
#include "StepCatalogBuilder.h"
#include "StepCompositeCurveCatalogData.h"
#include "StepDateTimeCatalogData.h"
#include "StepLinearExtrusionCatalogData.h"
#include "StepProductCatalogData.h"
#include "StepRationalSplineCatalogData.h"
#include "StepSurfaceCurveCatalogData.h"
#include "StepVertexLoopCatalogData.h"
#include "StepWireframeCatalogData.h"

#include <array>

namespace step_semantics::catalog_build
{
namespace common
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
constexpr std::string_view SiMass[] = { "MASS_UNIT", "NAMED_UNIT", "SI_UNIT" };
constexpr std::string_view ConversionMass[] = { "CONVERSION_BASED_UNIT", "MASS_UNIT", "NAMED_UNIT" };
constexpr std::string_view ConversionPlaneAngle[] = { "CONVERSION_BASED_UNIT", "NAMED_UNIT", "PLANE_ANGLE_UNIT" };
constexpr std::string_view MeasureRepresentation[] = { "MEASURE_REPRESENTATION_ITEM", "MEASURE_WITH_UNIT", "REPRESENTATION_ITEM" };
constexpr Names ComplexSignatures[] = { Context, ContextWithUncertainty, SiLength, SiPlaneAngle, SiSolidAngle, ConversionLength };
static_assert(std::size(ComplexSignatures) == 6);

constexpr std::string_view UnitDefinitions[] = { "SI_UNIT", "CONVERSION_BASED_UNIT" };
constexpr std::string_view NominalQuantities[] = { "LENGTH_UNIT", "PLANE_ANGLE_UNIT", "SOLID_ANGLE_UNIT", "MASS_UNIT" };
constexpr std::string_view ContextBranches[] = { "PRODUCT_CONTEXT", "PRODUCT_DEFINITION_CONTEXT" };
constexpr Names ExclusiveGroups[] = { UnitDefinitions, NominalQuantities, ContextBranches };
inline constexpr auto Ap214ExclusiveGroups = []
{
    std::array<Names, std::size(ExclusiveGroups) + 2> result;
    std::copy(std::begin(ExclusiveGroups), std::end(ExclusiveGroups), result.begin());
    result[std::size(ExclusiveGroups)] = vertex_loop::LoopKinds;
    result.back() = linear_extrusion::SweptKinds;
    return result;
}();

constexpr std::string_view UnitBranches[] = { "NAMED_UNIT", "DERIVED_UNIT" };
constexpr std::string_view ShellBranches[] = { "OPEN_SHELL", "CLOSED_SHELL" };
constexpr std::string_view CharacterizedBranches[] = { "PRODUCT_DEFINITION", "PRODUCT_DEFINITION_SHAPE" };
constexpr std::string_view RepresentedBranches[] = { "PROPERTY_DEFINITION" };
// Only branches possible under the complete, closed profile are listed here.
constexpr NamedBranches ReferenceSelects[] = { { "UNIT", UnitBranches },
                                               { "SHELL", ShellBranches },
                                               { "CHARACTERIZED_DEFINITION", CharacterizedBranches },
                                               { "REPRESENTED_DEFINITION", RepresentedBranches } };
// AP214E3_2010.exp: these are admitted branches, not the complete EXPRESS SELECTs.
// Keep them in the selected catalog so AP203 cannot acquire presentation types by accident.
constexpr std::string_view PresentationStyles[] = { "SURFACE_STYLE_USAGE", "FILL_AREA_STYLE" };
constexpr std::string_view SurfaceSideStyles[] = { "SURFACE_SIDE_STYLE" };
constexpr std::string_view SurfaceElements[] = { "SURFACE_STYLE_FILL_AREA" };
constexpr std::string_view FillStyles[] = { "FILL_AREA_STYLE_COLOUR" };
constexpr NamedBranches Ap214ReferenceSelects[] = { { "PRESENTATION_STYLE_SELECT", PresentationStyles },
                                                    { "SURFACE_SIDE_STYLE_SELECT", SurfaceSideStyles },
                                                    { "SURFACE_STYLE_ELEMENT_SELECT", SurfaceElements },
                                                    { "FILL_STYLE_SELECT", FillStyles } };
constexpr std::string_view SurfaceSides[] = { "POSITIVE", "NEGATIVE", "BOTH" };
constexpr std::string_view ConicPlacements[] = { "AXIS2_PLACEMENT_3D" };
constexpr std::string_view CurveForms[] = { "POLYLINE_FORM", "CIRCULAR_ARC", "ELLIPTIC_ARC", "PARABOLIC_ARC", "HYPERBOLIC_ARC", "UNSPECIFIED" };
constexpr std::string_view SurfaceForms[] = { "PLANE_SURF",    "CYLINDRICAL_SURF",         "CONICAL_SURF", "SPHERICAL_SURF",
                                              "TOROIDAL_SURF", "SURF_OF_REVOLUTION",       "RULED_SURF",   "GENERALISED_CONE",
                                              "QUADRIC_SURF",  "SURF_OF_LINEAR_EXTRUSION", "UNSPECIFIED" };
constexpr std::string_view KnotTypes[] = { "UNIFORM_KNOTS", "QUASI_UNIFORM_KNOTS", "PIECEWISE_BEZIER_KNOTS", "UNSPECIFIED" };
constexpr std::string_view TrimmingPreferences[] = { "CARTESIAN", "PARAMETER", "UNSPECIFIED" };
constexpr std::string_view TrimmingPoints[] = { "CARTESIAN_POINT" };

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

constexpr Field Text(const char* name, const char* type = "LABEL", bool optional = false)
{
    return { name, type, FieldKind::String, optional };
}
constexpr Field Ref(const char* name, const char* type, bool optional = false)
{
    return { name, type, FieldKind::Reference, optional };
}
constexpr Field Real(const char* name, const char* type = "REAL")
{
    return { name, type, FieldKind::Real };
}
constexpr Field Enum(const char* name, const char* type, bool optional = false)
{
    return { name, type, FieldKind::Enumeration, optional };
}
constexpr Field Bool(const char* name)
{
    return { name, "BOOLEAN", FieldKind::Boolean };
}
constexpr Field Aggregate(const char* name, const char* type, std::size_t min, std::size_t max, Unique unique, FieldKind element = FieldKind::Reference)
{
    Field field{ name, type, FieldKind::Aggregate };
    field.elementKind = element;
    field.minimum = min;
    field.maximum = max;
    field.unique = unique;
    return field;
}
constexpr Field Set(const char* name, const char* type)
{
    return Aggregate(name, type, 1, static_cast<std::size_t>(-1), Unique::Set);
}
constexpr Field List(const char* name, const char* type, FieldKind element)
{
    return Aggregate(name, type, 2, static_cast<std::size_t>(-1), Unique::None, element);
}
constexpr Field ControlGrid()
{
    auto field = List("control_points_list", "CARTESIAN_POINT", FieldKind::Reference);
    // AP214 B_SPLINE_SURFACE declares LIST[2:?] OF LIST[2:?] OF point.
    // Rows and repeated references carry meaning; never flatten or deduplicate them.
    field.nestedAggregates.push_back({ "LIST", 2, {}, false });
    return field;
}
constexpr Field Logical(const char* name)
{
    return { name, "LOGICAL", FieldKind::Logical };
}
} // namespace common
constexpr Recipe Describe(ProfileKind profile)
{
    using namespace common;
    if (profile == ProfileKind::Ap203N916Source)
    {
        return ap203_n916::Apply(Describe(ProfileKind::Ap203e2GeneralSource));
    }
    Recipe recipe;
    if (profile != ProfileKind::Ap203e2Planar && profile != ProfileKind::Ap203e2GeneralSource && profile != ProfileKind::Ap214e3Source &&
        profile != ProfileKind::Ap214MbxSource)
    {
        recipe.issue = Error::InvalidProfile;
        return recipe;
    }
    recipe.exclusiveGroups = ExclusiveGroups;
    recipe.prefixes = Prefixes;
    auto& declarations = recipe.declarations;
    auto& referenceSelects = recipe.referenceSelects;
    auto& enumerations = recipe.enumerations;
    auto& complexSignatures = recipe.complexSignatures;
    auto& measureBranches = recipe.measureBranches;
    auto& derivedSlots = recipe.derivedSlots;

    referenceSelects.assign(std::begin(ReferenceSelects), std::end(ReferenceSelects));
    enumerations.assign(std::begin(Enumerations), std::end(Enumerations));
    complexSignatures.assign(std::begin(ComplexSignatures), std::end(ComplexSignatures));
    measureBranches.assign(std::begin(MeasureBranches), std::end(MeasureBranches));
    // Share the admitted common transport fields/parents; remove the AP203-only legacy contexts below.
    // Registered guard entries are not serialized leaves of this closed profile.
    // Apply protocol-specific removals and DERIVE overrides before freezing indexes and qualified field bindings;
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
    if (profile != ProfileKind::Ap203e2Planar)
    {
        // Compared transport declarations in pinned AP214E3_2010.exp and N2635:
        // DERIVED_UNIT/ELEMENT replace guard-only entries before field indexes are frozen.
        // DERIVED_UNIT.name is DERIVE, never a transmitted name slot. Shared
        // fields do not imply shared complete schema/rule qualification.
        for (const auto name : { "DERIVED_UNIT",
                                 "DERIVED_UNIT_ELEMENT",
                                 "NAMED_UNIT",
                                 "PROPERTY_DEFINITION_REPRESENTATION",
                                 "PROPERTY_DEFINITION",
                                 "REPRESENTATION",
                                 "REPRESENTATION_CONTEXT" })
        {
            if (!RequireTarget(recipe, declarations, name))
            {
                return recipe;
            }
        }
        for (auto& declaration : declarations)
        {
            if (declaration.name == "DERIVED_UNIT")
            {
                declaration.fields = { Set("elements", "DERIVED_UNIT_ELEMENT") };
                declaration.simple = true;
            }
            if (declaration.name == "DERIVED_UNIT_ELEMENT")
            {
                declaration.fields = { Ref("unit", "NAMED_UNIT"), Real("exponent") };
                declaration.simple = true;
            }
            if (declaration.name == "NAMED_UNIT")
            {
                declaration.simple = true;
            }
            // Both artifacts' property metadata uses these exact base declarations.
            // DERIVE names/IDs/descriptions are not extra transmitted parameters.
            if (declaration.name == "PROPERTY_DEFINITION_REPRESENTATION" || declaration.name == "PROPERTY_DEFINITION" ||
                declaration.name == "REPRESENTATION" || declaration.name == "REPRESENTATION_CONTEXT")
            {
                declaration.simple = true;
            }
        }
        complexSignatures.push_back(SiMass);
        complexSignatures.push_back(ConversionMass);
        measureBranches.push_back("MASS_MEASURE");
        measureBranches.push_back("RATIO_MEASURE");
        measureBranches.push_back("POSITIVE_RATIO_MEASURE");
        declarations.push_back({ "MASS_MEASURE_WITH_UNIT", { "MEASURE_WITH_UNIT" }, {}, true });
        if (IsAp214SourceProfile(profile))
        {
            // These two deprecated AP203 types do not occur in the pinned AP214e3 artifact.
            // The separately identified MBx subset retains their N2635 transport
            // descriptors only for a later explicit interoperability proof. Strict
            // checks still reject every such record before binding its fields.
            if (profile != ProfileKind::Ap214MbxSource)
            {
                if (!RequireTarget(recipe, declarations, "MECHANICAL_CONTEXT") || !RequireTarget(recipe, declarations, "DESIGN_CONTEXT"))
                {
                    return recipe;
                }
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
            }
            // AP214E3_2010.exp, CONVERSION_BASED_UNIT DERIVE: dimensions is transmitted as '*'.
            // The source value stays a marker; this descriptor must not invent the factor unit's dimensions reference.
            derivedSlots.push_back({ "CONVERSION_BASED_UNIT", "NAMED_UNIT", "dimensions" });
        }
        // Compared AP214E3_2010.exp/N2635 presentation entities and inherited explicit fields.
        // All values/reference directions stay source-owned. Registering these transport
        // declarations does not evaluate the MDGPR/style/colour WHERE clauses or global RULEs.
        declarations.insert(
            declarations.end(),
            {
                { "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION", { "REPRESENTATION" }, {}, true },
                { "STYLED_ITEM",
                  { "REPRESENTATION_ITEM" },
                  { Set("styles", "PRESENTATION_STYLE_ASSIGNMENT"), Ref("item", "REPRESENTATION_ITEM") },
                  true },
                { "PRESENTATION_STYLE_ASSIGNMENT", { "FOUNDED_ITEM" }, { Set("styles", "PRESENTATION_STYLE_SELECT") }, true },
                { "SURFACE_STYLE_USAGE", { "FOUNDED_ITEM" }, { Enum("side", "SURFACE_SIDE"), Ref("style", "SURFACE_SIDE_STYLE_SELECT") }, true },
                { "SURFACE_SIDE_STYLE",
                  { "FOUNDED_ITEM" },
                  { Text("name"), Aggregate("styles", "SURFACE_STYLE_ELEMENT_SELECT", 1, 7, Unique::Set) },
                  true },
                { "SURFACE_STYLE_FILL_AREA", { "FOUNDED_ITEM" }, { Ref("fill_area", "FILL_AREA_STYLE") }, true },
                { "FILL_AREA_STYLE", { "FOUNDED_ITEM" }, { Text("name"), Set("fill_styles", "FILL_STYLE_SELECT") }, true },
                { "FILL_AREA_STYLE_COLOUR", {}, { Text("name"), Ref("fill_colour", "COLOUR") }, true },
                { "COLOUR", {}, {} },
                { "COLOUR_SPECIFICATION", { "COLOUR" }, { Text("name") } },
                { "COLOUR_RGB", { "COLOUR_SPECIFICATION" }, { Real("red"), Real("green"), Real("blue") }, true },
                { "REPRESENTATION_RELATIONSHIP",
                  {},
                  { Text("name"), Text("description", "TEXT", true), Ref("rep_1", "REPRESENTATION"), Ref("rep_2", "REPRESENTATION") } },
                { "SHAPE_REPRESENTATION_RELATIONSHIP", { "REPRESENTATION_RELATIONSHIP" }, {}, true },
            }
        );
        // Either source root may be a placement-only SHAPE_REPRESENTATION linked
        // to the BRep representation; admitting its fields does not apply a transform.
        SetSimple(recipe, "SHAPE_REPRESENTATION");
        referenceSelects.insert(referenceSelects.end(), std::begin(Ap214ReferenceSelects), std::end(Ap214ReferenceSelects));
        enumerations.push_back({ "SURFACE_SIDE", SurfaceSides });
        // AP214E3_2010.exp/N2635 CONIC.position is AXIS2_PLACEMENT. Restricting
        // that SELECT to 3D is this project's subset, not an EXPRESS restriction.
        // Axes/radii/angles remain original source values. The B-spline fields
        // and enum literals below were also compared with both fixed artifacts.
        declarations.insert(
            declarations.end(),
            {
                { "CONIC", { "CURVE" }, { Ref("position", "AXIS2_PLACEMENT") } },
                { "CIRCLE", { "CONIC" }, { Real("radius", "POSITIVE_LENGTH_MEASURE") }, true },
                { "ELLIPSE", { "CONIC" }, { Real("semi_axis_1", "POSITIVE_LENGTH_MEASURE"), Real("semi_axis_2", "POSITIVE_LENGTH_MEASURE") }, true },
                { "CYLINDRICAL_SURFACE", { "ELEMENTARY_SURFACE" }, { Real("radius", "POSITIVE_LENGTH_MEASURE") }, true },
                { "CONICAL_SURFACE", { "ELEMENTARY_SURFACE" }, { Real("radius", "LENGTH_MEASURE"), Real("semi_angle", "PLANE_ANGLE_MEASURE") }, true },
            }
        );
        referenceSelects.push_back({ "AXIS2_PLACEMENT", ConicPlacements });
        declarations.insert(
            declarations.end(),
            {
                { "BOUNDED_CURVE", { "CURVE" }, {} },
                { "B_SPLINE_CURVE",
                  { "BOUNDED_CURVE" },
                  { { "degree", "INTEGER", FieldKind::Integer },
                    List("control_points_list", "CARTESIAN_POINT", FieldKind::Reference),
                    Enum("curve_form", "B_SPLINE_CURVE_FORM"),
                    Logical("closed_curve"),
                    Logical("self_intersect") } },
                { "B_SPLINE_CURVE_WITH_KNOTS",
                  { "B_SPLINE_CURVE" },
                  { List("knot_multiplicities", "INTEGER", FieldKind::Integer),
                    List("knots", "PARAMETER_VALUE", FieldKind::Real),
                    Enum("knot_spec", "KNOT_TYPE") },
                  true },
                { "BOUNDED_SURFACE", { "SURFACE" }, {} },
                { "B_SPLINE_SURFACE",
                  { "BOUNDED_SURFACE" },
                  { { "u_degree", "INTEGER", FieldKind::Integer },
                    { "v_degree", "INTEGER", FieldKind::Integer },
                    ControlGrid(),
                    Enum("surface_form", "B_SPLINE_SURFACE_FORM"),
                    Logical("u_closed"),
                    Logical("v_closed"),
                    Logical("self_intersect") } },
                { "B_SPLINE_SURFACE_WITH_KNOTS",
                  { "B_SPLINE_SURFACE" },
                  { List("u_multiplicities", "INTEGER", FieldKind::Integer),
                    List("v_multiplicities", "INTEGER", FieldKind::Integer),
                    List("u_knots", "PARAMETER_VALUE", FieldKind::Real),
                    List("v_knots", "PARAMETER_VALUE", FieldKind::Real),
                    Enum("knot_spec", "KNOT_TYPE") },
                  true },
            }
        );
        enumerations.push_back({ "B_SPLINE_CURVE_FORM", CurveForms });
        enumerations.push_back({ "B_SPLINE_SURFACE_FORM", SurfaceForms });
        enumerations.push_back({ "KNOT_TYPE", KnotTypes });
        // AP214E3_2010.exp/N2635: MRI inherits RI before MWU, so simple mapping is
        // name/value_component/unit_component. Keep both source parent roles.
        declarations.push_back({ "MEASURE_REPRESENTATION_ITEM", { "REPRESENTATION_ITEM", "MEASURE_WITH_UNIT" }, {}, true });
        // External mapping transmits each parent's local fields in its own facet;
        // the empty MRI facet does not duplicate or reorder those source values.
        complexSignatures.push_back(MeasureRepresentation);
        declarations.push_back({ "DESCRIPTIVE_REPRESENTATION_ITEM", { "REPRESENTATION_ITEM" }, { Text("description", "TEXT") }, true });
    }
    if (profile == ProfileKind::Ap203e2GeneralSource)
    {
        ap203_general::AddAp203GeneralDeclarations(recipe);
    }
    if (IsAp214SourceProfile(profile))
    {
        date_time::AddDateTimeDeclarations(recipe, profile);
        ap214_presentation::AddAp214RenderingDeclarations(recipe);
        ap214_presentation::AddAp214CurveStyleDeclarations(recipe);
    }
    if (profile == ProfileKind::Ap214MbxSource)
    {
        ap214_mbx::AddAp214MbxDeclarations(recipe);
    }
    if (profile != ProfileKind::Ap203e2Planar)
    {
        rational_spline::AddRationalSplineDeclarations(recipe);
        // Pinned N2635 / AP214E3_2010.exp TOROIDAL_SURFACE: two positive
        // lengths, with the inherited elementary-surface placement. The
        // major>minor WHERE belongs to the distinct CSG TORUS entity, so
        // source horn/spindle surfaces remain legal source representations.
        // Append after existing entries to preserve their type identities.
        declarations.push_back(
            { "TOROIDAL_SURFACE",
              { "ELEMENTARY_SURFACE" },
              { Real("major_radius", "POSITIVE_LENGTH_MEASURE"), Real("minor_radius", "POSITIVE_LENGTH_MEASURE") },
              true }
        );
        // N2635 / AP214E3_2010.exp TRIMMED_CURVE has a mixed SELECT: the
        // reference table records its entity branch; TrimmingSelect validates
        // the typed PARAMETER_VALUE REAL branch without treating it as a measure.
        auto trim = Aggregate("trim_1", "TRIMMING_SELECT", 1, 2, Unique::Set, FieldKind::TrimmingSelect);
        auto trim2 = trim;
        trim2.name = "trim_2";
        declarations.push_back(
            { "TRIMMED_CURVE",
              { "BOUNDED_CURVE" },
              { Ref("basis_curve", "CURVE"), trim, trim2, Bool("sense_agreement"), Enum("master_representation", "TRIMMING_PREFERENCE") },
              true }
        );
        referenceSelects.push_back({ "TRIMMING_SELECT", TrimmingPoints });
        enumerations.push_back({ "TRIMMING_PREFERENCE", TrimmingPreferences });
    }
    if (IsAp214SourceProfile(profile))
    {
        // Fixed AP214E3_2010.exp (71ab140f...), PLANE_ANGLE_MEASURE_WITH_UNIT:
        // inherited value/unit slots; its nominal-unit WR1 is distinct from
        // generic MWU's valid_units dimension test. Append to retain prior IDs.
        declarations.push_back({ "PLANE_ANGLE_MEASURE_WITH_UNIT", { "MEASURE_WITH_UNIT" }, {}, true });
        measureBranches.push_back("PLANE_ANGLE_MEASURE");
        complexSignatures.push_back(ConversionPlaneAngle);
        // Append new declarations after existing entries to retain every prior
        // type identity in both fixed AP214 catalogs.
        ap214_presentation::AddAp214PredefinedColourDeclarations(recipe);
    }
    if (profile == ProfileKind::Ap214MbxSource)
    {
        // Borrowed transport remains an error until the explicit property-only
        // interoperability proof succeeds. Append to keep all prior type IDs.
        ap214_mbx::AddBorrowedRealItemTransport(recipe);
    }
    return recipe;
}
template <ProfileKind Profile>
constexpr Recipe ProfileRecipe()
{
    if constexpr (Profile == ProfileKind::Ap242e2Source)
    {
        return ap242::Apply(ProfileRecipe<ProfileKind::Ap214MbxSource>());
    }
    auto recipe = Describe(Profile);
    if constexpr (Profile != ProfileKind::Ap203e2Planar)
    {
        // Finalize after protocol-specific recipes (including N916's copied
        // recipe and MBx's foreign declarations), so no prior type ID moves.
        wireframe::AppendDeclarations(recipe);
    }
    if constexpr (IsAp214SourceProfile(Profile))
    {
        ap214_source_roles::AppendDeclarations(recipe, Profile == ProfileKind::Ap214MbxSource);
        ap214_construction::AppendDeclarations(recipe);
        ap214_value::AppendDeclarations(recipe);
        ap214_point_style::AppendDeclarations(recipe);
    }
    if constexpr (Profile != ProfileKind::Ap203e2Planar)
    {
        composite_curve::AppendDeclarations(recipe); // Append after every existing profile-specific identity.
    }
    if constexpr (IsAp214SourceProfile(Profile))
    {
        ap214_surface_role::AppendDeclarations(recipe);
    }
    if constexpr (Profile == ProfileKind::Ap203N916Source)
    {
        // Fixed modified N916 (020b4d25...), SPHERICAL_SURFACE: inherited
        // elementary placement and a positive length radius. Append after all
        // existing identities; this admits source data, not native sphere UVs
        // or the AP214 bounded-surface representation role.
        recipe.declarations.push_back({ "SPHERICAL_SURFACE", { "ELEMENTARY_SURFACE" }, { common::Real("radius", "POSITIVE_LENGTH_MEASURE") }, true });
    }
    if constexpr (HasDeclaredProductStructureProfile(Profile))
    {
        // Preserve every existing catalog identity and complex mapping index.
        // Product-structure declarations are appended after all prior overlays.
        products::AddDeclarations(recipe, Profile);
    }
    if constexpr (IsAp214SourceProfile(Profile))
    {
        surface_curve::AppendDeclarations(recipe); // Preserve all existing profile IDs and complex signature order.
        vertex_loop::AppendDeclarations(recipe);
        linear_extrusion::AppendDeclarations(recipe);
        recipe.exclusiveGroups = common::Ap214ExclusiveGroups;
    }
    return recipe;
}
} // namespace step_semantics::catalog_build
