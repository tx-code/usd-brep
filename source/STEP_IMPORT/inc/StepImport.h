// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepPlanar.h"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class SmBrep;

// Internal native-import interface. Imported snapshots expose no mutable native handles.
namespace step_import
{
using Point = std::array<double, 3>;
using Bounds = std::array<Point, 2>;
using Fraction = step_planar::Rational;
namespace detail
{
struct ModelData;
struct CollectionData;
struct DocumentData;
struct EditableData;
struct Access;
struct UnitRatioAccess;
class Execution;
} // namespace detail
enum class CancelResult
{
    Requested,
    AlreadyRequested,
    TooLate,
    AlreadyCompleted
};
class ImportControl
{
public:

    CancelResult RequestCancel() noexcept; // One control per invocation; thread safe. A request is not immediate completion.

private:

    friend class detail::Execution;
    enum class State
    {
        Ready,
        Requested,
        Accepted,
        Cancelled,
        Failed
    };
    std::atomic<State> m_state{ State::Ready };
    std::atomic<bool> m_started{ false };
};
enum class Phase
{
    NotStarted,
    BeforeCapture,
    Captured,
    SemanticsChecked,
    PlanarChecked,
    BeforeNativeCall,
    NativeReady,
    BeforeReport,
    ReportReady,
    BeforeCommit,
    BeforeRepairProof,
    BeforeRepair,
    RepairReady,
    BeforeDefinitionSelection,
    DefinitionSelectionReady
};
struct Progress
{
    Phase phase;
    std::size_t nativeCalls;
};
struct ExecutionOptions
{
    std::shared_ptr<ImportControl> control;
    std::function<void(Progress)> progress; // Synchronous safe-boundary observer; exceptions are execution failures.
    std::optional<std::chrono::steady_clock::time_point> deadline;
    bool measureProcessMemory = false;
};
struct Quality
{
    std::string id;
    Fraction definitionMm, consistencyMm, derivedUvMm;
    Fraction initialWorkToleranceMm, maximumWorkToleranceMm, toleranceGrowthMm;
    Fraction queryMm, areaMm2, volumeMm3;
    static Quality PlanarFixture(); // Explicit synthetic-test candidate, never a production default.
    static Quality GeneralDevelopment(); // Explicit development profile; production calibration remains open.
    static Quality GeneralToleranceStudy(); // Explicit 10-micrometre study, not an automatic source-uncertainty policy.
    static Quality GeneralWorkToleranceStudy(); // Same 10um geometry budgets; explicit 10->20um native zone study, never automatic.
};
struct Limits
{
    // Completed-call checkpoints for construction, UV installation, validators and measured queries.
    // Read-only coefficient/topology accessors are not individually charged. This is not a timeout or memory cap.
    std::size_t nativeCalls = 10000;
    std::size_t reportBytes = 16 * 1024 * 1024;
    std::optional<std::size_t> processPrivateBytes; // Cooperative, process-wide commit charge at safe boundaries; not a hard cap.
    std::optional<std::size_t> hardMemoryBytes; // Unsupported in this in-process execution mode; never silently relaxed.
};
struct NumericalIntegrationSettings
{
    // Dimensionless stopping control for general face integration, in [1e-8, 1e-4].
    // Neither a certified relative volume error nor a geometric/repair tolerance.
    double relativeAccuracy = 1e-4;
};
struct NumericalIntegrationObservation
{
    double requestedRelativeAccuracy = 1e-4;
    std::optional<double> appliedRelativeAccuracy; // Present once a native face query starts; never silently clamped.
    bool completed = false; // All fused face integrals completed, not a continuous error certificate.
};
struct TargetFaceOrigin
{
    std::size_t target = 0;
    std::string sourceFaceId, sourceSurfaceId;
    std::array<double, 4> nativeDomain{};
    std::optional<std::array<double, 4>> originalNativeDomain;
    std::optional<double> nativeSurfaceDifferenceBoundMm; // Whole retained native rectangle; not a source-decimal certificate.
};
struct TargetEdgeOrigin
{
    std::size_t target = 0;
    std::optional<std::string> sourceEdgeId;
    std::optional<std::string> derivedFromFaceId;
    std::array<double, 2> basisParameterInterval{}; // Original native-basis parameter; source STEP conventions remain in parameter_mappings.
};
struct SurfacePoleOrigin
{
    std::string sourceFaceId, sourceSurfaceId;
    int axisSign = 0; // +1/-1 on the retained source sphere axis; no longitude is invented at a pole.
};
struct TargetVertexOrigin
{
    std::size_t target = 0;
    std::optional<std::string> sourceVertexId;
    std::vector<std::pair<std::string, double>> basisEdgeParameters; // Derived vertex incidences, not fabricated source entities.
    std::optional<SurfacePoleOrigin> surfacePole; // A derived target vertex, never a source vertex identity.
};
struct TargetUseSegment
{
    std::size_t edge = 0, startVertex = 0, endVertex = 0;
    std::array<double, 2> basisParameterInterval{}; // Increasing original native basis, independently of traversal sense.
};
struct SourceUseSegmentOrigin
{
    std::string sourceFaceId, sourceBoundId, sourceLoopId, sourceOrientedEdgeId, sourceEdgeId;
    bool forwardInNativeCurve = true;
    TargetUseSegment target;
};
struct SurfaceNormalizationInfo
{
    std::string sourceFaceId, sourceSurfaceId;
    std::size_t targetFaceIndex = 0;
    // Domains are umin,vmin,umax,vmax. Native UV is retained across extraction;
    // analytic degrees describe the complete native computational chart before
    // extraction, not the STEP angular parameters or a clipped source surface.
    std::array<double, 4> originalNativeDomain{}, retainedNativeDomain{}, originalAnalyticDomainDegrees{};
    // Signed generator radius in the retained torus frame; either sign is valid
    // when its whole outward interval excludes the axis. All lengths are mm.
    std::array<double, 2> generatorRadiusBoundsMm{};
    double minimumAbsoluteGeneratorRadiusMm = 0, continuousDifferenceUpperMm = 0;
    std::size_t tensorPatches = 0;
};
struct TopologyNormalizationInfo
{
    std::vector<TargetFaceOrigin> faces;
    std::vector<TargetEdgeOrigin> edges;
    std::vector<TargetVertexOrigin> vertices;
    std::vector<SourceUseSegmentOrigin> sourceUseSegments; // Owning one-to-many incidence correspondence, independent of mutable reports.
    bool sourceCurveIntervalsCovered = false, sourceVerticesPreserved = false, completed = false;
};
enum class ReportEncoding
{
    ExpandedV1,
    CompactV2
};
struct FaceOrientationEdit
{
    std::string definitionId, shapeId, faceId;
    bool expectedSourceSameSense = true, targetSameSense = false;
};
struct FaceOrientationRepairRequest
{
    std::string sourceSha256; // Exact original byte identity, never a pathname or rewritten carrier.
    std::size_t maximumFaceEdits = 0; // Required explicit discrete budget; zero is not a default allowance.
    std::size_t maximumProofControls = 0, maximumProofProducts = 0; // Workspace and request-cumulative proof work, independent of parser limits.
    std::vector<FaceOrientationEdit> edits;
};
struct FaceOrientationRepairAction
{
    FaceOrientationEdit edit;
    std::size_t targetFace = 0;
    std::array<std::array<double, 3>, 2> projection{};
    std::array<double, 4> supportDomain{};
    double firstDiagonalLower = 0, secondDiagonalLower = 0, symmetricDeterminantLower = 0;
    double continuousLiftBoundMm = 0;
    std::size_t liftCells = 0;
};
struct FaceOrientationRepairHistory
{
    std::string sourceSha256;
    std::vector<FaceOrientationRepairAction> actions;
    std::vector<step_semantics::Diagnostic> originalDiagnostics;
    std::size_t verticesCompared = 0, edgesCompared = 0, surfacesCompared = 0;
    bool definitionsIdentical = false, workingTolerancesIdentical = false;
    bool sourceInterpretationApplied = false;
};
struct DefinitionSelectionRequest
{
    std::string sourceSha256; // Bind explicit source definition IDs to the exact original file.
    std::vector<std::string> definitionIds; // Nonempty set; selected definitions are built in source order.
};
struct SourceErrorIsolationLimits
{
    std::size_t maximumFailures = 0, maximumInfluenceVisits = 0; // Explicit FaceBoundsClause profile; no production defaults.
};
struct CylindricalBandRepairRequest
{
    std::string sourceSha256;
    std::size_t maximumFaces = 0, maximumBoundEdits = 0, maximumProofOperations = 0;
    std::vector<step_semantics::CylindricalBandRepairDeferral> faces;
};
struct CylindricalBandRepairAction
{
    step_semantics::CylindricalBandRepairDeferral edit;
    std::string sourceSurfaceId;
    std::array<std::string, 2> sourceCircleIds, heightOrderedBoundIds;
};
struct CylindricalBandRepairHistory
{
    std::string sourceSha256;
    std::vector<CylindricalBandRepairAction> actions;
    std::vector<step_semantics::Diagnostic> originalSourceDiagnostics;
    std::size_t verticesCompared = 0, edgesCompared = 0, surfacesCompared = 0;
    bool definitionsIdentical = false, workingTolerancesIdentical = false, sourceInterpretationApplied = false;
};
struct DefinitionSelectionReceipt
{
    std::string sourceSha256;
    std::vector<std::string> sourceDefinitionIds, selectedDefinitionIds, excludedDefinitionIds;
    std::string configuration = "SourceCompleteNativeSelection-0.1", sourceScope = "AllDefinitionsChecked";
    std::size_t isolatedSourceFailureCount = 0;
    // sourceScope distinguishes complete source checking from selected source
    // qualification. Only explicitly selected definitions acquire native evidence;
    // isolated original failures remain in the owning source selection receipt.
};
struct Options
{
    step_source::Limits sourceLimits{};
    step_semantics::Limits semanticLimits;
    step_planar::Limits planarLimits;
    Quality quality;
    Limits nativeLimits;
    ExecutionOptions execution;
    bool generalBrep = false; // Explicit native geometry path; never implies full AP/schema conformance.
    step_semantics::SourcePolicy sourcePolicy = step_semantics::SourcePolicy::Strict;
    step_source::HeaderNamePolicy headerNamePolicy = step_source::HeaderNamePolicy::StrictAscii;
    step_source::DataStringPolicy dataStringPolicy = step_source::DataStringPolicy::StrictAscii;
    step_semantics::UnitInterpretation unitInterpretation = step_semantics::UnitInterpretation::DeclaredOnly;
    step_semantics::SchemaInterpretation schemaInterpretation = step_semantics::SchemaInterpretation::DeclaredOnly;
    bool restrictUnusedSupportDomains = false; // Explicit equivalent target representation; general development path only.
    step_source::CarrierInterpretation carrierInterpretation = step_source::CarrierInterpretation::DeclaredOnly;
    std::optional<NumericalIntegrationSettings> numericalIntegration; // Explicit general-only control; absent retains legacy 1e-4.
    bool normalizePeriodicFaces = false; // Equivalent target topology/chart only; never source repair or automatic tolerance growth.
    ReportEncoding reportEncoding = ReportEncoding::ExpandedV1; // Explicit lossless encoding; never an automatic budget fallback.
    std::optional<FaceOrientationRepairRequest> faceOrientationRepair; // Explicit zero-geometry pilot; absent remains Repair=None.
    std::optional<DefinitionSelectionRequest> definitionSelection; // General file/document entry only; never a failed-request fallback.
    std::optional<SourceErrorIsolationLimits> sourceErrorIsolation; // Requires explicit selection; isolates only proved unselected FACE.WR2 facts.
    step_semantics::ComplexMappingInterpretation complexMappingInterpretation = step_semantics::ComplexMappingInterpretation::DeclaredOnly;
    std::optional<CylindricalBandRepairRequest> cylindricalBandRepair;
    static Options PlanarFixture();
};
struct Statistics
{
    std::size_t faces = 0, edges = 0, vertices = 0;
    double areaMm2 = 0, boundaryMm = 0; // boundaryMm sums unique edge lengths, including shared edges once.
    Bounds boundsMm{};
    Point centroidMm{};
    std::optional<Point> normal; // The qualified planar path supplies normals/mm windows; general charts can omit them.
    std::optional<std::array<double, 4>> windowMm; // umin, vmin, umax, vmax; computational window, not a new trim.
    std::optional<double> materialVolumeMm3;
    std::size_t freeBoundaryEdges = 0;
    std::string shapeRole;
};
struct ClosestPoint
{
    Point pointMm{};
    double distanceMm = 0;
};
struct WireframeCurveInfo
{
    std::string sourceId, basisId;
    std::size_t nativeIndex = 0; // Detached curve index in the native part archive; not a topological edge.
    std::array<double, 2> parameterDomain{}; // Actual native evaluator parameters, not STEP analytic angles.
    bool sourceForwardInNative = true;
};
struct WireframePointInfo
{
    std::string sourceId;
    std::size_t nativeIndex = 0; // Archive point adapter BRep follows the material BRep at index nativeIndex+1.
    Point pointMm{};
};
struct WireframeInfo
{
    std::string representationId, relationshipId, setId;
    std::vector<WireframeCurveInfo> curves;
    std::vector<WireframePointInfo> points;
    Bounds boundsMm{};
};
class WireframeView
{
public:

