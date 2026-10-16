// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
// Fixed declaration metadata extracted from N2635 c68dd022; no runtime schema dependency.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
namespace step_planar
{
// Private identities are independent of registry storage and are never serialized as integers.
enum class LocalRuleId : std::uint16_t
{
    AdvancedBrepShapeRepresentationWr1 = 1,
    AdvancedBrepShapeRepresentationWr2 = 2,
    AdvancedBrepShapeRepresentationWr3 = 3,
    AdvancedBrepShapeRepresentationWr4 = 4,
    AdvancedBrepShapeRepresentationWr5 = 5,
    AdvancedBrepShapeRepresentationWr6 = 6,
    AdvancedFaceWr1 = 7,
    AdvancedFaceWr2 = 8,
    AdvancedFaceWr3 = 9,
    AdvancedFaceWr4 = 10,
    AdvancedFaceWr5 = 11,
    AdvancedFaceWr6 = 12,
    AdvancedFaceWr7 = 13,
    AdvancedFaceWr8 = 14,
    AdvancedFaceWr9 = 15,
    AdvancedFaceWr10 = 16,
    ApplicationContextContextElements = 17,
    ApplicationContextWr1 = 18,
    ApplicationContextWr2 = 19,
    Axis2Placement3dWr1 = 20,
    Axis2Placement3dWr2 = 21,
    Axis2Placement3dWr3 = 22,
    Axis2Placement3dWr4 = 23,
    ConversionBasedUnitWr1 = 24,
    DerivedUnitWr1 = 25,
    DerivedUnitWr2 = 26,
    DesignContextWr1 = 27,
    DirectionWr1 = 28,
    EdgeLoopWr1 = 29,
    FaceWr1 = 30,
    FaceWr2 = 31,
    FaceSurfaceWr1 = 32,
    FoundedItemWr1 = 33,
    FoundedItemWr2 = 34,
    GeometricRepresentationItemWr1 = 35,
    LengthMeasureWithUnitWr1 = 36,
    LengthUnitWr1 = 37,
    LineWr1 = 38,
    ManifoldSurfaceShapeRepresentationWr1 = 39,
    ManifoldSurfaceShapeRepresentationWr2 = 40,
    ManifoldSurfaceShapeRepresentationWr3 = 41,
    ManifoldSurfaceShapeRepresentationWr4 = 42,
    ManifoldSurfaceShapeRepresentationWr5 = 43,
    ManifoldSurfaceShapeRepresentationWr6 = 44,
    ManifoldSurfaceShapeRepresentationWr7 = 45,
    ManifoldSurfaceShapeRepresentationWr8 = 46,
    ManifoldSurfaceShapeRepresentationWr9 = 47,
    ManifoldSurfaceShapeRepresentationWr10 = 48,
    ManifoldSurfaceShapeRepresentationWr11 = 49,
    ManifoldSurfaceShapeRepresentationWr12 = 50,
    ManifoldSurfaceShapeRepresentationWr13 = 51,
    ManifoldSurfaceShapeRepresentationWr14 = 52,
    MassUnitWr1 = 53,
    MeasureWithUnitWr1 = 54,
    MechanicalContextWr1 = 55,
    OrientedEdgeWr1 = 56,
    PathWr1 = 57,
    PlaneAngleUnitWr1 = 58,
    ProductCategoryWr1 = 59,
    ProductDefinitionWr1 = 60,
    ProductDefinitionFormationUr1 = 61,
    ProductDefinitionShapeUr1 = 62,
    ProductDefinitionShapeWr1 = 63,
    PropertyDefinitionWr1 = 64,
    PropertyDefinitionRepresentationWr1 = 65,
    PropertyDefinitionRepresentationWr2 = 66,
    RepresentationWr1 = 67,
    RepresentationWr2 = 68,
    RepresentationContextRepresentationsInContext = 69,
    RepresentationItemWr1 = 70,
    ShapeDefinitionRepresentationWr1 = 71,
    ShapeDefinitionRepresentationWr2 = 72,
    ShellBasedSurfaceModelWr1 = 73,
    SiUnitWr1 = 74,
    SolidAngleUnitWr1 = 75,
    UncertaintyMeasureWithUnitWr1 = 76,
    VectorWr1 = 77,
    NonNegativeLengthMeasureWr1 = 78,
    PositiveLengthMeasureWr1 = 79,
    DimensionCountWr1 = 80,
};
enum class LocalSubject
{
    EntityPopulation,
    MeasureValue,
    ContextAttribute
};
struct Clause
{
    LocalRuleId id;
    const char* type;
    const char* kind;
    const char* label;
    std::size_t line;
    LocalSubject subject;
    std::string_view failureField;
};
inline constexpr Clause LocalRules[] = {
    { LocalRuleId::AdvancedBrepShapeRepresentationWr1, "ADVANCED_BREP_SHAPE_REPRESENTATION", "WHERE", "WR1", 2125, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedBrepShapeRepresentationWr2, "ADVANCED_BREP_SHAPE_REPRESENTATION", "WHERE", "WR2", 2127, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedBrepShapeRepresentationWr3, "ADVANCED_BREP_SHAPE_REPRESENTATION", "WHERE", "WR3", 2129, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedBrepShapeRepresentationWr4, "ADVANCED_BREP_SHAPE_REPRESENTATION", "WHERE", "WR4", 2134, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedBrepShapeRepresentationWr5, "ADVANCED_BREP_SHAPE_REPRESENTATION", "WHERE", "WR5", 2137, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedBrepShapeRepresentationWr6, "ADVANCED_BREP_SHAPE_REPRESENTATION", "WHERE", "WR6", 2141, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedFaceWr1, "ADVANCED_FACE", "WHERE", "WR1", 2150, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedFaceWr2, "ADVANCED_FACE", "WHERE", "WR2", 2151, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedFaceWr3, "ADVANCED_FACE", "WHERE", "WR3", 2155, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedFaceWr4, "ADVANCED_FACE", "WHERE", "WR4", 2159, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedFaceWr5, "ADVANCED_FACE", "WHERE", "WR5", 2163, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedFaceWr6, "ADVANCED_FACE", "WHERE", "WR6", 2166, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedFaceWr7, "ADVANCED_FACE", "WHERE", "WR7", 2167, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedFaceWr8, "ADVANCED_FACE", "WHERE", "WR8", 2170, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedFaceWr9, "ADVANCED_FACE", "WHERE", "WR9", 2172, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::AdvancedFaceWr10, "ADVANCED_FACE", "WHERE", "WR10", 2177, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::ApplicationContextContextElements, "APPLICATION_CONTEXT", "INVERSE", "context_elements", 2399, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::ApplicationContextWr1, "APPLICATION_CONTEXT", "WHERE", "WR1", 2401, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::ApplicationContextWr2, "APPLICATION_CONTEXT", "WHERE", "WR2", 2402, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::Axis2Placement3dWr1, "AXIS2_PLACEMENT_3D", "WHERE", "WR1", 2751, LocalSubject::EntityPopulation, "ref_direction" },
    { LocalRuleId::Axis2Placement3dWr2, "AXIS2_PLACEMENT_3D", "WHERE", "WR2", 2752, LocalSubject::EntityPopulation, "ref_direction" },
    { LocalRuleId::Axis2Placement3dWr3, "AXIS2_PLACEMENT_3D", "WHERE", "WR3", 2753, LocalSubject::EntityPopulation, "ref_direction" },
    { LocalRuleId::Axis2Placement3dWr4, "AXIS2_PLACEMENT_3D", "WHERE", "WR4", 2754, LocalSubject::EntityPopulation, "ref_direction" },
    { LocalRuleId::ConversionBasedUnitWr1, "CONVERSION_BASED_UNIT", "WHERE", "WR1", 3995, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::DerivedUnitWr1, "DERIVED_UNIT", "WHERE", "WR1", 4391, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::DerivedUnitWr2, "DERIVED_UNIT", "WHERE", "WR2", 4392, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::DesignContextWr1, "DESIGN_CONTEXT", "WHERE", "WR1", 4418, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::DirectionWr1, "DIRECTION", "WHERE", "WR1", 4678, LocalSubject::EntityPopulation, "direction_ratios" },
    { LocalRuleId::EdgeLoopWr1, "EDGE_LOOP", "WHERE", "WR1", 5324, LocalSubject::EntityPopulation, "edge_list" },
    { LocalRuleId::FaceWr1, "FACE", "WHERE", "WR1", 5946, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::FaceWr2, "FACE", "WHERE", "WR2", 5947, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::FaceSurfaceWr1, "FACE_SURFACE", "WHERE", "WR1", 5975, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::FoundedItemWr1, "FOUNDED_ITEM", "WHERE", "WR1", 6135, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::FoundedItemWr2, "FOUNDED_ITEM", "WHERE", "WR2", 6136, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::GeometricRepresentationItemWr1, "GEOMETRIC_REPRESENTATION_ITEM", "WHERE", "WR1", 6290, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::LengthMeasureWithUnitWr1, "LENGTH_MEASURE_WITH_UNIT", "WHERE", "WR1", 6824, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::LengthUnitWr1, "LENGTH_UNIT", "WHERE", "WR1", 6831, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::LineWr1, "LINE", "WHERE", "WR1", 6894, LocalSubject::EntityPopulation, "dir" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr1,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR1",
      7168,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr2,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR2",
      7172,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr3,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR3",
      7175,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr4,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR4",
      7184,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr5,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR5",
      7192,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr6,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR6",
      7199,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr7,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR7",
      7208,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr8,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR8",
      7219,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr9,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR9",
      7231,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr10,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR10",
      7253,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr11,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR11",
      7265,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr12,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR12",
      7281,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr13,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR13",
      7303,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::ManifoldSurfaceShapeRepresentationWr14,
      "MANIFOLD_SURFACE_SHAPE_REPRESENTATION",
      "WHERE",
      "WR14",
      7315,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::MassUnitWr1, "MASS_UNIT", "WHERE", "WR1", 7352, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::MeasureWithUnitWr1, "MEASURE_WITH_UNIT", "WHERE", "WR1", 7419, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::MechanicalContextWr1, "MECHANICAL_CONTEXT", "WHERE", "WR1", 7426, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::OrientedEdgeWr1, "ORIENTED_EDGE", "WHERE", "WR1", 8728, LocalSubject::EntityPopulation, "edge_element" },
    { LocalRuleId::PathWr1, "PATH", "WHERE", "WR1", 8852, LocalSubject::EntityPopulation, "edge_list" },
    { LocalRuleId::PlaneAngleUnitWr1, "PLANE_ANGLE_UNIT", "WHERE", "WR1", 9048, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::ProductCategoryWr1, "PRODUCT_CATEGORY", "WHERE", "WR1", 9474, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::ProductDefinitionWr1, "PRODUCT_DEFINITION", "WHERE", "WR1", 9574, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::ProductDefinitionFormationUr1, "PRODUCT_DEFINITION_FORMATION", "UNIQUE", "UR1", 9615, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::ProductDefinitionShapeUr1, "PRODUCT_DEFINITION_SHAPE", "UNIQUE", "UR1", 9668, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::ProductDefinitionShapeWr1, "PRODUCT_DEFINITION_SHAPE", "WHERE", "WR1", 9670, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::PropertyDefinitionWr1, "PROPERTY_DEFINITION", "WHERE", "WR1", 9789, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::PropertyDefinitionRepresentationWr1,
      "PROPERTY_DEFINITION_REPRESENTATION",
      "WHERE",
      "WR1",
      9808,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::PropertyDefinitionRepresentationWr2,
      "PROPERTY_DEFINITION_REPRESENTATION",
      "WHERE",
      "WR2",
      9809,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::RepresentationWr1, "REPRESENTATION", "WHERE", "WR1", 10012, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::RepresentationWr2, "REPRESENTATION", "WHERE", "WR2", 10015, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::RepresentationContextRepresentationsInContext,
      "REPRESENTATION_CONTEXT",
      "INVERSE",
      "representations_in_context",
      10025,
      LocalSubject::EntityPopulation,
      "" },
    { LocalRuleId::RepresentationItemWr1, "REPRESENTATION_ITEM", "WHERE", "WR1", 10033, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::ShapeDefinitionRepresentationWr1, "SHAPE_DEFINITION_REPRESENTATION", "WHERE", "WR1", 10418, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::ShapeDefinitionRepresentationWr2, "SHAPE_DEFINITION_REPRESENTATION", "WHERE", "WR2", 10419, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::ShellBasedSurfaceModelWr1, "SHELL_BASED_SURFACE_MODEL", "WHERE", "WR1", 10465, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::SiUnitWr1, "SI_UNIT", "WHERE", "WR1", 10709, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::SolidAngleUnitWr1, "SOLID_ANGLE_UNIT", "WHERE", "WR1", 10758, LocalSubject::EntityPopulation, "" },
    { LocalRuleId::UncertaintyMeasureWithUnitWr1,
      "UNCERTAINTY_MEASURE_WITH_UNIT",
      "WHERE",
      "WR1",
      12100,
      LocalSubject::EntityPopulation,
      "value_component" },
    { LocalRuleId::VectorWr1, "VECTOR", "WHERE", "WR1", 12209, LocalSubject::EntityPopulation, "magnitude" },
    { LocalRuleId::NonNegativeLengthMeasureWr1,
      "NON_NEGATIVE_LENGTH_MEASURE",
      "TYPE_WHERE",
      "WR1",
      1359,
      LocalSubject::MeasureValue,
      "value_component" },
    { LocalRuleId::PositiveLengthMeasureWr1, "POSITIVE_LENGTH_MEASURE", "TYPE_WHERE", "WR1", 1485, LocalSubject::MeasureValue, "value_component" },
    { LocalRuleId::DimensionCountWr1, "DIMENSION_COUNT", "TYPE_WHERE", "WR1", 841, LocalSubject::ContextAttribute, "" },
};
// This list preserves the original prerequisite and report order; it is not registry storage order.
inline constexpr std::array EntityLocalExecutionOrder = {
    LocalRuleId::DirectionWr1,
    LocalRuleId::VectorWr1,
    LocalRuleId::Axis2Placement3dWr1,
    LocalRuleId::Axis2Placement3dWr2,
    LocalRuleId::Axis2Placement3dWr3,
    LocalRuleId::Axis2Placement3dWr4,
    LocalRuleId::LineWr1,
    LocalRuleId::OrientedEdgeWr1,
    LocalRuleId::PathWr1,
    LocalRuleId::EdgeLoopWr1,
    LocalRuleId::AdvancedBrepShapeRepresentationWr1,
    LocalRuleId::AdvancedBrepShapeRepresentationWr2,
    LocalRuleId::AdvancedBrepShapeRepresentationWr3,
    LocalRuleId::AdvancedBrepShapeRepresentationWr4,
    LocalRuleId::AdvancedBrepShapeRepresentationWr5,
    LocalRuleId::AdvancedBrepShapeRepresentationWr6,
    LocalRuleId::AdvancedFaceWr1,
    LocalRuleId::AdvancedFaceWr2,
    LocalRuleId::AdvancedFaceWr3,
    LocalRuleId::AdvancedFaceWr4,
    LocalRuleId::AdvancedFaceWr5,
    LocalRuleId::AdvancedFaceWr6,
    LocalRuleId::AdvancedFaceWr7,
    LocalRuleId::AdvancedFaceWr8,
    LocalRuleId::AdvancedFaceWr9,
    LocalRuleId::AdvancedFaceWr10,
    LocalRuleId::ApplicationContextContextElements,
    LocalRuleId::ApplicationContextWr1,
    LocalRuleId::ApplicationContextWr2,
    LocalRuleId::ConversionBasedUnitWr1,
    LocalRuleId::DerivedUnitWr1,
    LocalRuleId::DerivedUnitWr2,
    LocalRuleId::DesignContextWr1,
    LocalRuleId::FaceWr1,
    LocalRuleId::FaceWr2,
    LocalRuleId::FaceSurfaceWr1,
    LocalRuleId::FoundedItemWr1,
    LocalRuleId::FoundedItemWr2,
    LocalRuleId::GeometricRepresentationItemWr1,
    LocalRuleId::LengthMeasureWithUnitWr1,
    LocalRuleId::LengthUnitWr1,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr1,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr2,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr3,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr4,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr5,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr6,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr7,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr8,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr9,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr10,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr11,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr12,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr13,
    LocalRuleId::ManifoldSurfaceShapeRepresentationWr14,
    LocalRuleId::MassUnitWr1,
    LocalRuleId::MeasureWithUnitWr1,
    LocalRuleId::MechanicalContextWr1,
    LocalRuleId::PlaneAngleUnitWr1,
    LocalRuleId::ProductCategoryWr1,
    LocalRuleId::ProductDefinitionWr1,
    LocalRuleId::ProductDefinitionFormationUr1,
    LocalRuleId::ProductDefinitionShapeUr1,
    LocalRuleId::ProductDefinitionShapeWr1,
    LocalRuleId::PropertyDefinitionWr1,
    LocalRuleId::PropertyDefinitionRepresentationWr1,
    LocalRuleId::PropertyDefinitionRepresentationWr2,
    LocalRuleId::RepresentationWr1,
    LocalRuleId::RepresentationWr2,
    LocalRuleId::RepresentationContextRepresentationsInContext,
    LocalRuleId::RepresentationItemWr1,
    LocalRuleId::ShapeDefinitionRepresentationWr1,
    LocalRuleId::ShapeDefinitionRepresentationWr2,
    LocalRuleId::ShellBasedSurfaceModelWr1,
    LocalRuleId::SiUnitWr1,
    LocalRuleId::SolidAngleUnitWr1,
    LocalRuleId::UncertaintyMeasureWithUnitWr1,
};
inline constexpr std::array NonNegativeLengthPredicates = { LocalRuleId::NonNegativeLengthMeasureWr1 };
inline constexpr std::array PositiveLengthPredicates = {
    LocalRuleId::NonNegativeLengthMeasureWr1,
    LocalRuleId::PositiveLengthMeasureWr1,
};
struct LengthPredicatePlan
{
    std::string_view sourceType;
    std::span<const LocalRuleId> predicates;
};
inline constexpr LengthPredicatePlan LengthPredicatePlans[] = {
    { "LENGTH_MEASURE", {} },
    { "NON_NEGATIVE_LENGTH_MEASURE", NonNegativeLengthPredicates },
    { "POSITIVE_LENGTH_MEASURE", PositiveLengthPredicates },
};
inline constexpr auto DimensionPredicate = LocalRuleId::DimensionCountWr1;
constexpr const Clause& LocalMetadata(LocalRuleId id)
{
    for (const auto& clause : LocalRules)
    {
        if (clause.id == id)
        {
            return clause;
        }
    }
    throw std::logic_error("Unregistered local rule identity");
}
constexpr std::span<const LocalRuleId> LengthPredicates(std::string_view sourceType)
{
    for (const auto& plan : LengthPredicatePlans)
    {
        if (plan.sourceType == sourceType)
        {
            return plan.predicates;
        }
    }
    throw std::logic_error("Unadmitted length measure type");
}
consteval bool LocalRegistryIsValid()
{
    std::size_t entities = 0, measures = 0, contexts = 0;
    for (std::size_t i = 0; i < std::size(LocalRules); ++i)
    {
        const auto& clause = LocalRules[i];
        const std::string_view owner(clause.type), kind(clause.kind), label(clause.label);
        if (owner.empty() || label.empty() || !clause.line)
        {
            return false;
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            const auto& previous = LocalRules[j];
            if (previous.id == clause.id || (owner == previous.type && kind == previous.kind && label == previous.label))
            {
                return false;
            }
        }
        std::size_t occurrences = 0;
        for (const auto id : EntityLocalExecutionOrder)
        {
            occurrences += id == clause.id ? 1 : 0;
        }
        if (clause.subject == LocalSubject::EntityPopulation)
        {
            ++entities;
            if (occurrences != 1 || (kind != "WHERE" && kind != "UNIQUE" && kind != "INVERSE"))
            {
                return false;
            }
        }
        else
        {
            if (occurrences || kind != "TYPE_WHERE")
            {
                return false;
            }
            if (clause.subject == LocalSubject::MeasureValue)
            {
                ++measures;
                bool used = false;
                for (const auto& plan : LengthPredicatePlans)
                {
                    for (const auto id : plan.predicates)
                    {
                        used = used || id == clause.id;
                    }
                }
                if (!used)
                {
                    return false;
                }
            }
            else if (clause.subject == LocalSubject::ContextAttribute)
            {
                ++contexts;
                if (clause.id != DimensionPredicate)
                {
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
    }
    for (const auto id : EntityLocalExecutionOrder)
    {
        if (LocalMetadata(id).subject != LocalSubject::EntityPopulation)
        {
            return false;
        }
    }
    for (std::size_t i = 0; i < std::size(LengthPredicatePlans); ++i)
    {
        const auto& plan = LengthPredicatePlans[i];
        for (std::size_t j = 0; j < i; ++j)
        {
            if (LengthPredicatePlans[j].sourceType == plan.sourceType)
            {
                return false;
            }
        }
        for (std::size_t p = 0; p < plan.predicates.size(); ++p)
        {
            if (LocalMetadata(plan.predicates[p]).subject != LocalSubject::MeasureValue)
            {
                return false;
            }
            for (std::size_t q = 0; q < p; ++q)
            {
                if (plan.predicates[p] == plan.predicates[q])
                {
                    return false;
                }
            }
        }
    }
    const auto base = LengthPredicates("LENGTH_MEASURE");
    const auto nonnegative = LengthPredicates("NON_NEGATIVE_LENGTH_MEASURE");
    const auto positive = LengthPredicates("POSITIVE_LENGTH_MEASURE");
    return entities == 77 && measures == 2 && contexts == 1 && base.empty() && nonnegative.size() == 1 &&
           nonnegative[0] == LocalRuleId::NonNegativeLengthMeasureWr1 && positive.size() == 2 &&
           positive[0] == LocalRuleId::NonNegativeLengthMeasureWr1 && positive[1] == LocalRuleId::PositiveLengthMeasureWr1 &&
           LocalMetadata(DimensionPredicate).subject == LocalSubject::ContextAttribute;
}
static_assert(std::size(LocalRules) == 80 && EntityLocalExecutionOrder.size() == 77 && std::size(LengthPredicatePlans) == 3);
static_assert(LocalRegistryIsValid(), "Local identities, declarations and subject-specific execution plans must be complete and unique");

// Internal identities are independent of registry storage and are not serialized as integers.
enum class GlobalRuleId : std::uint16_t
{
    AlternativeSolutionRequiresSolutionDefinition = 1,
    ApplicationProtocolDefinitionRequired = 2,
    BreakdownElementRequiresProductDefinition = 3,
    CompatibleDimension = 4,
    ComponentClassForAssemblySelect = 5,
    ConsistentUncertainty = 6,
    ConstraintDefinitionRequiresConstraintCategory = 7,
    DesignConstraintRequiresProductDefinition = 8,
    DraughtingModelItemsConstraint = 9,
    ExternalVersionAssignmentsAreValid = 10,
    MaterialForCoatingLayer = 11,
    PlibPropertyReferenceRequiresNameScope = 12,
    PlibPropertyReferenceRequiresVersion = 13,
    PlyReference = 14,
    PlyStockMaterialSelect = 15,
    ProductConceptFeatureRequiresCategory = 16,
    ProductDefinitionReplacementRequiresEffectivityAssignment = 17,
    RestrictAlternativeDefinition = 18,
    RestrictAssemblyCategory = 19,
    RestrictCentreOfMassRepresentation = 20,
    RestrictClassificationAssignments = 21,
    RestrictCollectionCategory = 22,
    RestrictConceptFeatureOperator = 23,
    RestrictConfigurationDesignForClassBreakdownAssociation = 24,
    RestrictConfigurationDesignForDesignConstraint = 25,
    RestrictGroupRelationshipForClassificationHierarchy = 26,
    RestrictGroupRelationshipForSpecificationCategory = 27,
    RestrictLanguageAssignmentPerAttribute = 28,
    RestrictPartOccurrence = 29,
    RestrictPartOccurrenceCategory = 30,
    RestrictProductDefinitionsForBaseElement = 31,
    RestrictProductDefinitionsForCollection = 32,
    RestrictProductDefinitionsForDefinitionUsage = 33,
    RestrictProductDefinitionsForDesignConstraintAssociation = 34,
    RestrictProductDefinitionsForPartDefinitionRelationship = 35,
    RestrictRepresentationForSurfaceCondition = 36,
    RestrictTreatmentResult = 37,
    SelectedInstanceUsageRequiresRepresentation = 38,
    SolutionDefinitionRequiresSolutionCategory = 39,
    StockMaterialReference = 40,
    StyledCurve = 41,
    SubtypeExclusivenessGeometricTolerance = 42,
    SubtypeExclusivenessRepresentationItem = 43,
    SubtypeMandatoryGeometricTolerance = 44,
    TextFontUsage = 45,
    ThicknessLaminateTableComponentSelect = 46,
    ValidateDependentlyInstantiableEntityDataTypes = 47,
};
struct Global
{
    GlobalRuleId id;
    std::string_view name;
    std::size_t line;
    std::span<const std::string_view> types;
    std::string_view failureRole = "representation";
    std::string_view failureField;
};
inline constexpr std::string_view ForProductDefinitionFormation[] = {
    "PRODUCT_DEFINITION_FORMATION",
};
inline constexpr std::string_view ForApplicationContext[] = {
    "APPLICATION_CONTEXT",
};
inline constexpr std::string_view ForCompatibleDimension[] = {
    "CARTESIAN_POINT",
    "DIRECTION",
    "REPRESENTATION_CONTEXT",
    "GEOMETRIC_REPRESENTATION_CONTEXT",
};
inline constexpr std::string_view ForComponentClassForAssemblySelect[] = {
    "COMPOSITE_ASSEMBLY_SEQUENCE_DEFINITION",
    "NEXT_ASSEMBLY_USAGE_OCCURRENCE",
    "PRODUCT_RELATED_PRODUCT_CATEGORY",
};
inline constexpr std::string_view ForConsistentUncertainty[] = {
    "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT",
    "QUALIFIED_REPRESENTATION_ITEM",
    "UNCERTAINTY_ASSIGNED_REPRESENTATION",
};
inline constexpr std::string_view ForProductDefinition[] = {
    "PRODUCT_DEFINITION",
};
inline constexpr std::string_view ForDraughtingModel[] = {
    "DRAUGHTING_MODEL",
};
inline constexpr std::string_view ForAppliedExternalIdentificationAssignment[] = {
    "APPLIED_EXTERNAL_IDENTIFICATION_ASSIGNMENT",
};
inline constexpr std::string_view ForShapeAspect[] = {
    "SHAPE_ASPECT",
};
inline constexpr std::string_view ForExternallyDefinedGeneralProperty[] = {
    "EXTERNALLY_DEFINED_GENERAL_PROPERTY",
};
inline constexpr std::string_view ForPlyReference[] = {
    "PLY_LAMINATE_SEQUENCE_DEFINITION",
    "NEXT_ASSEMBLY_USAGE_OCCURRENCE",
    "PRODUCT_RELATED_PRODUCT_CATEGORY",
};
inline constexpr std::string_view ForPlyStockMaterialSelect[] = {
    "PRODUCT_RELATED_PRODUCT_CATEGORY",
    "MAKE_FROM_USAGE_OPTION",
};
inline constexpr std::string_view ForProductConceptFeature[] = {
    "PRODUCT_CONCEPT_FEATURE",
};
inline constexpr std::string_view ForProductDefinitionRelationship[] = {
    "PRODUCT_DEFINITION_RELATIONSHIP",
};
inline constexpr std::string_view ForRepresentation[] = {
    "REPRESENTATION",
};
inline constexpr std::string_view ForAppliedClassificationAssignment[] = {
    "APPLIED_CLASSIFICATION_ASSIGNMENT",
};
inline constexpr std::string_view ForConceptFeatureOperator[] = {
    "CONCEPT_FEATURE_OPERATOR",
};
inline constexpr std::string_view ForConfigurationDesign[] = {
    "CONFIGURATION_DESIGN",
};
inline constexpr std::string_view ForGroupRelationship[] = {
    "GROUP_RELATIONSHIP",
};
inline constexpr std::string_view ForAttributeLanguageAssignment[] = {
    "ATTRIBUTE_LANGUAGE_ASSIGNMENT",
};
inline constexpr std::string_view ForPropertyDefinitionRepresentation[] = {
    "PROPERTY_DEFINITION_REPRESENTATION",
};
inline constexpr std::string_view ForAssemblyComponentUsage[] = {
    "ASSEMBLY_COMPONENT_USAGE",
};
inline constexpr std::string_view ForStockMaterialReference[] = {
    "PERCENTAGE_PLY_DEFINITION",
    "MAKE_FROM_USAGE_OPTION",
    "PRODUCT_RELATED_PRODUCT_CATEGORY",
};
inline constexpr std::string_view ForStyledItem[] = {
    "STYLED_ITEM",
};
inline constexpr std::string_view ForGeometricTolerance[] = {
    "GEOMETRIC_TOLERANCE",
};
inline constexpr std::string_view ForRepresentationItem[] = {
    "REPRESENTATION_ITEM",
};
inline constexpr std::string_view ForTextFontUsage[] = {
    "EXTERNALLY_DEFINED_TEXT_FONT",
    "PRE_DEFINED_TEXT_FONT",
};
inline constexpr std::string_view ForThicknessLaminateTableComponentSelect[] = {
    "THICKNESS_LAMINATE_DEFINITION",
    "NEXT_ASSEMBLY_USAGE_OCCURRENCE",
    "PRODUCT_RELATED_PRODUCT_CATEGORY",
};
inline constexpr std::string_view ForValidateDependentlyInstantiableEntityDataTypes[] = {
    "ACTION_METHOD_ROLE",
    "ANNOTATION_TEXT",
    "ATTRIBUTE_VALUE_ROLE",
    "AUXILIARY_GEOMETRIC_REPRESENTATION_ITEM",
    "BINARY_NUMERIC_EXPRESSION",
    "BOOLEAN_EXPRESSION",
    "BOUNDED_CURVE",
    "BOUNDED_SURFACE",
    "CARTESIAN_TRANSFORMATION_OPERATOR",
    "COMPARISON_EXPRESSION",
    "CONCEPT_FEATURE_RELATIONSHIP",
    "CONCEPT_FEATURE_RELATIONSHIP_WITH_CONDITION",
    "CONNECTED_EDGE_SET",
    "DOCUMENT_USAGE_CONSTRAINT",
    "EDGE_BLENDED_SOLID",
    "EFFECTIVITY_CONTEXT_ROLE",
    "EVENT_OCCURRENCE_ROLE",
    "EXPLICIT_PROCEDURAL_REPRESENTATION_ITEM_RELATIONSHIP",
    "EXPRESSION",
    "FOUNDED_ITEM",
    "GENERIC_EXPRESSION",
    "GENERIC_VARIABLE",
    "INDIRECTLY_SELECTED_ELEMENTS",
    "INTERVAL_EXPRESSION",
    "LITERAL_NUMBER",
    "LOCAL_TIME",
    "LOOP",
    "MODIFIED_SOLID_WITH_PLACED_CONFIGURATION",
    "MULTIPLE_ARITY_BOOLEAN_EXPRESSION",
    "MULTIPLE_ARITY_GENERIC_EXPRESSION",
    "MULTIPLE_ARITY_NUMERIC_EXPRESSION",
    "NUMERIC_EXPRESSION",
    "ONE_DIRECTION_REPEAT_FACTOR",
    "ORIENTED_OPEN_SHELL",
    "ORIENTED_PATH",
    "POSITIONED_SKETCH",
    "PROCEDURAL_REPRESENTATION",
    "PROCEDURAL_REPRESENTATION_SEQUENCE",
    "PRODUCT_DEFINITION_CONTEXT_ROLE",
    "PRODUCT_DEFINITION_EFFECTIVITY",
    "RUNOUT_ZONE_ORIENTATION",
    "SIMPLE_BOOLEAN_EXPRESSION",
    "SIMPLE_GENERIC_EXPRESSION",
    "SIMPLE_NUMERIC_EXPRESSION",
    "SOLID_WITH_DEPRESSION",
    "SOLID_WITH_HOLE",
    "SOLID_WITH_POCKET",
    "SOLID_WITH_PROTRUSION",
    "SOLID_WITH_SHAPE_ELEMENT_PATTERN",
    "SOLID_WITH_SLOT",
    "SWEPT_AREA_SOLID",
    "SYMBOL_TARGET",
    "TOLERANCE_ZONE_FORM",
    "TWO_DIRECTION_REPEAT_FACTOR",
    "UNARY_GENERIC_EXPRESSION",
    "UNARY_NUMERIC_EXPRESSION",
    "USER_SELECTED_ELEMENTS",
    "VARIATIONAL_REPRESENTATION_ITEM",
    "VIEW_VOLUME",
};

inline constexpr Global Globals[] = {
    { GlobalRuleId::AlternativeSolutionRequiresSolutionDefinition,
      "alternative_solution_requires_solution_definition",
      12358,
      ForProductDefinitionFormation },
    { GlobalRuleId::ApplicationProtocolDefinitionRequired,
      "application_protocol_definition_required",
      12371,
      ForApplicationContext,
      "application_protocol",
      "application_interpreted_model_schema_name" },
    { GlobalRuleId::BreakdownElementRequiresProductDefinition, "breakdown_element_requires_product_definition", 12381, ForProductDefinitionFormation },
    { GlobalRuleId::CompatibleDimension, "compatible_dimension", 12398, ForCompatibleDimension },
    { GlobalRuleId::ComponentClassForAssemblySelect, "component_class_for_assembly_select", 12410, ForComponentClassForAssemblySelect },
    { GlobalRuleId::ConsistentUncertainty, "consistent_uncertainty", 12457, ForConsistentUncertainty, "unit_context", "uncertainty" },
    { GlobalRuleId::ConstraintDefinitionRequiresConstraintCategory,
      "constraint_definition_requires_constraint_category",
      12475,
      ForProductDefinition },
    { GlobalRuleId::DesignConstraintRequiresProductDefinition, "design_constraint_requires_product_definition", 12488, ForProductDefinitionFormation },
    { GlobalRuleId::DraughtingModelItemsConstraint, "draughting_model_items_constraint", 12499, ForDraughtingModel },
    { GlobalRuleId::ExternalVersionAssignmentsAreValid, "external_version_assignments_are_valid", 12518, ForAppliedExternalIdentificationAssignment },
    { GlobalRuleId::MaterialForCoatingLayer, "material_for_coating_layer", 12525, ForShapeAspect },
    { GlobalRuleId::PlibPropertyReferenceRequiresNameScope,
      "plib_property_reference_requires_name_scope",
      12537,
      ForExternallyDefinedGeneralProperty },
    { GlobalRuleId::PlibPropertyReferenceRequiresVersion, "plib_property_reference_requires_version", 12551, ForExternallyDefinedGeneralProperty },
    { GlobalRuleId::PlyReference, "ply_reference", 12564, ForPlyReference },
    { GlobalRuleId::PlyStockMaterialSelect, "ply_stock_material_select", 12609, ForPlyStockMaterialSelect },
    { GlobalRuleId::ProductConceptFeatureRequiresCategory, "product_concept_feature_requires_category", 12674, ForProductConceptFeature },
    { GlobalRuleId::ProductDefinitionReplacementRequiresEffectivityAssignment,
      "product_definition_replacement_requires_effectivity_assignment",
      12685,
      ForProductDefinitionRelationship },
    { GlobalRuleId::RestrictAlternativeDefinition, "restrict_alternative_definition", 12694, ForProductDefinition },
    { GlobalRuleId::RestrictAssemblyCategory, "restrict_assembly_category", 12714, ForProductDefinition },
    { GlobalRuleId::RestrictCentreOfMassRepresentation, "restrict_centre_of_mass_representation", 12726, ForRepresentation },
    { GlobalRuleId::RestrictClassificationAssignments, "restrict_classification_assignments", 12738, ForAppliedClassificationAssignment },
    { GlobalRuleId::RestrictCollectionCategory, "restrict_collection_category", 12745, ForProductDefinition },
    { GlobalRuleId::RestrictConceptFeatureOperator, "restrict_concept_feature_operator", 12757, ForConceptFeatureOperator },
    { GlobalRuleId::RestrictConfigurationDesignForClassBreakdownAssociation,
      "restrict_configuration_design_for_class_breakdown_association",
      12772,
      ForConfigurationDesign },
    { GlobalRuleId::RestrictConfigurationDesignForDesignConstraint,
      "restrict_configuration_design_for_design_constraint",
      12791,
      ForConfigurationDesign },
    { GlobalRuleId::RestrictGroupRelationshipForClassificationHierarchy,
      "restrict_group_relationship_for_classification_hierarchy",
      12800,
      ForGroupRelationship },
    { GlobalRuleId::RestrictGroupRelationshipForSpecificationCategory,
      "restrict_group_relationship_for_specification_category",
      12809,
      ForGroupRelationship },
    { GlobalRuleId::RestrictLanguageAssignmentPerAttribute, "restrict_language_assignment_per_attribute", 12818, ForAttributeLanguageAssignment },
    { GlobalRuleId::RestrictPartOccurrence, "restrict_part_occurrence", 12831, ForProductDefinition },
    { GlobalRuleId::RestrictPartOccurrenceCategory, "restrict_part_occurrence_category", 12871, ForProductDefinition },
    { GlobalRuleId::RestrictProductDefinitionsForBaseElement,
      "restrict_product_definitions_for_base_element",
      12882,
      ForProductDefinitionRelationship },
    { GlobalRuleId::RestrictProductDefinitionsForCollection, "restrict_product_definitions_for_collection", 12892, ForProductDefinitionRelationship },
    { GlobalRuleId::RestrictProductDefinitionsForDefinitionUsage,
      "restrict_product_definitions_for_definition_usage",
      12905,
      ForProductDefinitionRelationship },
    { GlobalRuleId::RestrictProductDefinitionsForDesignConstraintAssociation,
      "restrict_product_definitions_for_design_constraint_association",
      12914,
      ForProductDefinitionRelationship },
    { GlobalRuleId::RestrictProductDefinitionsForPartDefinitionRelationship,
      "restrict_product_definitions_for_part_definition_relationship",
      12924,
      ForProductDefinitionRelationship },
    { GlobalRuleId::RestrictRepresentationForSurfaceCondition,
      "restrict_representation_for_surface_condition",
      12933,
      ForPropertyDefinitionRepresentation },
    { GlobalRuleId::RestrictTreatmentResult, "restrict_treatment_result", 12940, ForRepresentation },
    { GlobalRuleId::SelectedInstanceUsageRequiresRepresentation, "selected_instance_usage_requires_representation", 12957, ForAssemblyComponentUsage },
    { GlobalRuleId::SolutionDefinitionRequiresSolutionCategory, "solution_definition_requires_solution_category", 12967, ForProductDefinition },
    { GlobalRuleId::StockMaterialReference, "stock_material_reference", 12979, ForStockMaterialReference },
    { GlobalRuleId::StyledCurve, "styled_curve", 13027, ForStyledItem },
    { GlobalRuleId::SubtypeExclusivenessGeometricTolerance, "subtype_exclusiveness_geometric_tolerance", 13033, ForGeometricTolerance },
    { GlobalRuleId::SubtypeExclusivenessRepresentationItem, "subtype_exclusiveness_representation_item", 13039, ForRepresentationItem },
    { GlobalRuleId::SubtypeMandatoryGeometricTolerance, "subtype_mandatory_geometric_tolerance", 13048, ForGeometricTolerance },
    { GlobalRuleId::TextFontUsage, "text_font_usage", 13054, ForTextFontUsage },
    { GlobalRuleId::ThicknessLaminateTableComponentSelect,
      "thickness_laminate_table_component_select",
      13061,
      ForThicknessLaminateTableComponentSelect },
    { GlobalRuleId::ValidateDependentlyInstantiableEntityDataTypes,
      "validate_dependently_instantiable_entity_data_types",
      13108,
      ForValidateDependentlyInstantiableEntityDataTypes },
};
// Preserve the established source-rule execution/report order even if Globals is rearranged.
inline constexpr std::array GlobalExecutionOrder = {
    GlobalRuleId::AlternativeSolutionRequiresSolutionDefinition,
    GlobalRuleId::ApplicationProtocolDefinitionRequired,
    GlobalRuleId::BreakdownElementRequiresProductDefinition,
    GlobalRuleId::CompatibleDimension,
    GlobalRuleId::ComponentClassForAssemblySelect,
    GlobalRuleId::ConsistentUncertainty,
    GlobalRuleId::ConstraintDefinitionRequiresConstraintCategory,
    GlobalRuleId::DesignConstraintRequiresProductDefinition,
    GlobalRuleId::DraughtingModelItemsConstraint,
    GlobalRuleId::ExternalVersionAssignmentsAreValid,
    GlobalRuleId::MaterialForCoatingLayer,
    GlobalRuleId::PlibPropertyReferenceRequiresNameScope,
    GlobalRuleId::PlibPropertyReferenceRequiresVersion,
    GlobalRuleId::PlyReference,
    GlobalRuleId::PlyStockMaterialSelect,
    GlobalRuleId::ProductConceptFeatureRequiresCategory,
    GlobalRuleId::ProductDefinitionReplacementRequiresEffectivityAssignment,
    GlobalRuleId::RestrictAlternativeDefinition,
    GlobalRuleId::RestrictAssemblyCategory,
    GlobalRuleId::RestrictCentreOfMassRepresentation,
    GlobalRuleId::RestrictClassificationAssignments,
    GlobalRuleId::RestrictCollectionCategory,
    GlobalRuleId::RestrictConceptFeatureOperator,
    GlobalRuleId::RestrictConfigurationDesignForClassBreakdownAssociation,
    GlobalRuleId::RestrictConfigurationDesignForDesignConstraint,
    GlobalRuleId::RestrictGroupRelationshipForClassificationHierarchy,
    GlobalRuleId::RestrictGroupRelationshipForSpecificationCategory,
    GlobalRuleId::RestrictLanguageAssignmentPerAttribute,
    GlobalRuleId::RestrictPartOccurrence,
    GlobalRuleId::RestrictPartOccurrenceCategory,
    GlobalRuleId::RestrictProductDefinitionsForBaseElement,
    GlobalRuleId::RestrictProductDefinitionsForCollection,
    GlobalRuleId::RestrictProductDefinitionsForDefinitionUsage,
    GlobalRuleId::RestrictProductDefinitionsForDesignConstraintAssociation,
    GlobalRuleId::RestrictProductDefinitionsForPartDefinitionRelationship,
    GlobalRuleId::RestrictRepresentationForSurfaceCondition,
    GlobalRuleId::RestrictTreatmentResult,
    GlobalRuleId::SelectedInstanceUsageRequiresRepresentation,
    GlobalRuleId::SolutionDefinitionRequiresSolutionCategory,
    GlobalRuleId::StockMaterialReference,
    GlobalRuleId::StyledCurve,
    GlobalRuleId::SubtypeExclusivenessGeometricTolerance,
    GlobalRuleId::SubtypeExclusivenessRepresentationItem,
    GlobalRuleId::SubtypeMandatoryGeometricTolerance,
    GlobalRuleId::TextFontUsage,
    GlobalRuleId::ThicknessLaminateTableComponentSelect,
    GlobalRuleId::ValidateDependentlyInstantiableEntityDataTypes,
};

consteval bool GlobalRegistryIsValid()
{
    if (std::size(Globals) != GlobalExecutionOrder.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < std::size(Globals); ++i)
    {
        const auto& rule = Globals[i];
        if (rule.name.empty() || !rule.line || rule.types.empty() || rule.failureRole.empty())
        {
            return false;
        }
        for (const auto type : rule.types)
        {
            if (type.empty())
            {
                return false;
            }
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (Globals[j].id == rule.id || Globals[j].name == rule.name)
            {
                return false;
            }
        }
        std::size_t occurrences = 0;
        for (const auto id : GlobalExecutionOrder)
        {
            occurrences += id == rule.id ? 1 : 0;
        }
        if (occurrences != 1)
        {
            return false;
        }
    }
    return true;
}
static_assert(std::size(Globals) == 47, "Pinned N2635 profile requires all 47 global rules");
static_assert(GlobalRegistryIsValid(), "Global rule identities, names and execution coverage must be unique and complete");

constexpr const Global& GlobalMetadata(GlobalRuleId id)
{
    for (const auto& rule : Globals)
    {
        if (rule.id == id)
        {
            return rule;
        }
    }
    throw std::logic_error("Unregistered global rule identity");
}
} // namespace step_planar
