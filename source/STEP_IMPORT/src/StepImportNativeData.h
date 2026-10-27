// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"
#include "StepImportCurvePointDefinitions.h"

#include <SmBrep.h>
#include <SmContext.h>
#include <SmCurve.h>
#include <SmSurface.h>
#include <mutex>

class SmEdge;
class SmFace;
class SmLine;
class SmLoop;
class SmPlane;
class SmRegion;
class SmShell;
class SmVertex;

namespace step_import::detail
{
struct SplineChartSession;
struct PcurveLineSession;
std::recursive_mutex& NativeMutex();
inline bool HasAppliedSourceInterpretation(const step_semantics::Snapshot& source)
{
    return source.AppliedInterpretation() != step_semantics::SchemaInterpretation::DeclaredOnly || source.Source().AppliedCarrierAssumption() ||
           source.Source().HasAsciiInvariantDataAssumption() ||
           source.AppliedUnitInterpretation() != step_semantics::UnitInterpretation::DeclaredOnly || !source.ComplexMappingAssumptions().empty();
}
struct NativeFaceData
{
    SmFace* face = nullptr;
    SmPlane* plane = nullptr; // Only the legacy planar verifier uses this narrower view.
    std::vector<SmLoop*> loops;
    std::string faceId;
    Statistics info;
};
struct ModelData
{
    struct DerivedVertexConnection
    {
        std::size_t target = 0;
        std::string sourceEdgeId;
        double parameter = 0, observedGapMm = 0;
        Fraction maximumGapMm;
    };
    ~ModelData(); // Native destruction is serialized; BRep must precede its allocating context.
    std::unique_ptr<SmContext> context;
    std::unique_ptr<SmBrep> brep;
    std::shared_ptr<const step_semantics::Snapshot> source;
    std::vector<NativeFaceData> faces;
    std::vector<SmVertex*> vertices;
    std::vector<SmEdge*> edges;
    std::vector<SmLine*> curves; // Legacy affine verifier only; native edges own all defining curves.
    OwnedCurvePointDefinitions curvePointDefinitions; // Shared source definitions, never material topology; clear before context under NativeMutex.
    std::vector<WireframeInfo> wireframes;
    // Includes canonical plane adapters for coordinate frames. No adapter is
    // inserted into the material BRep or presented as a source PLANE.
    std::vector<std::unique_ptr<SmSurface>> supportingSurfaces;
    std::vector<SupportingGeometryInfo> supportingGeometry;
    // Detached original analytic/native UV definitions; clear under NativeMutex before context.
    std::vector<std::unique_ptr<SmSurface>> retainedSupportDefinitions;
    std::vector<SurfaceNormalizationInfo> surfaceNormalizations;
    std::vector<RevolutionChartInfo> revolutionCharts;
    std::shared_ptr<const SplineChartSession> splineChartEvidence;
    std::shared_ptr<const PcurveLineSession> pcurveLineEvidence;
    std::vector<RevolutionPoleVisitInfo> revolutionPoleVisits;
    std::vector<PcurveCarrierPrecisionInfo> pcurveCarrierPrecisions;
    SmRegion* material = nullptr;
    SmRegion* bounded = nullptr;
    SmShell* outside = nullptr;
    SmShell* inside = nullptr;
    double queryTolerance = 0;
    double definitionToleranceMm = 0; // General defining-geometry budget, also bounds linked archive readback.
    Fraction queryBudget;
    std::optional<NumericalIntegrationObservation> numericalIntegration;
    std::optional<UnitNormalization> lengthUnits; // Independent of the caller's mutable report and output lifetime.
    std::optional<TopologyNormalizationInfo> topologyNormalization;
    std::vector<DerivedVertexConnection> derivedVertexConnections; // Owning final evidence; never transfers to modeling copies/native files.
    std::optional<FaceOrientationRepairHistory> repairs;
    std::optional<CylindricalBandRepairHistory> cylindricalBandRepairs;
    std::optional<ConicalApexRepairHistory> conicalApexRepairs;
    std::shared_ptr<const TerminalDomainInterpretationReceipt> terminalDomainInterpretation;
    std::shared_ptr<const SurfaceCurve3dInterpretationReceipt> surfaceCurve3dInterpretation;
    std::shared_ptr<const TargetSurfacePrecisionReceipt> targetSurfacePrecision;
    std::vector<std::size_t> repairVertexSources, repairEdgeSources; // Only private repair trials retain these comparison inventories.
    std::vector<SmEdge*> targetEdges; // Populated by explicit topology normalization; original source arrays remain separate.
    std::vector<SmVertex*> targetVertices;
    std::vector<std::size_t> targetFaceSources; // Source face ordinal for every target face, including source-to-many children.
    std::vector<std::size_t> targetEdgeOwners; // Source record for execution/diagnostics, also for derived cuts.
    std::vector<std::size_t> targetVertexOwners;
    Statistics info;
    mutable std::mutex mutex;
};
struct CollectionData
{
    // No native context at this level. Each body owns its context and destruction lock.
    std::shared_ptr<const step_semantics::Snapshot> source;
    std::vector<std::shared_ptr<ModelData>> bodies;
};
struct DocumentData
{
    struct Body
    {
        std::size_t part, definition, shape;
        std::shared_ptr<ModelData> model;
    };
    std::shared_ptr<const step_semantics::ProductDocument> source;
    std::optional<DefinitionSelectionReceipt> selection;
    std::vector<std::size_t> sourcePartIndices; // Qualified source-view ordinals, distinct from declared inventory and native ordinals.
    std::vector<PartModel> parts;
    std::vector<Body> bodies;
};
void WriteWireframeArchive(const ModelData& model, const std::filesystem::path& path);
} // namespace step_import::detail