    WireframeInfo Info() const;
    step_semantics::SchemaInterpretation AppliedSourceInterpretation() const;
    const step_source::CarrierAssumption* AppliedCarrierAssumption() const; // Borrows immutable evidence owned by this view.
    bool HasAsciiInvariantDataAssumption() const; // Snapshot-owned evidence survives releasing the import result.
    step_semantics::UnitInterpretation AppliedUnitInterpretation() const; // Source-owned dimensional interpretation evidence.
    Point EvaluateCurve(std::size_t curve, double parameter) const;
    ClosestPoint Closest(const Point& pointMm) const; // Only this source set; neither material classification nor a certified global error bound.

private:

    friend class ModelSnapshot;
    explicit WireframeView(std::shared_ptr<detail::ModelData> data, std::size_t set);
    std::shared_ptr<detail::ModelData> m_data;
    std::size_t m_set = 0;
};
struct CoordinateFrame
{
    Point originMm{}, x{}, y{}, z{}; // Orthonormal STEP build_axes; directions dimensionless.
};
struct SupportingFrameInfo
{
    std::string sourceId;
    std::size_t nativeSurfaceIndex = 0; // Explicit frame adapter in the archive, never a source surface.
    CoordinateFrame frame;
};
struct SupportingPlaneInfo
{
    std::string sourceId, positionId;
    std::size_t nativeSurfaceIndex = 0;
    CoordinateFrame frame;
    std::array<double, 4> parameterDomain{}; // Native finite sentinels, not source trims or geometric bounds.
};
struct SupportingGeometryInfo
{
    std::string representationId, relationshipId;
    // Primitive mappings share the detached archive arrays with wireframes;
    // the containing representation, not these value types, establishes role.
    std::vector<WireframeCurveInfo> curves;
    std::vector<WireframePointInfo> points;
    std::vector<SupportingFrameInfo> frames;
    std::vector<SupportingPlaneInfo> planes;
};
class SupportingGeometryView
{
public:

