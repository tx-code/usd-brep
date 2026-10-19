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
struct EditableData;
struct Access;
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
    BeforeCommit
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
    step_semantics::SchemaInterpretation schemaInterpretation = step_semantics::SchemaInterpretation::DeclaredOnly;
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
    step_semantics::SchemaInterpretation AppliedSourceInterpretation() const; // Immutable source assumption retained by this owning view.
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
    FaceView Face(std::size_t index = 0) const; // Source face order; owning view retains the model and context.
    std::vector<FaceView> Faces() const;
    std::vector<WireframeView> Wireframes() const; // Separate associated source sets; no connectivity or composition inferred.
    std::vector<SupportingGeometryView> SupportingGeometry() const;
    ClosestPoint Closest(const Point& pointMm) const; // Whole selected BRep boundary; associated geometry has separate queries.
    std::optional<MaterialPointClass> ClassifyMaterial(const Point& pointMm) const; // null: surface role has no material classification.
    std::shared_ptr<const step_semantics::Snapshot> Source() const;
    std::optional<double> MaterialVolumeMm3() const; // Any surface role: NotApplicable, never a fabricated zero.
    void WriteNative(const std::filesystem::path& path) const; // Native archive, analytic definitions retained; no STEP re-encoding.
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
struct UnitNormalization
{
    std::string sourceLengthUnitId;
    Fraction lengthToMm;
    std::vector<step_planar::UnitConversion> definitions; // Source record indices refer to Report::source.
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
struct Report
{
    bool generalBrep = false;
    std::optional<step_semantics::Outcome> generalSource; // Source diagnostics and original records survive native failure.
    std::vector<std::pair<std::string, std::string>> classifiedOuterBounds; // Derived face/bound IDs, never source relabeling.
    std::vector<RepresentationNormalization> normalizations;
    std::vector<ParameterMapping> parameterMappings; // Derived native maps; never changes source parameters or entities.
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
    AcceptedUnderInterpretation // Complete native result, unverified source binding; distinct from ordinary acceptance.
};
struct Outcome
{
    Status status = Status::BuildFailure;
    std::shared_ptr<const ModelSnapshot> model;
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
// Invalid options throw std::invalid_argument; allocation failure is not a qualified recoverable outcome.
const char* StatusName(Status status);
const char* MaterialPointClassName(MaterialPointClass classification);
const char* PhaseName(Phase phase);
} // namespace step_import
