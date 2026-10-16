// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
// Fixed declaration metadata extracted from N2635 c68dd022; no runtime schema dependency.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>
namespace step_planar
{
struct Clause
{
    const char* type;
    const char* kind;
    const char* label;
    std::size_t line;
};
inline const std::vector<Clause> Clauses = {
    { "ADVANCED_BREP_SHAPE_REPRESENTATION", "WHERE", "WR1", 2125 },
    { "ADVANCED_BREP_SHAPE_REPRESENTATION", "WHERE", "WR2", 2127 },
    { "ADVANCED_BREP_SHAPE_REPRESENTATION", "WHERE", "WR3", 2129 },
    { "ADVANCED_BREP_SHAPE_REPRESENTATION", "WHERE", "WR4", 2134 },
    { "ADVANCED_BREP_SHAPE_REPRESENTATION", "WHERE", "WR5", 2137 },
    { "ADVANCED_BREP_SHAPE_REPRESENTATION", "WHERE", "WR6", 2141 },
    { "ADVANCED_FACE", "WHERE", "WR1", 2150 },
    { "ADVANCED_FACE", "WHERE", "WR2", 2151 },
    { "ADVANCED_FACE", "WHERE", "WR3", 2155 },
    { "ADVANCED_FACE", "WHERE", "WR4", 2159 },
    { "ADVANCED_FACE", "WHERE", "WR5", 2163 },
    { "ADVANCED_FACE", "WHERE", "WR6", 2166 },
    { "ADVANCED_FACE", "WHERE", "WR7", 2167 },
    { "ADVANCED_FACE", "WHERE", "WR8", 2170 },
    { "ADVANCED_FACE", "WHERE", "WR9", 2172 },
    { "ADVANCED_FACE", "WHERE", "WR10", 2177 },
    { "APPLICATION_CONTEXT", "INVERSE", "context_elements", 2399 },
    { "APPLICATION_CONTEXT", "WHERE", "WR1", 2401 },
    { "APPLICATION_CONTEXT", "WHERE", "WR2", 2402 },
    { "AXIS2_PLACEMENT_3D", "WHERE", "WR1", 2751 },
    { "AXIS2_PLACEMENT_3D", "WHERE", "WR2", 2752 },
    { "AXIS2_PLACEMENT_3D", "WHERE", "WR3", 2753 },
    { "AXIS2_PLACEMENT_3D", "WHERE", "WR4", 2754 },
    { "CONVERSION_BASED_UNIT", "WHERE", "WR1", 3995 },
    { "DERIVED_UNIT", "WHERE", "WR1", 4391 },
    { "DERIVED_UNIT", "WHERE", "WR2", 4392 },
    { "DESIGN_CONTEXT", "WHERE", "WR1", 4418 },
    { "DIRECTION", "WHERE", "WR1", 4678 },
    { "EDGE_LOOP", "WHERE", "WR1", 5324 },
    { "FACE", "WHERE", "WR1", 5946 },
    { "FACE", "WHERE", "WR2", 5947 },
    { "FACE_SURFACE", "WHERE", "WR1", 5975 },
    { "FOUNDED_ITEM", "WHERE", "WR1", 6135 },
    { "FOUNDED_ITEM", "WHERE", "WR2", 6136 },
    { "GEOMETRIC_REPRESENTATION_ITEM", "WHERE", "WR1", 6290 },
    { "LENGTH_MEASURE_WITH_UNIT", "WHERE", "WR1", 6824 },
    { "LENGTH_UNIT", "WHERE", "WR1", 6831 },
    { "LINE", "WHERE", "WR1", 6894 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR1", 7168 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR2", 7172 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR3", 7175 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR4", 7184 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR5", 7192 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR6", 7199 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR7", 7208 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR8", 7219 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR9", 7231 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR10", 7253 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR11", 7265 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR12", 7281 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR13", 7303 },
    { "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "WHERE", "WR14", 7315 },
    { "MASS_UNIT", "WHERE", "WR1", 7352 },
    { "MEASURE_WITH_UNIT", "WHERE", "WR1", 7419 },
    { "MECHANICAL_CONTEXT", "WHERE", "WR1", 7426 },
    { "ORIENTED_EDGE", "WHERE", "WR1", 8728 },
    { "PATH", "WHERE", "WR1", 8852 },
    { "PLANE_ANGLE_UNIT", "WHERE", "WR1", 9048 },
    { "PRODUCT_CATEGORY", "WHERE", "WR1", 9474 },
    { "PRODUCT_DEFINITION", "WHERE", "WR1", 9574 },
    { "PRODUCT_DEFINITION_FORMATION", "UNIQUE", "UR1", 9615 },
    { "PRODUCT_DEFINITION_SHAPE", "UNIQUE", "UR1", 9668 },
    { "PRODUCT_DEFINITION_SHAPE", "WHERE", "WR1", 9670 },
    { "PROPERTY_DEFINITION", "WHERE", "WR1", 9789 },
    { "PROPERTY_DEFINITION_REPRESENTATION", "WHERE", "WR1", 9808 },
    { "PROPERTY_DEFINITION_REPRESENTATION", "WHERE", "WR2", 9809 },
    { "REPRESENTATION", "WHERE", "WR1", 10012 },
    { "REPRESENTATION", "WHERE", "WR2", 10015 },
    { "REPRESENTATION_CONTEXT", "INVERSE", "representations_in_context", 10025 },
    { "REPRESENTATION_ITEM", "WHERE", "WR1", 10033 },
    { "SHAPE_DEFINITION_REPRESENTATION", "WHERE", "WR1", 10418 },
    { "SHAPE_DEFINITION_REPRESENTATION", "WHERE", "WR2", 10419 },
    { "SHELL_BASED_SURFACE_MODEL", "WHERE", "WR1", 10465 },
    { "SI_UNIT", "WHERE", "WR1", 10709 },
    { "SOLID_ANGLE_UNIT", "WHERE", "WR1", 10758 },
    { "UNCERTAINTY_MEASURE_WITH_UNIT", "WHERE", "WR1", 12100 },
    { "VECTOR", "WHERE", "WR1", 12209 },
};
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