    SupportingGeometryInfo Info() const;
    step_semantics::SchemaInterpretation AppliedSourceInterpretation() const;
    const step_source::CarrierAssumption* AppliedCarrierAssumption() const; // Independent of the schema interpretation.
    bool HasAsciiInvariantDataAssumption() const; // Snapshot-owned evidence survives releasing the import result.
    step_semantics::UnitInterpretation AppliedUnitInterpretation() const; // Source-owned dimensional interpretation evidence.
    Point EvaluateCurve(std::size_t curve, double parameter) const;
    Point EvaluatePlane(std::size_t plane, double uMm, double vMm) const;
    ClosestPoint ClosestPlane(std::size_t plane, const Point& pointMm) const;
    // Frames are coordinate definitions, not added point/plane loci. Queries
    // explicitly select a primitive and never classify material or transform it.

private:

    friend class ModelSnapshot;
    explicit SupportingGeometryView(std::shared_ptr<detail::ModelData> data, std::size_t set);
    std::shared_ptr<detail::ModelData> m_data;
    std::size_t m_set = 0;
};
struct EditableCopyOutcome;
struct UnitNormalization;
enum class PointClass
{
    Outside,
    Interior,
    Edge,
    Vertex
};
enum class MaterialPointClass
{
    Outside,
    Inside,
    Face,
    Edge,
    Vertex,
    Indeterminate
};
class FaceView
{
public:

