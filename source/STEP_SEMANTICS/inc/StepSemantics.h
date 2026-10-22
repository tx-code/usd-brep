// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepSource.h"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace step_semantics
{
inline constexpr const char* Profile = "203e2-P0-Planar-0.1";
inline constexpr const char* SchemaName = "AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF";
inline constexpr const char* SchemaSha256 = "c68dd02200e2f4213e311553a4f6295effd168209de1fdc4e8003ce4c9ddd088";
inline constexpr const char* Ap214SourceProfile = "214e3-P0-Source-0.1";
inline constexpr const char* Ap214SchemaSha256 = "71ab140fe7f774321beee6a31e6fee2afc3973fd60350ae2018c74c211fb4295";
inline constexpr const char* Ap214MbxSourceProfile = "214-MBx2010-Source-0.1";
inline constexpr const char* Ap203GeneralSourceProfile = "203e2-G0-Source-0.1";
inline constexpr const char* Ap203N916SourceProfile = "203-N916-Source-0.1";
inline constexpr const char* SourceFaceClauseSelectionProfile = "SourceFaceClauseSelection-0.1";
inline constexpr const char* Ap203N916SchemaSha256 = "020b4d25dbd0b6ee7d15099b978e3448f6699a72cb862d381e416e32187562f1";

enum class ProfileKind
{
    Ap203e2Planar,
    Ap214e3Source,
    Ap203e2GeneralSource,
    Ap214MbxSource,
    Ap203N916Source
};

struct Limits
{
    std::size_t dependencyVisits = 1000000; // Explicit source-test preset; no production precision policy.
    std::size_t preservedDiagnostics = 1024; // Explicit interoperability only; never suppress excess issues.
};
enum class SourcePolicy
{
    Strict,
    PreserveNonGeometric
};
enum class GeometryScope
{
    ExplicitOuterBounds,
    GeneralBrep // Source regions without an explicit outer bound require a later geometric proof.
};
enum class SchemaInterpretation
{
    DeclaredOnly,
    // Application assumption for exactly one unregistered AP214 identifier.
    // This chooses a fixed implementation; it does not authenticate the HEADER.
    UnregisteredAp214As2010,
    Ap214NameOnlyAs2010 // Exact bare AUTOMOTIVE_DESIGN; caller selects the fixed 2010 compatibility subset.
};
const char* SchemaInterpretationName(SchemaInterpretation interpretation);

enum class UnitInterpretation
{
    DeclaredOnly,
    VerifiedExplicitUnitDimensions // Exact redundant AP214 SI/conversion-length dimensions, original marker error retained.
};
const char* UnitInterpretationName(UnitInterpretation interpretation);
enum class ComplexMappingInterpretation
{
    DeclaredOnly,
    RegisteredFacetNames // Explicit nonconforming order; complete registered facet/field binding still required.
};
const char* ComplexMappingInterpretationName(ComplexMappingInterpretation interpretation);
struct ComplexMappingAssumption
{
    std::size_t record = 0;
    std::vector<std::size_t> canonicalComponentOrder; // Canonical position -> original zero-based component index; source is never reordered.
};
struct CylindricalBandRepairDeferral
{
    std::string definitionId, shapeId, faceId;
    std::array<std::string, 2> sourceOuterBoundIds; // Exact original pair; no source marker is rewritten by this layer.
};
struct UnitDimensionAssumption
{
    std::size_t unit, dimensions; // Original source records, never substituted unit definitions.
    std::optional<std::size_t> factor; // Absent for SI dimensions derived from the original SI name.
};
enum class Protocol
{
    Unknown,
    Ap203e1,
    Ap203e2,
    Ap214,
    Ap242
};
enum class SchemaMatch
{
    Malformed,
    UnknownName,
    NameOnly,
    LegacyAlias,
    UnregisteredVersion,
    RegisteredVersion
};
struct SchemaIdentifier
{
    std::string literal, name;
    std::vector<std::string> oid; // Original numeric arc strings; no integer conversion or leading-zero normalization.
    step_source::Span span;
    Protocol protocol = Protocol::Unknown;
    SchemaMatch match = SchemaMatch::Malformed;
    std::string registryId; // Identity recognition only, not permission to run a type/rule implementation.
};
struct QualifiedSchemaProfile
{
    // This selects a source-check implementation; downstream rule/geometry capability is separate.
    std::string registryId, profile, expressSha256;
    ProfileKind kind = ProfileKind::Ap203e2Planar;
};
struct SchemaSelection
{
    std::vector<SchemaIdentifier> identifiers; // Complete HEADER order; owning observations survive failed binding.
    std::optional<QualifiedSchemaProfile> qualified; // Absent for known versions without an implemented profile.
    SchemaInterpretation appliedInterpretation = SchemaInterpretation::DeclaredOnly;
};

struct AggregateDimension
{
    std::string_view aggregate;
    std::size_t minimum = 0;
    std::optional<std::size_t> maximum;
    bool uniqueElements = false;
};
struct AttributeView
{
    std::string_view name;
    std::string_view declaredIn;
    std::string_view declaredType; // Effective type after explicit narrowing; declaredIn retains the slot's origin.
    std::string_view redeclaredBy; // Explicit or DERIVE redeclaring subtype. Only derivedSlot denotes a source '*'.
    bool optional = false;
    bool derivedSlot = false;
    std::string_view aggregate; // LIST/SET, empty for scalars.
    std::size_t minimum = 0;
    std::optional<std::size_t> maximum;
    bool uniqueElements = false;
    // Static descriptor storage; the actual source Value still borrows its snapshot.
    std::span<const AggregateDimension> nestedAggregates;
    const step_source::Value* value = nullptr; // Borrowed from the immutable owning SourceSnapshot.
};

struct Role
{
    std::string name;
    std::size_t recordIndex = 0;
};

struct Part
{
    struct PropertyAttachment
    {
        // Source indices belonging to this snapshot, not portable IDs or inferred
        // material values. Keep each source link even when names/items are shared.
        std::size_t link, propertyDefinition, characterizedDefinition, productDefinition, representation, context;
    };
    struct Shape
    {
        std::size_t shape, shell; // Source record indices; never native handles or selected-largest substitutes.
        std::string role; // The individual source role, independent of the collection's aggregate role.
    };
    std::vector<Shape> shapes; // Complete representation item order; shared source definitions retain their identities.
    std::vector<Role> roles; // Singleton role map. Collections use shapes, never duplicate shape/shell role keys.
    std::vector<PropertyAttachment> propertyAttachments;
    std::vector<std::size_t> deferredRegionFaces; // Source faces only; no bound has been relabeled or selected as outer.
    std::vector<std::size_t> deferredCylindricalBandRepairs; // Explicit FACE.WR2 failures; source remains invalid pending native repair proof.
    std::string shapeRole; // MaterialSolid / OpenSurfaceModel / ClosedSurfaceModel: source claims only.
    std::string apdStatus;
    std::string apdSchemaClaim;
    std::string apdYear; // Exact INTEGER lexeme; never used to infer the HEADER edition.
    bool applicationProtocolPresent = true; // False means unavailable source metadata; never synthesize an APD.
};

struct SiUnit
{
    std::size_t recordIndex = 0;
    std::string quantity; // Length / PlaneAngle / SolidAngle / Mass; nominal type is preserved.
    std::string siName;
    std::string prefix; // Empty means explicitly absent prefix, not a guessed unit.
    std::array<int, 7> dimensions{};
    int canonicalPower10 = 0; // Exact SI metadata scale to mm/rad/sr/kg; original unit/value data stay unchanged.
};

struct DimensionExponent
{
    std::string numerator;
    std::string denominator;
};
struct UnitDimensionDefinition
{
    std::size_t recordIndex = 0;
    std::string nominal; // Source subtype identity, never inferred from an equal vector.
    std::array<DimensionExponent, 7> dimensions; // L/M/T/I/temperature/amount/luminous; derived data, source unchanged.
};
struct Units
{
    std::size_t lengthUnit = 0;
    std::size_t planeAngleUnit = 0;
    std::optional<std::size_t> solidAngleUnit;
    std::vector<SiUnit> siDefinitions;
    std::optional<int> lengthPower10Mm; // Absent for deferred conversion-based interpretation.
    std::vector<UnitDimensionDefinition> dimensionDefinitions; // AP214 source-dimensional analysis only, not a unit scale.
};

struct Diagnostic
{
    step_source::Diagnostic source;
    std::string classification; // SourceInvalid / ProfileUnsupported / ResourceLimit / SourceInspectionFailure.
    std::string field;
    std::string declaredIn;
    std::string declaredType;
    std::string expected;
    std::string actual;
    std::string responsibility;
};

struct SemanticData;
struct ProductStructureData;
struct ProductRepresentationLink
{
    std::size_t link, shapeDefinition, representation, context;
};
struct ProductDefinitionLink
{
    std::size_t definition, formation, product, definitionContext;
    std::vector<ProductRepresentationLink> representations;
};
struct ProductUsageLink
{
    std::size_t usage, relatingDefinition, relatedDefinition;
    bool nextAssemblyOccurrence = false;
};
struct ContextDependentShapeLink
{
    std::size_t link, representationRelationship, productDefinitionShape, productRelationship;
};
struct RepresentationTransformLink
{
    std::size_t relationship, firstRepresentation, secondRepresentation;
    std::size_t firstContext, secondContext, transformation, firstItem, secondItem;
};
struct ProductRepresentationRelationship
{
    std::size_t relationship, firstRepresentation, secondRepresentation, firstContext, secondContext;
    bool shapeRelationship = false;
    std::optional<std::size_t> transformation;
};
struct ProductStructureBasis
{
    QualifiedSchemaProfile profile;
    SchemaInterpretation schemaInterpretation = SchemaInterpretation::DeclaredOnly;
    SourcePolicy sourcePolicy = SourcePolicy::Strict;
    ComplexMappingInterpretation complexMappingInterpretation = ComplexMappingInterpretation::DeclaredOnly;
    std::vector<Diagnostic> complexMappingDiagnostics; // Owning type-binding evidence only, not complete source/model qualification.
};
// Declared product graph evidence only: immutable source identities/references,
// usage uniqueness/acyclicity and selected dependency predicates. It grants no
// qualified part snapshot, unit/placement interpretation or native model.
class ProductStructure
{
public:

    explicit ProductStructure(std::shared_ptr<const ProductStructureData> data);
    const step_source::SourceSnapshot& Source() const;
    const ProductStructureBasis& Basis() const;
    std::span<const ProductDefinitionLink> Definitions() const;
    std::span<const ProductUsageLink> Usages() const;
    std::span<const ContextDependentShapeLink> ContextDependentShapes() const;
    std::span<const RepresentationTransformLink> Transforms() const;
    std::span<const ProductRepresentationRelationship> RepresentationRelationships() const;
    std::span<const std::size_t> Roots() const; // Usage roots, never an implicit native/root selection.

private:

    std::shared_ptr<const ProductStructureData> m_data;
};
struct AttributeLocation
{
    std::string recordId;
    std::string component;
    step_source::Span span;
    std::vector<std::size_t> parameterPath; // One-based source slot; strings/path own their storage.
};

// An owning, read-only field handle. References returned from it borrow its snapshot lifetime.
class FieldView
{
public:

    const AttributeView& Declaration() const;
    const step_source::Value& Value() const;
    AttributeLocation Location() const;
    // Results are record indices local to this source graph, not portable entity identities.
    std::size_t ReferenceTarget() const;
    // Flat reference aggregates only. Walk nested Value rows with Snapshot::ReferenceTarget;
    // flattening a control grid here would hide its source shape.
    std::vector<std::size_t> ReferenceTargets() const;

private:

    friend class Snapshot;
    FieldView(std::shared_ptr<const SemanticData> data, std::size_t record, std::size_t binding);
    std::shared_ptr<const SemanticData> m_data;
    std::size_t m_record;
    std::size_t m_binding;
};

struct SelectedSourceRequest
{
    std::string sourceSha256;
    std::vector<std::string> definitionIds;
    std::size_t maximumIsolatedFailures = 0, maximumInfluenceVisits = 0;
};
struct IsolatedSourceFailure
{
    std::string definitionId;
    Diagnostic diagnostic; // Original failed FACE.WR2, separate from metadata preservation and repair.
};
struct SourceSelectionEvidence
{
    SelectedSourceRequest request;
    std::vector<std::string> sourceDefinitionIds, selectedDefinitionIds, excludedDefinitionIds;
    std::vector<IsolatedSourceFailure> failures;
    std::size_t influenceVisits = 0;
    bool localityEstablished = false;
};

class Snapshot
{
public:

    explicit Snapshot(std::shared_ptr<const SemanticData> data);
    const step_source::SourceSnapshot& Source() const;
    // Borrowed identity views remain valid while this owning snapshot lives.
    const QualifiedSchemaProfile& SelectedProfile() const;
    const SchemaIdentifier& SchemaIdentity() const;
    SchemaInterpretation AppliedInterpretation() const;
    const std::vector<Diagnostic>& InterpretationDiagnostics() const;
    UnitInterpretation AppliedUnitInterpretation() const;
    const std::vector<UnitDimensionAssumption>& UnitDimensionAssumptions() const;
    const std::vector<Diagnostic>& UnitInterpretationDiagnostics() const;
    const std::vector<ComplexMappingAssumption>& ComplexMappingAssumptions() const;
    const std::vector<Diagnostic>& ComplexMappingDiagnostics() const;
    const std::vector<CylindricalBandRepairDeferral>& CylindricalBandRepairDeferrals() const;
    const std::vector<Diagnostic>& CylindricalBandRepairDiagnostics() const;
    const Part& PartDefinition() const;
    const Units& UnitContext() const;
    const std::optional<SourceSelectionEvidence>& SourceSelection() const;
    // In document views, only part/unit qualification is definition-scoped.
    // Type/field/reference queries still inspect the one captured source graph.
    const std::vector<Diagnostic>& PreservedDiagnostics() const;
    const std::vector<std::size_t>& UnattachedNonGeometricDeclarations() const;
    std::span<const std::string_view> RegisteredTypes() const;
    const std::vector<std::size_t>* Instances(std::string_view type) const; // Null: unregistered, not a proved empty set.
    bool IsA(std::size_t recordIndex, std::string_view type) const; // Unknown type is an error.
    std::vector<std::string> TypesOf(std::size_t recordIndex) const;
    AttributeView Attribute(std::size_t recordIndex, std::size_t componentIndex, std::size_t parameterIndex) const;
    // Named lookup rejects missing/ambiguous fields. Qualification uses the original declaring type.
    FieldView Field(std::size_t recordIndex, std::string_view name) const;
    FieldView Field(std::size_t recordIndex, std::string_view declaredIn, std::string_view name) const;
    // Requires an actual reference Value owned by this source snapshot, not a copied/foreign value.
    std::size_t ReferenceTarget(const step_source::Value& value) const;
    std::vector<std::size_t> UsedIn(std::string_view targetId, std::string_view ownerType, std::string_view field) const;
    bool UsedByRepresentation(std::size_t recordIndex) const; // RI-filtered use closure, not a generic management walk.

private:

    friend class ProductDocument;
    Snapshot(std::shared_ptr<const SemanticData> data, std::size_t partIndex);
    std::shared_ptr<const SemanticData> m_data;
    std::optional<std::size_t> m_partIndex;
};

// Qualified independent definitions sharing one immutable source/type binding
// owner. Default mode is complete; SourceSelection explicitly identifies a
// selected-only view and retains the original all-definition inventory/failures.
// There are no assembly placements or implicit surviving-root selection.
class ProductDocument
{
public:

    explicit ProductDocument(std::shared_ptr<const SemanticData> data);
    const step_source::SourceSnapshot& Source() const;
    std::size_t PartCount() const;
    std::shared_ptr<const Snapshot> Part(std::size_t index) const;
    const std::vector<Role>& Roles() const; // Whole-document ownership roots, not a singular role map.
    const std::optional<SourceSelectionEvidence>& SourceSelection() const; // Selected mode exposes only qualified selected Part() views.

private:

    std::shared_ptr<const SemanticData> m_data;
};

enum class Status
{
    Checked, // Only the ticket-03 checks; never complete AP validity or model acceptance.
    CheckedWithDiagnostics, // Explicit interoperability; original nonconformances remain in the owning source.
    Deferred,
    Rejected,
    Unsupported,
    ResourceLimit,
    SourceFailure,
    CheckedSelectedDefinitions // Explicitly scoped qualification; excluded source failures remain owned.
};

struct DefinitionPrerequisiteCheck
{
    std::size_t definition = 0; // Index in the one original source, never a new or renumbered record.
    std::string part = "NotRun";
    std::string units = "NotRun";
    std::vector<Diagnostic> diagnostics;
};
struct DefinitionInspectionData;
// Diagnostic observations only. Even an entirely green inspection does not
// establish document ownership, interoperability approval or a qualified part.
class DefinitionInspection
{
public:

    explicit DefinitionInspection(std::shared_ptr<const DefinitionInspectionData> data);
    const ProductStructure& Structure() const;
    std::span<const DefinitionPrerequisiteCheck> Checks() const;
    std::span<const Diagnostic> UnapprovedDiagnostics() const; // Pending metadata facts, without final ownership/exception approval.
    bool AllDefinitionsVisited() const;

private:

    std::shared_ptr<const DefinitionInspectionData> m_data;
};

struct UnitDependencyCounts
{
    std::size_t total = 0;
    std::size_t unitDefinitions = 0;
    std::size_t typePredicates = 0;
};
struct GeometryDependencyCounts
{
    std::size_t total = 0;
    std::size_t typePredicates = 0;
    std::size_t entityPredicates = 0;
};
struct Outcome
{
    Status status = Status::SourceFailure;
    SourcePolicy policy = SourcePolicy::Strict;
    GeometryScope geometryScope = GeometryScope::ExplicitOuterBounds;
    SchemaInterpretation requestedInterpretation = SchemaInterpretation::DeclaredOnly;
    UnitInterpretation requestedUnitInterpretation = UnitInterpretation::DeclaredOnly;
    ComplexMappingInterpretation requestedComplexMappingInterpretation = ComplexMappingInterpretation::DeclaredOnly;
    std::shared_ptr<const Snapshot> snapshot;
    std::shared_ptr<const ProductDocument> document;
    std::shared_ptr<const ProductStructure> productStructure; // Separate, structure-only evidence can survive single-part refusal.
    std::shared_ptr<const DefinitionInspection> definitionInspection; // Explicit diagnostic-only per-definition observations.
    bool definitionInspectionRequested = false;
    std::optional<SchemaSelection> schemaSelection; // Null means the binding phase did not run.
    std::string binding = "NotRun";
    std::string types = "NotRun";
    std::string unitDependencies = "NotRun";
    std::string geometryDependencies = "NotRun"; // Selected AP214 source prerequisites, not complete local/global rules.
    std::string part = "NotRun";
    std::string siUnits = "NotRun";
    std::string interoperability = "NotRun";
    std::size_t dependencyVisits = 0;
    GeometryDependencyCounts geometryDependencyChecks;
    UnitDependencyCounts unitDependencyChecks;
    std::size_t propertyAttachmentChecks = 0; // Actual AP214 links attempted within part/closure guards.
    Limits limits;
    std::vector<Diagnostic> diagnostics;
    std::vector<Diagnostic> preservedDiagnostics; // Approved nongeometric issues only, separate from fatal failures.
    // Original unsupported binding, separately retained as an application
    // assumption. Never describe this as a proven nongeometric exception.
    std::vector<Diagnostic> interpretationDiagnostics;
    std::vector<Diagnostic> unitInterpretationDiagnostics; // Physical source nonconformance, not a metadata-only exception.
    std::vector<Diagnostic> complexMappingDiagnostics; // Observed original order errors; on failure these are NOT qualified assumptions.
    std::vector<Diagnostic> cylindricalBandRepairDiagnostics; // Original failed source clauses; never metadata exceptions.
    std::vector<Diagnostic> unapprovedDiagnostics; // Diagnostic-mode pending facts only, without exception approval.
    std::optional<SelectedSourceRequest> sourceSelectionRequest;
    std::vector<IsolatedSourceFailure> isolatedSourceFailures; // Provisional until owning SourceSelection proves locality.
    std::size_t sourceInfluenceVisits = 0;
};

Outcome Check(const step_source::InspectOutcome& source, Limits limits);
Outcome Check(const step_source::InspectOutcome& source, Limits limits, SourcePolicy policy, GeometryScope scope = GeometryScope::ExplicitOuterBounds);
Outcome Check(const step_source::InspectOutcome& source, Limits limits, SourcePolicy policy, GeometryScope scope, SchemaInterpretation interpretation);
Outcome Check(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    GeometryScope scope,
    SchemaInterpretation interpretation,
    UnitInterpretation unitInterpretation,
    ComplexMappingInterpretation complexMappingInterpretation = ComplexMappingInterpretation::DeclaredOnly,
    std::span<const CylindricalBandRepairDeferral> cylindricalBandRepairs = {}
);
bool InspectJson(const step_source::InspectOutcome& source, const Outcome& semantics, std::string& output);
Outcome CheckIndependentProducts(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    SchemaInterpretation interpretation = SchemaInterpretation::DeclaredOnly,
    UnitInterpretation unitInterpretation = UnitInterpretation::DeclaredOnly,
    ComplexMappingInterpretation complexMappingInterpretation = ComplexMappingInterpretation::DeclaredOnly,
    std::span<const CylindricalBandRepairDeferral> cylindricalBandRepairs = {}
);
// Continues independent part/unit observations after a definition-local failure.
// Complete parsing, types and global dependency checks remain prerequisites.
// This API never publishes snapshot/document and never licenses source isolation.
Outcome InspectIndependentDefinitions(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    SchemaInterpretation interpretation = SchemaInterpretation::DeclaredOnly,
    UnitInterpretation unitInterpretation = UnitInterpretation::DeclaredOnly,
    ComplexMappingInterpretation complexMappingInterpretation = ComplexMappingInterpretation::DeclaredOnly
);
Outcome CheckSelectedIndependentProducts(
    const step_source::InspectOutcome& source,
    Limits limits,
    SourcePolicy policy,
    const SelectedSourceRequest& selection,
    SchemaInterpretation interpretation = SchemaInterpretation::DeclaredOnly,
    UnitInterpretation unitInterpretation = UnitInterpretation::DeclaredOnly,
    ComplexMappingInterpretation complexMappingInterpretation = ComplexMappingInterpretation::DeclaredOnly
);
const char* StatusName(Status status);
} // namespace step_semantics