    std::string SourceId() const;
    std::optional<FaceOrientationRepairAction> OrientationRepair() const; // This owning face's effective orientation change, if any.
    step_semantics::SchemaInterpretation AppliedSourceInterpretation() const; // Immutable schema assumption retained by this owning view.
    const step_source::CarrierAssumption* AppliedCarrierAssumption() const; // Borrow remains valid while this view owns its source.
    bool HasAsciiInvariantDataAssumption() const; // Snapshot-owned evidence survives releasing the import result.
    step_semantics::UnitInterpretation AppliedUnitInterpretation() const; // Source-owned dimensional interpretation evidence.
    Statistics Info() const;
    std::array<double, 4> ParameterDomain() const; // Native evaluator parameters; general charts do not imply source UV or millimetres.
    Point EvaluateSurface(double u, double v) const; // Rejects domain violations before native evaluation.
    ClosestPoint Closest(const Point& pointMm) const;
    PointClass ClassifyUV(double u, double v) const;

private:

    friend class ModelSnapshot;
    explicit FaceView(std::shared_ptr<detail::ModelData> data, std::size_t face);
    std::shared_ptr<detail::ModelData> m_data;
    std::size_t m_face = 0;
};
class ModelSnapshot
{
public:

    Statistics Info() const;
    // Source face order; explicit periodic normalization groups target children
    // by source face (see TopologyNormalization). The view retains the model/context.
    FaceView Face(std::size_t index = 0) const;
    std::vector<FaceView> Faces() const;
    std::vector<WireframeView> Wireframes() const; // Separate associated source sets; no connectivity or composition inferred.
    std::vector<SupportingGeometryView> SupportingGeometry() const;
    ClosestPoint Closest(const Point& pointMm) const; // Whole selected BRep boundary; associated geometry has separate queries.
    std::optional<MaterialPointClass> ClassifyMaterial(const Point& pointMm) const; // null: surface role has no material classification.
    std::shared_ptr<const step_semantics::Snapshot> Source() const;
    std::optional<double> MaterialVolumeMm3() const; // Any surface role: NotApplicable, never a fabricated zero.
    std::optional<NumericalIntegrationObservation> NumericalIntegration() const; // Owning snapshot evidence; null for the proved planar path.
    UnitNormalization LengthUnits() const; // Value copy; source record indices refer to this snapshot's Source().
    std::optional<TopologyNormalizationInfo> TopologyNormalization() const; // Owning evidence; target indices address normalized inventory.
    std::vector<SurfaceNormalizationInfo> SurfaceNormalizations() const; // Owning original-basis support extraction, separate from repair/topology
                                                                         // changes.
    std::optional<FaceOrientationRepairHistory> Repairs() const; // Owning values, independent of the mutable outcome/report.
    std::optional<CylindricalBandRepairHistory> CylindricalBandRepairs() const;
    void WriteNative(const std::filesystem::path& path) const; // Native target archive; original STEP/normalization metadata is not encoded.
    EditableCopyOutcome CopyForModeling() const; // Complete BRep-only requests; associated geometry requires an explicit body-copy choice.
    EditableCopyOutcome CopyBrepForModeling() const; // Explicit independent selected BRep copy; associated geometry is outside this scope.

private:

    friend struct detail::Access;
    explicit ModelSnapshot(std::shared_ptr<detail::ModelData> data);
    std::shared_ptr<detail::ModelData> m_data;
};
struct ToleranceObservation
{
    std::string stage, kind, sourceId;
    double valueMm = 0; // Present observations only; an unexecuted stage has no rows.
};
struct EdgeMapping
{
    std::string edgeId, curveId, startVertexId, endVertexId, nativeMinVertexId, nativeMaxVertexId;
    bool sourceSameSense = true;
    Fraction sourceStartCoefficient, sourceEndCoefficient, sourceDirectionNormSquared;
    // Source parameter = coefficient / sqrt(norm squared); target curve parameter is [0,1].
};
struct UseMapping
{
    std::string orientedEdgeId, edgeId, startVertexId, endVertexId;
    bool forwardInNativeCurve = true, uvOwnerOnSelectedSide = false;
    std::array<double, 2> uvStartMm{}, uvEndMm{};
};
struct LoopMapping
{
    std::string faceId, boundId, loopId;
    bool outer = false, sourceOrientation = true;
    std::vector<UseMapping> uses; // Actual selected-side cyclic traversal; UV is stored on each edge's SAME member.
};
struct FaceObservation
{
    std::string faceId, planeId;
    bool sourceSameSense = true;
    Statistics native;
};
struct EdgeAdjacency
{
    std::string edgeId;
    std::vector<std::string> faceIds;
    std::size_t nativeUses = 0;
};
struct SourceShapeObservation
{
    std::string role, representationId, representationType, shapeId, shapeType, shellId, shellType;
};
// Canonical exact evidence for source factors and intermediate unit scales.
// Decimal integer strings avoid narrowing these to the native int64 ratio.
// Only the final lengthToMm ratio must fit Fraction; no Boost type is public.
class ExactUnitRatio
{
public:

    ExactUnitRatio() = default;
    // Views borrow this value, not the STEP owner. Retain the returned
    // UnitNormalization copy while using a view from one of its ratios.
    std::string_view Numerator() const noexcept
    {
        return m_numerator;
    }
    std::string_view Denominator() const noexcept
    {
        return m_denominator;
    }

private:

    friend struct detail::UnitRatioAccess;
    std::string m_numerator = "0", m_denominator = "1";
};
struct ConversionUnitDefinition
{
    std::size_t unit = 0, factor = 0, baseUnit = 0; // Source record indices, never native pointers or invented dimension entities.
    std::string valueType;
    ExactUnitRatio factorValue, scaleMm;
};
struct UnitNormalization
{
    std::string sourceLengthUnitId;
    Fraction lengthToMm;
    std::vector<step_planar::UnitConversion> definitions; // Source record indices refer to Report::source.
    std::vector<ConversionUnitDefinition> generalDefinitions; // General source dimensions remain in the typed source, including derived slots.
};
struct NativeRoleObservation
{
    std::size_t regions = 0, shells = 0, materialRegions = 0, boundedVoidRegions = 0;
    bool manifoldSolidPredicate = false; // Native topology predicate does not test IsVoid or grant material qualification.
};
struct SourceUncertainty
{
    std::size_t record = 0, unit = 0; // Indices in SourceDeclarations::semantic, not native model pointers.
    std::string valueType;
    Fraction value;
    std::optional<Fraction> unitScaleMm, valueMm;
    std::string consistencyComparison; // Within / Exceeds / Unestablished; advisory only.
};
struct SourceDeclarations
{
    std::shared_ptr<const step_semantics::Snapshot> semantic; // Full owning typed source; never retains native objects.
    std::vector<std::size_t> records; // Declaration roots and their complete outbound dependency closure, in source order.
    std::vector<SourceUncertainty> uncertainties;
};
struct RepresentationNormalization
{
    std::string kind, sourceFaceId;
    std::vector<std::string> sourceBoundIds, sourceVertexIds;
    std::optional<std::size_t> targetEdgeIndex; // Index in the private model edge mapping, not a source entity ID.
};
struct ParameterMapping
{
    std::string kind, sourceGeometryId, sourceUseId, equation;
    std::optional<std::size_t> targetEdgeIndex, targetFaceIndex;
    std::vector<std::pair<std::string, std::vector<double>>> parameters;
    std::optional<std::string> sourceBasisGeometryId; // Trim wrapper and defining basis keep distinct source identities.
    std::optional<std::size_t> targetCurveIndex; // Detached linked geometry, never an invented EDGE_CURVE.
};
struct Report;
enum class Status;
struct MaterialBodyReport
{
    std::size_t sourceShapeRecord = 0, sourceShellRecord = 0;
    bool checksPassed = false;
    std::shared_ptr<Report> checks;
};
struct ProductDefinitionReport
{
    std::size_t sourceDefinitionRecord = 0;
    bool checksPassed = false;
    std::shared_ptr<Report> checks;
};
struct Report
{
    ReportEncoding encoding = ReportEncoding::ExpandedV1;
    bool generalBrep = false;
    bool materialCollection = false;
    bool productDocument = false;
    std::optional<DefinitionSelectionRequest> definitionSelectionRequest;
    std::optional<SourceErrorIsolationLimits> sourceErrorIsolationRequest;
    std::optional<DefinitionSelectionReceipt> definitionSelection;
    std::optional<CylindricalBandRepairRequest> cylindricalBandRepairRequest;
    std::optional<CylindricalBandRepairHistory> cylindricalBandRepairs;
    std::size_t cylindricalBandProofOperations = 0;
    std::string sourceEvidenceScope;
    std::vector<ProductDefinitionReport> definitions;
    std::vector<MaterialBodyReport> bodies; // Per-body observations share the one root source report.
    std::size_t selectedBodyCount = 0; // Complete per-definition inventory when source evidence is held by a document root.
    bool supportDomainRestrictionRequested = false;
    std::optional<NumericalIntegrationObservation> numericalIntegration; // Explicit requests only; survives failure without a model.
    bool periodicNormalizationRequested = false;
    std::optional<TopologyNormalizationInfo> topologyNormalization;
    std::optional<step_semantics::Outcome> generalSource; // Source diagnostics and original records survive native failure.
    std::vector<std::pair<std::string, std::string>> classifiedOuterBounds; // Derived face/bound IDs, never source relabeling.
    std::vector<RepresentationNormalization> normalizations;
    std::vector<ParameterMapping> parameterMappings; // Derived native maps; never changes source parameters or entities.
    std::vector<SurfaceNormalizationInfo> surfaceNormalizations;
    std::vector<WireframeInfo> wireframes; // Owning values/source identities only, never native owners.
    std::vector<SupportingGeometryInfo> supportingGeometry;
    std::string sourceSha256, sourceStatus, semanticStatus, planarStatus;
    std::optional<step_semantics::SchemaSelection> sourceSchema; // Owning HEADER identity, including unsupported profile selection.
    std::shared_ptr<const step_source::SourceSnapshot> source;
    std::shared_ptr<const std::string> captured;
    std::vector<step_semantics::Diagnostic> diagnostics;
    std::vector<ToleranceObservation> tolerances;
    std::vector<EdgeMapping> edges;
    std::vector<LoopMapping> loops;
    std::vector<FaceObservation> faces;
    std::vector<EdgeAdjacency> adjacency;
    std::optional<SourceShapeObservation> sourceShape;
    std::optional<NativeRoleObservation> nativeRole;
    std::optional<UnitNormalization> units;
    std::optional<SourceDeclarations> declarations;
    std::optional<double> volumeBoundMm3, volumeJunctionBoundMm;
    std::optional<double> volumeGapFluxBoundMm3, volumeSkippedFluxBoundMm3;
    std::optional<std::size_t> solidRegions, voidCavities;
    std::optional<Statistics> native;
    std::optional<double> definitionBoundMm, consistencyBoundMm, uvBoundMm;
    std::optional<std::size_t> pointerReports, deepReports;
    std::string kernelPath;
    std::size_t nativeCalls = 0;
    bool candidateDiscarded = false;
    Phase lastBoundary = Phase::NotStarted;
    std::size_t checkpoints = 0;
    std::optional<std::size_t> maximumObservedPrivateBytes, processPeakCommitBytes;
    std::optional<std::size_t> processPrivateLimitBytes, hardMemoryLimitBytes;
    bool deadlineConfigured = false;
    std::string json; // Mandatory bounded report, completed before a model is published.
    std::shared_ptr<const step_source::CarrierAssumption> carrierAssumption; // Also survives parsing failure without a record graph.
    std::optional<FaceOrientationRepairRequest> repairRequest;
    std::optional<FaceOrientationRepairHistory> repairs;
    std::shared_ptr<Report> originalAttempt; // No native owners; source/evidence live at the complete request root.
    std::optional<Status> originalAttemptStatus;
    std::size_t repairProofProducts = 0;
    std::string repairStage;
    std::size_t repairEditsAttempted = 0;
};
enum class Status
{
    Accepted,
    Rejected,
    Unsupported,
    Deferred,
    Indeterminate,
    ResourceLimit,
    BuildFailure,
    PostcheckFailure,
    SourceFailure,
    Cancelled,
    AcceptedUnderInterpretation, // Complete native result with an assumed source schema or carrier; distinct from ordinary acceptance.
    AcceptedWithRepairs, // Explicit repaired qualification; source assumptions remain separately owned/reported.
    AcceptedWithSourceIsolation // Complete selected request, with separately owned excluded-source failures and locality evidence.
};
struct Outcome
{
    Status status = Status::BuildFailure;
    std::shared_ptr<const ModelSnapshot> model;
    Report report;
};
class MaterialCollectionSnapshot
{
public:

    std::size_t BodyCount() const;
    std::shared_ptr<const ModelSnapshot> Body(std::size_t index) const;
    std::string SourceShapeId(std::size_t index) const;
    std::shared_ptr<const step_semantics::Snapshot> Source() const;
    // Ordered independent BReps, with an empty CSG tree. No union/qualification transfer is implied.
    void WriteNative(const std::filesystem::path& path) const;

private:

    friend struct detail::Access;
    explicit MaterialCollectionSnapshot(std::shared_ptr<detail::CollectionData> data);
    std::shared_ptr<detail::CollectionData> m_data;
};
struct CollectionOutcome
{
    Status status = Status::BuildFailure;
    std::shared_ptr<const MaterialCollectionSnapshot> collection;
    Report report;
};
using PartModel = std::variant<std::monostate, std::shared_ptr<const ModelSnapshot>, std::shared_ptr<const MaterialCollectionSnapshot>>;
class DocumentSnapshot
{
public:

    std::size_t PartCount() const;
    PartModel Part(std::size_t index) const;
    std::size_t BodyCount() const;
    std::shared_ptr<const ModelSnapshot> Body(std::size_t index) const;
    std::size_t BodyPartIndex(std::size_t index) const;
    std::string SourceDefinitionId(std::size_t bodyIndex) const;
    std::string SourceShapeId(std::size_t bodyIndex) const;
    std::shared_ptr<const step_semantics::ProductDocument> Source() const;
    std::optional<DefinitionSelectionReceipt> Selection() const; // Owns scope; Source retains original bytes/inventory, with possibly selected-only
                                                                 // Part views.
    void WriteNative(const std::filesystem::path& path) const; // Ordered definitions/bodies; no union or source-qualification transfer.

private:

    friend struct detail::Access;
    explicit DocumentSnapshot(std::shared_ptr<detail::DocumentData> data);
    std::shared_ptr<detail::DocumentData> m_data;
};
struct DocumentOutcome
{
    Status status = Status::BuildFailure;
    std::shared_ptr<const DocumentSnapshot> document;
    Report report;
};
struct PartOutcome
{
    Status status = Status::BuildFailure;
    PartModel part;
    Report report;
};
// Complete general-source request. Definitions retain separate local frames;
// this variant does not imply assembly placement or partial acceptance.
using FileModel = std::variant<
    std::monostate,
    std::shared_ptr<const ModelSnapshot>,
    std::shared_ptr<const MaterialCollectionSnapshot>,
    std::shared_ptr<const DocumentSnapshot>>;
struct FileOutcome
{
    Status status = Status::BuildFailure;
    FileModel model;
    Report report;
};
class EditableBrep
{
public:

    ~EditableBrep();
    // Borrowed only during this serialized callback. Do not delete/consume the root or retain native references.
    // Edits have no imported-model quality qualification and cannot affect the original snapshot/report.
    void Edit(const std::function<void(SmBrep&)>& edit);

private:

    friend class ModelSnapshot;
    explicit EditableBrep(std::unique_ptr<detail::EditableData> data);
    std::unique_ptr<detail::EditableData> m_data;
};
struct EditableCopyOutcome
{
    Status status = Status::BuildFailure;
    std::unique_ptr<EditableBrep> model;
    std::string errorKey, message;
};
Outcome ImportBytes(std::string bytes, const Options& options);
Outcome ImportFile(const std::filesystem::path& path, const Options& options);
// Explicit whole material collection. Every selected MSB must pass; no partial body result.
CollectionOutcome ImportMaterialCollectionBytes(std::string bytes, const Options& options);
CollectionOutcome ImportMaterialCollectionFile(const std::filesystem::path& path, const Options& options);
// Canonical one-part dispatch: source is read/checked once, then single or collection
// ownership is selected from the checked inventory. Accepted results hold exactly one alternative.
PartOutcome ImportPartFile(const std::filesystem::path& path, const Options& options);
PartOutcome ImportPartBytes(std::string bytes, const Options& options);
DocumentOutcome ImportDocumentFile(const std::filesystem::path& path, const Options& options);
DocumentOutcome ImportDocumentBytes(std::string bytes, const Options& options);
// Capture/check once, then dispatch from the qualified definition/body inventory.
// A single definition keeps its associated geometry and existing owning type.
FileOutcome ImportModelFile(const std::filesystem::path& path, const Options& options);
FileOutcome ImportModelBytes(std::string bytes, const Options& options);
// Invalid options throw std::invalid_argument; allocation failure is not a qualified recoverable outcome.
const char* StatusName(Status status);
const char* MaterialPointClassName(MaterialPointClass classification);
const char* PhaseName(Phase phase);
} // namespace step_import
