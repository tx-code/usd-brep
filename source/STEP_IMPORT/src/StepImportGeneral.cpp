// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportCircleVertexCorrection.h"
#include "StepImportConicalApexRepair.h"
#include "StepImportConicalApexSession.h"
#include "StepImportConicalVertexLoop.h"
#include "StepImportCurvePointConstruction.h"
#include "StepImportCylinderChart.h"
#include "StepImportCylinderSeamPlan.h"
#include "StepImportCylindricalBandRepair.h"
#include "StepImportDerivedUv.h"
#include "StepImportExecution.h"
#include "StepImportGeneralMeasurements.h"
#include "StepImportGeneralRole.h"
#include "StepImportGeometryConstruction.h"
#include "StepImportNativeBuild.h"
#include "StepImportNativeData.h"
#include "StepImportOrientationRepair.h"
#include "StepImportPcurveConstruction.h"
#include "StepImportPeriodicDiagnosis.h"
#include "StepImportPeriodicNormalization.h"
#include "StepImportRevolutionChart.h"
#include "StepImportRevolutionConstruction.h"
#include "StepImportRevolutionPole.h"
#include "StepImportRingSegmentation.h"
#include "StepImportSourceParameterUnits.h"
#include "StepImportSplineSurfaceConstruction.h"
#include "StepImportSupportDomain.h"
#include "StepImportSurfaceCurveSession.h"
#include "StepImportSurfacePrecisionSession.h"
#include "StepImportTrimMeasurement.h"
#include "StepImportUnits.h"

#include <SmApiQueries.h>
#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmBrepData.h>
#include <SmCircle.h>
#include <SmCone.h>
#include <SmCrvOnSurf.h>
#include <SmCurveClass.h>
#include <SmCylinder.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmEllipse.h>
#include <SmFace.h>
#include <SmFaceProps.h>
#include <SmHealProbArray.h>
#include <SmLine.h>
#include <SmLoop.h>
#include <SmLoopProps.h>
#include <SmPlane.h>
#include <SmRegion.h>
#include <SmSphere.h>
#include <SmTol.h>
#include <SmTorus.h>
#include <SmVertex.h>
#include <algorithm>
#include <cfenv>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <set>

// This path constructs defining geometry directly from a typed source graph.
// It deliberately does not inherit the planar profile's exact region/volume
// proofs. Native topology, measured projection gaps and independent controls
// qualify a development import, not full EXPRESS or global geometric validity.
namespace step_import::detail
{
namespace
{
using namespace construction;
struct Curve
{
    Index record = 0, geometry = 0, v0 = 0, v1 = 0;
    bool same = true;
    std::unique_ptr<SmCurve> native;
    double t0 = 0, t1 = 0;
    Bounds bounds{};
    V p0{}, p1{}; // Evaluated endpoints of the defining curve, not the possibly gapped topology vertices.
    bool forwardForComparison = true; // Private direction label: equal flags mean increasing t follows the same physical sense.
    std::size_t associationOccurrence = 0; // 1-based LIST position; zero denotes the separate curve_3d comparison.
};
struct Use
{
    Index edge, source;
    bool forward;
    Index face = 0, bound = 0, loop = 0;
};
struct Loop
{
    Index record, bound;
    bool outer, orientation;
    std::vector<Use> uses;
    std::optional<Index> vertex; // VERTEX_LOOP is a point use, never an empty PATH.
};
struct Face
{
    Index record, surface;
    bool same;
    std::vector<Loop> loops;
};
struct SeamPlan
{
    Index v0, v1;
    V a, b;
    double angle;
    Index firstEdge = 0, secondEdge = 0;
    bool mixedBoundary = false;
    bool separatedSourceVertices = false;
};

struct SpherePolePlan
{
    Index baseVertex;
    Frame frame;
    V pole, radial, toward;
    double radius, latitudeDegrees, longitude;
    int sign;
};
struct DerivedPole
{
    SmVertex* vertex;
    Index face;
    int sign;
    V expectedPoint;
};

class GeneralBuilder : private GeometryConstruction
{
public:

    GeneralBuilder(
        std::shared_ptr<const step_semantics::Snapshot> source,
        const Options& options,
        Report& report,
        Execution& execution,
        std::optional<step_semantics::Part::Shape> binding = {},
        bool captureRepairFailure = false,
        FaceSenseOverrides overrides = {},
        VertexPointOverrides pointOverrides = {}
    )
        : GeometryConstruction(std::move(source), options, report, execution),
          m_model(std::make_shared<ModelData>()),
          m_binding(std::move(binding)),
          m_captureRepairFailure(captureRepairFailure),
          m_faceSenseOverrides(std::move(overrides)),
          m_pointOverrides(std::move(pointOverrides))
    {
        m_model->source = m_source;
        m_model->terminalDomainInterpretation = m_execution.TerminalDomainInterpretation();
        m_report.terminalDomainInterpretation = m_model->terminalDomainInterpretation;
        if (auto* choices = m_execution.SurfaceCurves())
        {
            m_model->surfaceCurve3dInterpretation = choices->Evidence();
            m_report.surfaceCurve3dInterpretation = m_model->surfaceCurve3dInterpretation;
        }
        if (auto* precisions = m_execution.TargetSurfaces())
        {
            m_model->targetSurfacePrecision = precisions->Evidence();
            m_report.targetSurfacePrecision = m_model->targetSurfacePrecision;
        }
        m_model->queryTolerance = Q(m_options.quality.queryMm);
        m_model->definitionToleranceMm = Q(m_options.quality.definitionMm);
        m_model->queryBudget = m_options.quality.queryMm;
    }
    [[nodiscard]] ConstructionResult<NativeCandidate> Run()
    {
        if (!std::numeric_limits<double>::is_iec559 || std::fegetround() != FE_TONEAREST)
        {
            return tl::unexpected(
                Failure{ Status::Indeterminate, "UnsupportedNativeNumericEnvironment", "Nearest binary64 environment required", {}, "GeneralBrep" }
            );
        }
        // Source role and unsupported option combinations are settled before
        // claiming a repair plan or allocating any native geometry.
        if (auto operation = PrepareRole(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        auto nativeValue1 = PrepareCylindricalBandRepair(m_source, m_binding, m_options, m_report, m_execution);
        if (!nativeValue1)
        {
            return tl::unexpected(std::move(nativeValue1.error()));
        }
        m_cylindricalBandTrial = std::move(*nativeValue1);
        if (auto operation = Prepare(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (!m_pointOverrides.empty())
        {
            // Repair changes only target vertex positions. Keep a separate
            // original table for source-incidence proofs and cone support bounds;
            // using corrected points there could silently redefine the surface.
            m_originalPoints = points;
            std::set<Index> changed;
            for (const auto& [record, point] : m_pointOverrides)
            {
                const auto found = vi.find(record);
                if (auto check = Need(found != vi.end() && changed.insert(record).second, "ConicalApexRepairOverrideBinding", record); !check)
                {
                    return tl::unexpected(std::move(check.error()));
                }
                points[found->second] = point;
            }
        }
        if (auto verified = VerifySurfaceCurveInterpretationScope(); !verified)
        {
            return tl::unexpected(std::move(verified.error()));
        }
        if (auto operation = Step(shape); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        m_model->context = std::make_unique<SmContext>();
        m_context = m_model->context.get();
        // All native candidates belong to this context and remain private until
        // the caller's existing bounded-report and cancellation commit point.
        SmBrepConstructor c;
        if (auto operation = Step(shape); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        m_model->brep.reset(c.StartBrep(*m_model->context));
        if (auto businessCheck = Need(bool(m_model->brep), "GeneralBrepAllocation", shape); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        m_model->brep->SetTolerance(Tol(), FALSE);
        for (auto& curve : curves)
        {
            if (auto operation = MakeCurve(curve); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
        }
        if (m_options.cylindricalBandRepair && m_options.cylindricalBandRepair->circleVertices)
        {
            std::vector<CircleVertexCurveView> views;
            views.reserve(curves.size());
            for (const auto& curve : curves)
            {
                views.push_back(
                    { curve.record,
                      curve.geometry,
                      vertices.at(curve.v0),
                      vertices.at(curve.v1),
                      curve.native.get(),
                      curve.p0,
                      curve.p1,
                      curve.t0,
                      curve.t1 }
                );
            }
            auto corrected = CorrectCircleVertices(
                *m_source,
                m_binding ? *m_binding : m_source->PartDefinition().shapes.at(0),
                m_options,
                vertices,
                points,
                views,
                m_execution,
                m_report
            );
            if (!corrected)
            {
                return tl::unexpected(std::move(corrected.error()));
            }
            m_circleVertexCorrections = std::move(*corrected);
        }
        if (auto operation = PrepareRingSubdivisions(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto operation = PrepareVertexLoopCuts(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        for (Index i = 0; i < vertices.size(); ++i)
        {
            if (auto operation = Step(vertices[i]); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            auto v = c.StartVertex(P(points[i]));
            if (auto businessCheck = Need(v != nullptr, "GeneralVertexAllocation", vertices[i]); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            v->SetTolerance(Tol(), FALSE);
            m_model->vertices.push_back(v);
            if (auto operation = Step(vertices[i]); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(c.EndVertex()); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
        }
        for (auto& cut : m_ringVertices)
        {
            if (auto operation = Step(curves[cut.curve].record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            cut.native = c.StartVertex(P(cut.point));
            if (auto businessCheck = Need(cut.native != nullptr, "RingSubdivisionVertexAllocation", curves[cut.curve].record); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            cut.native->SetTolerance(Tol(), FALSE);
            if (auto operation = Step(curves[cut.curve].record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(c.EndVertex()); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
        }
        if (IsClosedBoundary(m_bodyRole))
        {
            if (auto operation = Step(shape); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            m_model->bounded = c.StartRegion(IsMaterial(m_bodyRole) ? TRUE : FALSE, nullptr);
            if (auto businessCheck = Need(m_model->bounded != nullptr, "GeneralRegionAllocation", shape); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            // StartRegion's bool is ignored by the kernel. Void is an explicit
            // source-role policy, never inferred from native closure predicates.
            m_model->bounded->SetIsVoid(IsMaterial(m_bodyRole) ? FALSE : TRUE);
            m_model->material = IsMaterial(m_bodyRole) ? m_model->bounded : nullptr;
        }
        if (auto operation = Step(shell); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto nativeStatusCheck = Good(c.StartShell(m_model->outside, m_model->inside)); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        m_model->edges.resize(curves.size());
        for (auto& f : faces)
        {
            if (auto operation = BuildFace(c, f); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
        }
        if (auto operation = Step(shell); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto nativeStatusCheck = Good(c.EndShell(FALSE)); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        } // No implicit stitching or source repair.
        if (IsClosedBoundary(m_bodyRole))
        {
            if (auto operation = Step(shape); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(c.EndRegion()); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
        }
        if (auto operation = Step(shape); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto nativeStatusCheck = Good(c.EndBrep()); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        if (auto businessCheck = PrepareRingInventory(); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = PreparePoleInventory(); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (!m_circleVertexCorrections.empty())
        {
            std::vector<CircleVertexSurfaceView> surfaces;
            for (std::size_t i = 0; i < faces.size(); ++i)
            {
                CircleVertexSurfaceView view{ faces[i].record, m_model->faces.at(i).face->GetSurface(), {} };
                for (const auto& loop : faces[i].loops)
                {
                    for (const auto& use : loop.uses)
                    {
                        if (auto work = m_execution.SpendCylindricalBandProof(); !work)
                        {
                            return tl::unexpected(std::move(work.error()));
                        }
                        const auto& edge = curves[use.edge];
                        view.sourceVertexRecords.push_back(vertices[edge.v0]);
                        view.sourceVertexRecords.push_back(vertices[edge.v1]);
                    }
                }
                surfaces.push_back(std::move(view));
            }
            if (auto protectedResult =
                    ProtectCircleVerticesFromSurfaces(*m_source, m_options, surfaces, m_circleVertexCorrections, m_execution, m_report);
                !protectedResult)
            {
                return tl::unexpected(std::move(protectedResult.error()));
            }
            if (auto recorded = RecordCircleVertexCorrections(
                    m_cylindricalBandTrial,
                    std::move(m_circleVertexCorrections),
                    m_execution.CircleVertexBudget()
                );
                !recorded)
            {
                return tl::unexpected(std::move(recorded.error()));
            }
        }
        if (auto operation = CaptureCylindricalBandDefinitions(m_cylindricalBandTrial, m_model, m_report, m_execution); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto operation = InstallUv(); !operation)
        {
            if (m_captureRepairFailure && m_apexRepairFailure && m_options.conicalApexRepair)
            {
                RememberRepairSources();
                return tl::unexpected(ConicalApexFailureCandidate{ std::move(operation.error()), m_model });
            }
            return tl::unexpected(std::move(operation.error()));
        }
        if (m_options.restrictUnusedSupportDomains)
        {
            if (auto operation = RestrictSupports(); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
        }
        if (m_options.normalizePeriodicFaces)
        {
            std::vector<Index> sourceFaces, sourceEdges;
            for (const auto& face : faces)
            {
                sourceFaces.push_back(face.record);
            }
            for (const auto& edge : curves)
            {
                sourceEdges.push_back(edge.record);
            }
            if (auto operation = NormalizePeriodicTopology(
                    *m_model,
                    sourceFaces,
                    sourceEdges,
                    vertices,
                    derivedEdgeFaces,
                    m_options,
                    m_report,
                    [&](Index record)
                    {
                        return Step(record);
                    }
                );
                !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
        }
        if (auto operation = Validate(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto businessCheck = ObserveTolerances(); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto operation = Measure(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (wireframeRepresentation || supportingRepresentation)
        {
            // Local ownership unwinds before m_model/context on every failure.
            CurvePointDefinitions definitions(*this);
            if (auto operation = BuildWireframes(definitions); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto operation = BuildSupportingGeometry(definitions); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            m_model->curvePointDefinitions = std::move(definitions).Finish();
        }
        if (m_rebuiltUv || m_model->topologyNormalization)
        {
            // Validators and measured queries can fill/fix UV caches. Check
            // the final private candidate after those consumers, before return.
            if (auto operation = VerifyRebuiltUv(); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
        }
        if (!derivedPoles.empty() || !m_vertexLoopCuts.empty())
        {
            if (auto operation = VerifyPoleInventory(); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (m_model->topologyNormalization->completed)
            {


                // The periodic pass may split source edges. Its checked union
                // of original-basis child intervals replaces pointer/interval
                // identity; do not dereference pre-split edge handles here.
                if (auto businessCheck = Need(
                        m_model->topologyNormalization->sourceCurveIntervalsCovered,
                        "SphericalPoleSourceCoverageUnestablished",
                        shape
                    );
                    !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
            }
            else
            {
                for (Index i = 0; i < curves.size(); ++i)
                {
                    if (m_ringNormalization && m_ringSubdivisions.contains(i))
                    {
                        // The representative is only one child. The existing
                        // final shared-ring ledger proves the COMPLETE interval
                        // union and every source use before publication below.
                        continue;
                    }
                    if (auto operation = Step(curves[i].record); !operation)
                    {
                        return tl::unexpected(std::move(operation.error()));
                    }
                    const auto interval = m_model->edges[i]->GetInterval();
                    if (auto businessCheck = Need(
                            interval.GetMin() == curves[i].t0 && interval.GetMax() == curves[i].t1,
                            "SphericalPoleSourceIntervalChanged",
                            curves[i].record
                        );
                        !businessCheck)
                    {
                        return tl::unexpected(std::move(businessCheck.error()));
                    }
                }
            }
            for (Index i = 0; i < vertices.size(); ++i)
            {
                if (auto operation = Step(vertices[i]); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (auto businessCheck = Need(Norm(Sub(P(m_model->vertices[i]->GetPoint()), points[i])) == 0, "GeneralSourceVertexMoved", vertices[i]);
                    !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
            }
            for (const auto& pole : derivedPoles)
            {
                if (auto operation = Step(faces[pole.face].record); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (auto businessCheck = Need(
                        Norm(Sub(P(pole.vertex->GetPoint()), pole.expectedPoint)) == 0,
                        "SphericalPoleVertexMoved",
                        faces[pole.face].record
                    );
                    !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
            }
            m_model->topologyNormalization->sourceVerticesPreserved = m_pointOverrides.empty();
            if (!m_ringNormalization)
            {
                m_model->topologyNormalization->sourceCurveIntervalsCovered = true;
                m_model->topologyNormalization->completed = true;
            }
            m_report.topologyNormalization = m_model->topologyNormalization;
        }
        if (m_captureRepairFailure || !m_faceSenseOverrides.empty() || !m_pointOverrides.empty())
        {
            RememberRepairSources();
        }
        if (auto operation = CompleteRingSubdivisions(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto operation = CompleteCylindricalBandRepair(m_cylindricalBandTrial, *m_model, m_report, m_execution); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        for (const auto& [record, verify] : m_revolutionChartChecks)
        {
            // Queries/validators can materialize or refine derived data. The
            // exact chart witness must still hold after every native consumer,
            // in addition to the independently repeated physical lift check.
            if (auto operation = Step(record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (!verify())
            {
                return tl::unexpected(Fail("GeneralRevolutionChartIdentityLost", record, Status::PostcheckFailure));
            }
        }
        if (auto observed = RecordFinalRevolutionPoleVisits(); !observed)
        {
            return tl::unexpected(std::move(observed.error()));
        }
        if (!m_splineCharts->bindings.empty())
        {
            auto verified = VerifySplineChartBindings(
                *m_splineCharts,
                *m_context,
                [&](Index record)
                {
                    return SplineChartCallbacks{ [&, record]
                                                 {
                                                     return Step(record);
                                                 },
                                                 [&, record](std::size_t work)
                                                 {
                                                     return m_execution.SpendExactGeometryWork(m_report, work, record);
                                                 } };
                }
            );
            if (!verified)
            {
                return tl::unexpected(std::move(verified.error()));
            }
            // No mutable report/cache object becomes the snapshot's evidence.
            // Raw native links remain borrowed from this same owning ModelData.
            m_model->splineChartEvidence = std::make_shared<const SplineChartSession>(*m_splineCharts);
        }
        if (!m_pcurveLines->bindings.empty())
        {
            auto verified = VerifyPcurveLineBindings(
                *m_pcurveLines,
                *m_context,
                [&](Index record)
                {
                    return SplineChartCallbacks{ [&, record]
                                                 {
                                                     return Step(record);
                                                 },
                                                 [&, record](std::size_t work)
                                                 {
                                                     return m_execution.SpendExactGeometryWork(m_report, work, record);
                                                 } };
                }
            );
            if (!verified)
            {
                return tl::unexpected(std::move(verified.error()));
            }
            m_model->pcurveLineEvidence = std::make_shared<const PcurveLineSession>(*m_pcurveLines);
        }
        for (const auto action : m_surfaceCurveActions)
        {
            if (auto checked = m_execution.SurfaceCurves()->MarkNativeChecked(action); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
        }
        for (const auto& precision : m_report.pcurveCarrierPrecisions)
        {
            if (precision.targetEdgeIndex)
            {
                m_model->pcurveCarrierPrecisions.push_back(precision);
            }
        }
        for (const auto& [action, face] : m_targetSurfacePrecisionActions)
        {
            // Recheck the actual owning surface after every native consumer.
            // A prior setter, property mask or report is not final evidence.
            if (auto operation = Step(shape); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto checked = m_execution.TargetSurfaces()->Verify(action, *face); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
        }
        return m_model;
    }

private:

    std::shared_ptr<ModelData> m_model;
    std::vector<std::pair<std::size_t, SmFace*>> m_targetSurfacePrecisionActions;
    std::set<Index> m_pendingToroidalSupports;
    std::map<Index, const SmTorus*> m_toroidalOriginals;
    std::shared_ptr<CylindricalBandRepairTrial> m_cylindricalBandTrial;
    std::vector<CircleVertexCorrectionEvidence> m_circleVertexCorrections;
    std::optional<step_semantics::Part::Shape> m_binding;
    GeneralBodyRole m_bodyRole = GeneralBodyRole::MaterialSolid;
    std::vector<GeneralRoleFace> m_roleFaces;
    std::vector<GeneralRoleEdge> m_roleEdges;
    Index shape = 0, shell = 0;
    std::vector<Index> vertices;
    std::vector<V> points;
    std::map<Index, Index> vi, ei;
    std::vector<Curve> curves;
    std::vector<Face> faces;
    double consistency = 0, uvGap = 0;
    bool m_rebuiltUv = false;
    std::vector<std::pair<Index, std::function<bool()>>> m_revolutionChartChecks;
    std::map<Index, std::size_t> m_surfaceCurveChoices; // Source EC record -> immutable interpretation plan.
    std::vector<std::size_t> m_surfaceCurveActions; // Finalize only after every native consumer and witness.
    bool m_captureRepairFailure = false;
    FaceSenseOverrides m_faceSenseOverrides;
    VertexPointOverrides m_pointOverrides;
    std::vector<Point> m_originalPoints;
    bool m_apexRepairFailure = false;
    const Point& OriginalPoint(Index vertex) const
    {
        return m_originalPoints.empty() ? points.at(vertex) : m_originalPoints.at(vertex);
    }
    struct RingVertex
    {
        Index curve;
        double parameter;
        V point;
        SmVertex* native = nullptr;
        bool consistencyConnection = false;
    };
    // Private full-basis copies die before m_model and its allocating context.
    std::map<Index, RingSubdivision> m_ringSubdivisions;
    std::map<Index, SeamPlan> m_plannedSeams;
    std::vector<RingVertex> m_ringVertices;
    std::vector<Index> m_targetCurveSources;
    std::map<SmEdge*, Index> m_ringEdgeOrdinals;
    std::vector<SourceUseSegmentOrigin> m_ringUseOrigins;
    bool m_ringNormalization = false;
    void RememberRepairSources() const
    {
        m_model->repairVertexSources = vertices;
        m_model->repairEdgeSources.clear();
        for (const auto& curve : curves)
        {
            m_model->repairEdgeSources.push_back(curve.record);
        }
    }
    std::vector<Index> derivedEdgeFaces;
    std::vector<DerivedPole> derivedPoles;
    struct VertexLoopPlan
    {
        Index ringBound, pointBound, pointLoop, baseVertex, poleVertex;
        ConicalVertexLoopGeometry geometry;
    };
    std::map<Index, VertexLoopPlan> m_vertexLoopPlans;
    std::vector<TopologyNormalizationInfo::VertexLoopCut> m_vertexLoopCuts;
    std::optional<Index> wireframeRepresentation;
    std::optional<Index> supportingRepresentation;
    Index SourceFaceIndex(Index target) const
    {
        return m_model->topologyNormalization ? m_model->targetFaceSources.at(target) : target;
    }
    const std::vector<SmEdge*>& TargetEdges() const
    {
        return m_model->topologyNormalization ? m_model->targetEdges : m_model->edges;
    }
    const std::vector<SmVertex*>& TargetVertices() const
    {
        return m_model->topologyNormalization ? m_model->targetVertices : m_model->vertices;
    }
    Index EdgeOwner(Index target) const
    {
        if (m_model->topologyNormalization)
        {
            return m_model->targetEdgeOwners.at(target);
        }
        return target < curves.size() ? curves[target].record : derivedEdgeFaces.at(target - curves.size());
    }
    std::optional<Index> SourceCurveOf(SmEdge* edge) const
    {
        if (m_ringNormalization)
        {
            const auto found = m_ringEdgeOrdinals.find(edge);
            if (found == m_ringEdgeOrdinals.end())
            {
                return {};
            }
            const auto source = m_targetCurveSources.at(found->second);
            return source < curves.size() ? std::optional<Index>(source) : std::nullopt;
        }
        const auto& target = TargetEdges();
        const auto found = std::find(target.begin(), target.end(), edge);
        if (found == target.end())
        {
            return {};
        }
        const auto index = static_cast<Index>(found - target.begin());
        return index < curves.size() ? std::optional<Index>(index) : std::nullopt;
    }
    SmVertex* RingVertexAt(Index vertex) const
    {
        return vertex < vertices.size() ? m_model->vertices.at(vertex) : m_ringVertices.at(vertex - vertices.size()).native;
    }
    Index RingExtraEdges() const
    {
        Index count = 0;
        for (const auto& [source, plan] : m_ringSubdivisions)
        {
            (void)source;
            count += plan.segments.size() - 1;
        }
        return count;
    }
    Index RingTargetEdge(Index source, Index segment) const
    {
        if (segment == 0)
        {
            return source;
        }
        Index index = curves.size();
        for (const auto& [key, plan] : m_ringSubdivisions)
        {
            if (key == source)
            {
                return index + segment - 1;
            }
            index += plan.segments.size() - 1;
        }
        throw std::logic_error("RingSubdivisionSourceMissing");
    }
    double Tol() const
    {
        return Q(m_options.quality.initialWorkToleranceMm);
    }
    Index Role(std::string_view name) const
    {
        for (const auto& role : m_source->PartDefinition().roles)
        {
            if (role.name == name)
            {
                return role.recordIndex;
            }
        }
        throw std::domain_error("GeneralRequiredSourceRoleMissing");
    }
    [[nodiscard]] Result<Index> Vertex(Index a)
    {
        if (auto it = vi.find(a); it != vi.end())
        {
            return it->second;
        }
        const auto i = vertices.size();
        vi.emplace(a, i);
        vertices.push_back(a);
        auto pointResult = Position(Ref(a, "vertex_geometry"));
        if (!pointResult)
        {
            return tl::unexpected(std::move(pointResult.error()));
        }
        const auto& point = *pointResult;
        points.push_back(point);
        return i;
    }
    [[nodiscard]] Result<Index> ParameterGeometry(Index edge, Index geometry)
    {
        bool use3d = Is(geometry, "SURFACE_CURVE") && F(geometry, "master_representation").text == "CURVE_3D";
        if (auto* choices = m_execution.SurfaceCurves())
        {
            auto chosen = choices->FindUse(Id(Role("definition")), Id(shape), Id(edge));
            if (!chosen)
            {
                return tl::unexpected(std::move(chosen.error()));
            }
            if (*chosen)
            {
                const auto planIndex = **chosen;
                const auto& plan = choices->Plan(planIndex);
                if (!Is(geometry, "SURFACE_CURVE") || Id(geometry) != plan.surfaceCurveId ||
                    F(geometry, "master_representation").text !=
                        (plan.expectedMaster == SurfaceCurveDeclaredMaster::PcurveS1 ? "PCURVE_S1" : "PCURVE_S2") ||
                    Id(Ref(geometry, "curve_3d")) != plan.curve3dId)
                {
                    return tl::unexpected(Fail("SurfaceCurve3dInterpretationBindingMismatch", edge, Status::Unsupported));
                }
                const auto associated = Refs(geometry, "associated_geometry");
                if (associated.size() != plan.pcurveIds.size())
                {
                    return tl::unexpected(Fail("SurfaceCurve3dInterpretationBindingMismatch", edge, Status::Unsupported));
                }
                for (Index i = 0; i < associated.size(); ++i)
                {
                    if (!Is(associated[i], "PCURVE") || Id(associated[i]) != plan.pcurveIds[i])
                    {
                        return tl::unexpected(Fail("SurfaceCurve3dInterpretationBindingMismatch", edge, Status::Unsupported));
                    }
                }
                m_surfaceCurveChoices.emplace(edge, planIndex);
                use3d = true;
            }
        }
        return use3d ? Ref(geometry, "curve_3d") : geometry;
    }
    [[nodiscard]] Result<void> VerifySurfaceCurveInterpretationScope()
    {
        if (m_surfaceCurveChoices.empty())
        {
            return {};
        }
        // A non-driving pcurve cannot certify membership of the selected 3D
        // curve in its support. Restrict this increment to bases of incident
        // source faces: the unchanged final lift checks independently cover
        // every one of those actual surfaces. No unused base is ignored.
        std::map<Index, std::set<Index>> incident;
        for (const auto& face : faces)
        {
            for (const auto& loop : face.loops)
            {
                for (const auto& use : loop.uses)
                {
                    const auto record = curves[use.edge].record;
                    if (m_surfaceCurveChoices.contains(record))
                    {
                        incident[record].insert(face.surface);
                    }
                }
            }
        }
        for (const auto& [edge, plan] : m_surfaceCurveChoices)
        {
            static_cast<void>(plan);
            const auto geometry = curves[ei.at(edge)].geometry;
            for (const auto pcurve : Refs(geometry, "associated_geometry"))
            {
                if (!incident[edge].contains(Ref(pcurve, "basis_surface")))
                {
                    return tl::unexpected(Fail("SurfaceCurve3dInterpretationBasisCoverageUnestablished", pcurve, Status::Unsupported));
                }
            }
        }
        return {};
    }
    [[nodiscard]] Result<Index> Edge(Index a)
    {
        if (auto it = ei.find(a); it != ei.end())
        {
            return it->second;
        }
        const auto i = curves.size();
        ei.emplace(a, i);
        // AP214E3_2010.exp EDGE_CURVE.same_sense: orient the private edge
        // along increasing defining-curve parameter, reversing source endpoints
        // only in this mapping. Source EDGE_CURVE and vertex records stay intact.
        const auto geometry = Ref(a, "edge_geometry");
        // ISO 10303-42:2003 4.4.40: a trimmed curve's parameter increases
        // from trim_1, opposing its basis when sense_agreement is FALSE.
        // Native conics increase along the BASIS, so compose this extra sense
        // with EC and subsequently OE; never change the immutable source flags.
        auto parameter = ParameterGeometry(a, geometry);
        if (!parameter)
        {
            return tl::unexpected(std::move(parameter.error()));
        }
        const auto parameterGeometry = *parameter;
        // CURVE_3D carries the defining trimmed curve's sense through its
        // surface-curve wrapper. Compose it before OE/loop uses are prepared.
        const bool same = Is(parameterGeometry, "TRIMMED_CURVE") ? B(a, "same_sense") == B(parameterGeometry, "sense_agreement") : B(a, "same_sense");
        const auto start = Ref(a, "edge_start"), end = Ref(a, "edge_end");
        auto firstResult = Vertex(same ? start : end);
        if (!firstResult)
        {
            return tl::unexpected(std::move(firstResult.error()));
        }
        const auto& first = *firstResult;
        auto lastResult = Vertex(same ? end : start);
        if (!lastResult)
        {
            return tl::unexpected(std::move(lastResult.error()));
        }
        const auto& last = *lastResult;
        curves.push_back({ a, geometry, first, last, same, {}, 0, 0, {} });
        return i;
    }
    bool DetachedCurveSupported(Index geometry) const
    {
        return SupportsDetachedCurve(*m_source, geometry);
    }
    [[nodiscard]] Result<void> PrepareRole()
    {
        // Source checking can inventory every MSB in one part. Until collection
        // publication is implemented, reject the whole request before selecting
        // a singular role or making any native allocation. Never import body 0.
        if (!m_binding && m_source->PartDefinition().shapes.size() > 1)
        {
            return tl::unexpected(
                Failure{ Status::Unsupported,
                         "GeneralMaterialCollectionNotImplemented",
                         "Complete multi-body native construction and publication are not implemented",
                         Role("representation"),
                         "GeneralBrep" }
            );
        }
        for (const auto& role : m_source->PartDefinition().roles)
        {
            if (role.name == "wireframe_representation")
            {
                wireframeRepresentation = role.recordIndex;
            }
            if (role.name == "construction_representation")
            {
                supportingRepresentation = role.recordIndex;
            }
        }
        if (m_binding)
        {
            const auto& inventory = m_source->PartDefinition().shapes;
            const bool selected = std::any_of(
                inventory.begin(),
                inventory.end(),
                [&](const auto& item)
                {
                    return item.shape == m_binding->shape && item.shell == m_binding->shell && item.role == m_binding->role;
                }
            );
            if (!selected)
            {
                throw std::logic_error("UnregisteredGeneralBodyBinding");
            }
            shape = m_binding->shape;
            shell = m_binding->shell;
        }
        else
        {
            shape = Role("shape");
            shell = Role("shell");
        }
        const auto& role = m_binding ? m_binding->role : m_source->PartDefinition().shapeRole;
        if (role == "MaterialSolid")
        {
            m_bodyRole = GeneralBodyRole::MaterialSolid;
        }
        else if (role == "ClosedSurfaceModel")
        {
            m_bodyRole = GeneralBodyRole::ClosedSurfaceModel;
        }
        else if (role == "OpenSurfaceModel")
        {
            m_bodyRole = GeneralBodyRole::OpenSurfaceModel;
        }
        else
        {
            return tl::unexpected(Fail("GeneralUnsupportedShapeRole", shape, Status::Unsupported));
        }
        const char* shapeType = IsMaterial(m_bodyRole) ? "MANIFOLD_SOLID_BREP" : "SHELL_BASED_SURFACE_MODEL";
        const char* shellType = IsClosedBoundary(m_bodyRole) ? "CLOSED_SHELL" : "OPEN_SHELL";
        if (auto businessCheck = Need(Is(shape, shapeType) && Is(shell, shellType), "GeneralUnsupportedShape", shape); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (!IsMaterial(m_bodyRole) && (m_options.normalizePeriodicFaces || m_options.faceOrientationRepair || m_options.cylindricalBandRepair))
        {
            // Late splitting needs a role-qualified use/segment proof; source
            // repair plans retain their separately established material scope.
            return tl::unexpected(Fail("GeneralSurfaceConfigurationUnestablished", shape, Status::Unsupported));
        }
        const auto representation = Role("representation");
        m_report.sourceShape = SourceShapeObservation{ std::string(GeneralRoleName(m_bodyRole)),
                                                       Id(representation),
                                                       m_source->Source().Records().at(representation).components.at(0).name,
                                                       Id(shape),
                                                       shapeType,
                                                       Id(shell),
                                                       shellType };
        return {};
    }
    [[nodiscard]] Result<void> Prepare()
    {
        const bool admitsVertexLoops = m_source->Instances("VERTEX_LOOP") != nullptr;
        const auto power = m_source->UnitContext().lengthPower10Mm;
        std::optional<ResolvedLengthScale> convertedLength;
        if (power)
        {
            scale = std::pow(10.0, *power); // Preserve the established SI path and its report/call order.
        }
        else
        {
            auto nativeValue2 = ResolveLengthScale(
                *m_source,
                m_source->UnitContext().lengthUnit,
                [&]
                {
                    return m_execution.Check(Phase::SemanticsChecked);
                }
            );
            if (!nativeValue2)
            {
                return tl::unexpected(std::move(nativeValue2.error()));
            }
            convertedLength = std::move(*nativeValue2);
            scale = Q(convertedLength->scaleMm);
        }
        if (auto businessCheck = Need(scale > 0 && std::isfinite(scale), "GeneralUnitScaleRange", shape); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        // Angular placement conventions do not use the file's angle unit.
        // CONICAL_SURFACE.semi_angle does, through the checked physical angle chain.
        for (const auto a : Refs(shell, "cfs_faces"))
        {
            Face f{ a, Ref(a, "face_geometry"), B(a, "same_sense"), {} };
            const auto edited = std::find_if(
                m_faceSenseOverrides.begin(),
                m_faceSenseOverrides.end(),
                [&](const auto& e)
                {
                    return e.first == a;
                }
            );
            if (edited != m_faceSenseOverrides.end())
            {
                f.same = edited->second;
            }
            for (const auto bound : Refs(a, "bounds"))
            {
                const auto loop = Ref(bound, "bound");
                Loop l{ loop,
                        bound,
                        Is(bound, "FACE_OUTER_BOUND") && !NeutralizesCylindricalBandBounds(m_cylindricalBandTrial, a),
                        B(bound, "orientation"),
                        {},
                        {} };
                if (admitsVertexLoops && Is(loop, "VERTEX_LOOP"))
                {
                    auto vertex = Vertex(Ref(loop, "loop_vertex"));
                    if (!vertex)
                    {
                        return tl::unexpected(std::move(vertex.error()));
                    }
                    l.vertex = *vertex;
                    f.loops.push_back(std::move(l));
                    continue;
                }
                if (!Is(loop, "EDGE_LOOP"))
                {
                    return tl::unexpected(Fail("GeneralSourceLoopKindUnsupported", loop, Status::Unsupported));
                }
                const auto oriented = Refs(loop, "edge_list");
                // AP214E3_2010.exp ORIENTED_EDGE.orientation is relative to EC;
                // FACE_BOUND.orientation reverses the whole source loop, while
                // ADVANCED_FACE.same_sense relates the source face to surface normal.
                // Native primary loops use the surface-normal sense, so compose
                // all three with EC.same_sense before constructing ordered uses.
                const bool forward = l.orientation == f.same;
                for (Index k = 0; k < oriented.size(); ++k)
                {
                    const auto use = oriented[forward ? k : oriented.size() - 1 - k];
                    auto edgeResult = Edge(Ref(use, "edge_element"));
                    if (!edgeResult)
                    {
                        return tl::unexpected(std::move(edgeResult.error()));
                    }
                    const auto& edge = *edgeResult;
                    const bool along = B(use, "orientation") == curves[edge].same;
                    l.uses.push_back({ edge, use, forward ? along : !along, a, bound, loop });
                }
                // Closure is by source vertex identity, not a distance-based weld.
                for (Index k = 0; k < l.uses.size(); ++k)
                {
                    const auto& u = l.uses[k];
                    const auto& next = l.uses[(k + 1) % l.uses.size()];
                    const auto& c = curves[u.edge];
                    const auto& d = curves[next.edge];
                    if (auto businessCheck = Need((u.forward ? c.v1 : c.v0) == (next.forward ? d.v0 : d.v1), "GeneralSourceLoopDisconnected", bound);
                        !businessCheck)
                    {
                        return tl::unexpected(std::move(businessCheck.error()));
                    }
                }
                f.loops.push_back(std::move(l));
            }
            std::stable_sort(
                f.loops.begin(),
                f.loops.end(),
                [](const Loop& a, const Loop& b)
                {
                    return a.outer > b.outer;
                }
            );
            faces.push_back(std::move(f));
        }
        // Incidence is tested in source-selected-face sense. Undo the preceding
        // surface-normal normalization with face.same_sense before comparing
        // the two uses: every closed-shell EC must occur oppositely twice.
        std::vector<std::vector<std::pair<Index, bool>>> incidence(curves.size());
        m_roleEdges.reserve(curves.size());
        for (const auto& curve : curves)
        {
            m_roleEdges.push_back({ curve.record, {} });
        }
        for (Index ordinal = 0; ordinal < faces.size(); ++ordinal)
        {
            const auto& f = faces[ordinal];
            m_roleFaces.push_back({ f.record, f.same });
            for (const auto& l : f.loops)
            {
                for (const auto& use : l.uses)
                {
                    incidence[use.edge].push_back({ f.record, use.forward == f.same });
                    m_roleEdges[use.edge].uses.push_back({ ordinal, use.forward });
                }
            }
        }
        std::size_t freeEdges = 0;
        for (Index i = 0; i < incidence.size(); ++i)
        {
            const auto& uses = incidence[i];
            const bool free = !IsClosedBoundary(m_bodyRole) && uses.size() == 1;
            freeEdges += free;
            if (auto businessCheck = Need(
                    free || (uses.size() == 2 && uses[0].second != uses[1].second),
                    "GeneralSourceNonManifoldEdge",
                    curves[i].record
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        if (!IsClosedBoundary(m_bodyRole) && freeEdges == 0)
        {
            return tl::unexpected(Fail("GeneralOpenBoundaryUnestablished", shell, Status::Unsupported));
        }
        std::int64_t num = 1, den = 1;
        for (int i = 0; power && i < std::abs(*power); ++i)
        {
            if (i >= 15)
            {
                return tl::unexpected(Fail("GeneralUnitScaleRange", shape, Status::Indeterminate));
            }
            if (*power < 0)
            {
                den *= 10;
            }
            else
            {
                num *= 10;
            }
        }
        m_report.units = UnitNormalization{ Id(m_source->UnitContext().lengthUnit), Fraction(num, den), {} };
        if (convertedLength)
        {
            m_report.units->lengthToMm = convertedLength->scaleMm;
            m_report.units->generalDefinitions = std::move(convertedLength->definitions);
        }
        m_model->lengthUnits = *m_report.units;
        if (supportingRepresentation)
        {
            // Physical content cannot be an interoperability exception. Reject
            // unsupported required items before allocating a native candidate.
            for (const auto geometry : Refs(*supportingRepresentation, "items"))
            {
                if (!Is(geometry, "CARTESIAN_POINT") && !Is(geometry, "AXIS2_PLACEMENT_3D") && !Is(geometry, "PLANE") &&
                    !DetachedCurveSupported(geometry))
                {
                    return tl::unexpected(Fail("GeneralSupportingGeometryUnsupported", geometry, Status::Unsupported));
                }
            }
        }
        if (wireframeRepresentation)
        {
            for (const auto set : Refs(*wireframeRepresentation, "items"))
            {
                if (Is(set, "AXIS2_PLACEMENT_3D"))
                {
                    continue; // Retained source frame, not an inferred relation transform.
                }
                for (const auto geometry : Refs(set, "elements"))
                {
                    const bool admitted = Is(geometry, "CARTESIAN_POINT") || DetachedCurveSupported(geometry);
                    if (!admitted)
                    {
                        return tl::unexpected(Fail("GeneralWireframeGeometryUnsupported", geometry, Status::Unsupported));
                    }
                }
            }
        }
        return {};
    }
    [[nodiscard]] Result<void> BuildWireframes(CurvePointDefinitions& definitions)
    {
        if (!wireframeRepresentation)
        {
            return {};
        }
        WireframeEvidence evidence;
        for (const auto set : Refs(*wireframeRepresentation, "items"))
        {
            if (!Is(set, "GEOMETRIC_CURVE_SET"))
            {
                continue;
            }
            WireframeInfo group;
            group.representationId = Id(*wireframeRepresentation);
            group.relationshipId = Id(Role("wireframe_shape_relationship"));
            group.setId = Id(set);
            for (const auto geometry : Refs(set, "elements"))
            {
                if (auto operation = m_execution.Check(Phase::BeforeNativeCall); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (auto operation = definitions.Append(SourceRecord{ geometry }, group, evidence); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
            }
            if (auto businessCheck = Need(!group.curves.empty() || !group.points.empty(), "GeneralWireEmptySet", set); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            m_model->wireframes.push_back(std::move(group));
        }
        m_report.wireframes = m_model->wireframes;
        m_report.tolerances.push_back(
            { "Postcheck", "MeasuredWireframeSourceTrimResidual", Id(*wireframeRepresentation), evidence.SourceResidualMm() }
        );
        m_report.tolerances.push_back(
            { "Postcheck", "MeasuredWireframeDefinitionEndpointError", Id(*wireframeRepresentation), evidence.EndpointResidualMm() }
        );
        return {};
    }
    [[nodiscard]] Result<void> BuildSupportingGeometry(CurvePointDefinitions& definitions)
    {
        if (!supportingRepresentation)
        {
            return {};
        }
        SupportingGeometryInfo group;
        group.representationId = Id(*supportingRepresentation);
        group.relationshipId = Id(Role("construction_relationship"));
        SupportingEvidence evidence;
        for (const auto geometry : Refs(*supportingRepresentation, "items"))
        {
            if (auto operation = m_execution.Check(Phase::BeforeNativeCall); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            auto appended = definitions.Append(SourceRecord{ geometry }, group, evidence);
            if (!appended)
            {
                return tl::unexpected(std::move(appended.error()));
            }
            if (*appended == SupportingAppendResult::ExistingPlacement)
            {
                const bool plane = Is(geometry, "PLANE");
                const auto position = plane ? Ref(geometry, "position") : geometry;
                auto frameResult = Placement(position);
                if (!frameResult)
                {
                    return tl::unexpected(std::move(frameResult.error()));
                }
                const auto& frame = *frameResult;
                // STEP PLANE is unbounded. CreateCanonical retains its analytic
                // axes and native sentinel domain, without a finite source trim.
                // For a direct placement this same native storage is explicitly
                // an archive frame adapter, not an added surface or locus.
                SmPlane* raw = nullptr;
                if (auto operation = Step(geometry); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                const auto status = SmPlane::CreateCanonical(*m_model->context, frame.Native(), raw);
                std::unique_ptr<SmPlane> owned(raw);
                if (auto nativeStatusCheck = Good(status); !nativeStatusCheck)
                {
                    return tl::unexpected(std::move(nativeStatusCheck.error()));
                }
                if (auto businessCheck = Need(bool(owned), "GeneralSupportingPlaneAllocation", geometry); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                const auto& actual = owned->GetPosition();
                const auto axesEqual = [&](const auto& value, const V& expected)
                {
                    return value.x == expected[0] && value.y == expected[1] && value.z == expected[2];
                };
                if (auto businessCheck = Need(
                        axesEqual(actual.GetOriginRef(), frame.o) && axesEqual(actual.GetXAxisRef(), frame.x) &&
                            axesEqual(actual.GetYAxisRef(), frame.y),
                        "GeneralSupportingFrameChanged",
                        geometry
                    );
                    !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                const CoordinateFrame coordinates{ frame.o, frame.x, frame.y, frame.z };
                const auto index = m_model->supportingSurfaces.size();
                if (plane)
                {
                    const auto domain = owned->GetNaturalUVDomain();
                    group.planes.push_back(
                        { Id(geometry),
                          Id(position),
                          index,
                          coordinates,
                          { domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y } }
                    );
                }
                else
                {
                    group.frames.push_back({ Id(geometry), index, coordinates });
                }
                m_model->supportingSurfaces.push_back(std::move(owned));
            }
        }
        m_model->supportingGeometry.push_back(std::move(group));
        m_report.supportingGeometry = m_model->supportingGeometry;
        if (const auto sourceConsistency = evidence.SourceResidualMm())
        {
            m_report.tolerances.push_back({ "Postcheck", "MeasuredSupportingSourceTrimResidual", Id(*supportingRepresentation), *sourceConsistency });
        }
        if (const auto definitionEndpoints = evidence.EndpointResidualMm())
        {
            m_report.tolerances.push_back(
                { "Postcheck", "MeasuredSupportingDefinitionEndpointError", Id(*supportingRepresentation), *definitionEndpoints }
            );
        }
        return {};
    }
    [[nodiscard]] Result<void> MakeSurfaceCurve3d(Curve& c, Index geometry, std::optional<std::size_t> interpretation = {})
    {
        // Retain the original 3D definition, never a fit lifted from a pcurve.
        // Declared CURVE_3D requires all auxiliary consistency checks; the
        // separately bound interpretation retains known auxiliary conflicts
        // without qualifying the file's original pcurve priority/consistency.
        // STEP_IMPORT/README.md, source-priority surface-curve construction.
        const auto auxiliary = Ref(geometry, "curve_3d");
        if (Is(auxiliary, "SURFACE_CURVE"))
        {
            return tl::unexpected(Fail("GeneralSurfaceCurveAuxiliaryUnsupported", auxiliary, Status::Unsupported));
        }
        Curve master;
        master.record = c.record;
        master.geometry = auxiliary;
        master.v0 = c.v0;
        master.v1 = c.v1;
        master.same = c.same;
        if (auto made = MakeCurve(master); !made)
        {
            return tl::unexpected(std::move(made.error()));
        }
        std::optional<std::size_t> actionIndex;
        if (interpretation)
        {
            SurfaceCurve3dInterpretationAction action;
            action.planIndex = *interpretation;
            action.nativeEdgeInterval = { master.t0, master.t1 };
            auto recorded = m_execution.SurfaceCurves()->RecordAction(std::move(action));
            if (!recorded)
            {
                return tl::unexpected(std::move(recorded.error()));
            }
            actionIndex = *recorded;
        }
        const auto associated = Refs(geometry, "associated_geometry");
        const bool sourceForward = !Is(auxiliary, "TRIMMED_CURVE") || B(auxiliary, "sense_agreement");
        for (Index occurrence = 0; occurrence < associated.size(); ++occurrence)
        {
            const auto record = associated[occurrence];
            if (!Is(record, "PCURVE"))
            {
                return tl::unexpected(Fail("GeneralSurfaceCurveAssociationUnsupported", record, Status::Unsupported));
            }
            const auto mappingBegin = m_report.parameterMappings.size();
            auto made = MakeSourcePcurve(*this, record, c.record);
            if (!made)
            {
                return tl::unexpected(std::move(made.error()));
            }
            Curve comparison;
            comparison.record = record;
            comparison.geometry = record;
            comparison.native = std::move(made->curve);
            comparison.bounds = made->conservativeBoundsMm;
            comparison.associationOccurrence = occurrence + 1;
            const auto domain = comparison.native->GetNaturalInterval();
            const auto retainConflict = [&](const Failure& failure) -> Result<void>
            {
                // This explicit source interpretation can retain only known
                // auxiliary geometric disagreements. Type/unit/construction,
                // native API, resource and cancellation failures still stop
                // immediately, before any subsequent operation or publication.
                if (!interpretation || !IsSurfaceCurve3dAuxiliaryConflict(failure))
                {
                    return tl::unexpected(failure);
                }
                return m_execution.SurfaceCurves()->RecordConflict(
                    *actionIndex,
                    { Id(record),
                      occurrence + 1,
                      StatusName(failure.status),
                      failure.key,
                      failure.record ? Id(*failure.record) : Id(record),
                      failure.stage,
                      failure.message }
                );
            };
            auto first = CurveParameter(*comparison.native, points[c.v0], domain.GetMin(), record);
            if (!first)
            {
                if (auto retained = retainConflict(first.error()); !retained)
                {
                    return tl::unexpected(std::move(retained.error()));
                }
            }
            auto last = CurveParameter(*comparison.native, points[c.v1], domain.GetMax(), record);
            if (!last)
            {
                if (auto retained = retainConflict(last.error()); !retained)
                {
                    return tl::unexpected(std::move(retained.error()));
                }
            }
            // A reverse trimmed 3D master decreases in its basis parameter,
            // while its pcurve increases in the wrapper's source sense. Keep
            // both exact definitions; represent that known relation explicitly.
            bool endpoints = first && last;
            comparison.t0 = endpoints ? (sourceForward ? *first : *last) : domain.GetMin();
            comparison.t1 = endpoints ? (sourceForward ? *last : *first) : domain.GetMax();
            comparison.forwardForComparison = sourceForward;
            if (!(comparison.t1 > comparison.t0))
            {
                const auto failure = Fail("GeneralSurfaceCurveAssociationSenseConflict", record);
                if (auto retained = retainConflict(failure); !retained)
                {
                    return tl::unexpected(std::move(retained.error()));
                }
                endpoints = false;
            }
            const auto mappingEnd = m_report.parameterMappings.size();
            for (Index i = mappingBegin; i < mappingEnd; ++i)
            {
                auto& mapping = m_report.parameterMappings[i];
                mapping.targetEdgeIndex.reset();
                mapping.kind = "SurfaceCurveAuxiliaryPcurve" + mapping.kind;
                mapping.equation = "private comparison definition, no target-edge parameter identity: " + mapping.equation;
                mapping.parameters.push_back(
                    { interpretation ? "expected_increasing_parameter_follows_native_master" : "increasing_parameter_follows_native_master",
                      { sourceForward ? 1. : 0. } }
                );
                mapping.parameters.push_back({ "source_association_occurrence", { double(occurrence + 1) } });
                if (interpretation)
                {
                    mapping
                        .equation += "; retained source auxiliary under explicit Curve3dPriority interpretation, original consistency not qualified";
                }
            }
            // Do not assume equal parameters or silently drop an inaccurate
            // auxiliary. Shared source definitions and LIST occurrences retain
            // their original identities, order and direction checks.
            // If endpoints cannot be associated, the full source pcurve is
            // retained but no selected-interval/image or direction agreement
            // is claimed. Actual target face membership is checked separately.
            bool directionObserved = false;
            if (endpoints)
            {
                const auto observed = ObserveCurveImages(master, comparison, geometry);
                directionObserved = bool(observed);
                if (!observed)
                {
                    if (auto retained = retainConflict(observed.error()); !retained)
                    {
                        return tl::unexpected(std::move(retained.error()));
                    }
                }
            }
            if (interpretation)
            {
                // A declared/expected sense is not observed agreement. Keep
                // missing intervals and failed direction observations explicit
                // when a non-driving original is retained under interpretation.
                for (Index i = mappingBegin; i < mappingEnd; ++i)
                {
                    auto& mapping = m_report.parameterMappings[i];
                    mapping.parameters.push_back({ "selected_interval_established", { endpoints ? 1. : 0. } });
                    mapping.parameters.push_back({ "direction_observation_passed", { directionObserved ? 1. : 0. } });
                }
            }
        }
        c.native = std::move(master.native);
        c.bounds = master.bounds;
        c.t0 = master.t0;
        c.t1 = master.t1;
        c.p0 = master.p0;
        c.p1 = master.p1;
        ParameterMapping mapping;
        mapping.kind = interpretation ? "InterpretedSurfaceCurveMaster3d" : "SurfaceCurveMaster3d";
        mapping.sourceGeometryId = Id(geometry);
        mapping.sourceBasisGeometryId = Id(auxiliary);
        mapping.sourceUseId = Id(c.record);
        mapping.targetEdgeIndex = ei.at(c.record);
        mapping.equation =
            interpretation ?
                "explicit original/edge-bound Curve3dPriority; original declared pcurve priority retained and not qualified; original 3D parameter conversion is recorded by its basis mapping" :
                "declared CURVE_3D uses the original 3D defining curve; source/native parameter conversion is recorded by its basis mapping";
        mapping.parameters = { { "native_natural_interval", { c.native->GetNaturalInterval().GetMin(), c.native->GetNaturalInterval().GetMax() } },
                               { "native_edge_interval", { c.t0, c.t1 } } };
        m_report.parameterMappings.push_back(std::move(mapping));
        if (actionIndex)
        {
            m_surfaceCurveActions.push_back(*actionIndex);
        }
        return {};
    }
    [[nodiscard]] Result<void> MakeCurve(Curve& c)
    {
        const bool trimmed = Is(c.geometry, "TRIMMED_CURVE");
        const auto g = trimmed ? Ref(c.geometry, "basis_curve") : c.geometry;
        if (trimmed && !Is(g, "CIRCLE") && !Is(g, "ELLIPSE"))
        {
            return tl::unexpected(Fail("GeneralTrimmedBasisUnsupported", c.geometry, Status::Unsupported));
        }
        const auto a = points[c.v0], b = points[c.v1];
        ParameterMapping mapping;
        mapping.sourceGeometryId = Id(g);
        mapping.sourceUseId = Id(c.record);
        mapping.targetEdgeIndex = ei.at(c.record);
        std::vector<Curve> comparisons;
        const bool lifted = Is(g, "SURFACE_CURVE");
        if (lifted)
        {
            const auto preference = F(g, "master_representation").text;
            if (const auto chosen = m_surfaceCurveChoices.find(c.record); chosen != m_surfaceCurveChoices.end())
            {
                return MakeSurfaceCurve3d(c, g, chosen->second);
            }
            if (preference == "CURVE_3D")
            {
                return MakeSurfaceCurve3d(c, g);
            }
            if (preference != "PCURVE_S1" && preference != "PCURVE_S2")
            {
                return tl::unexpected(Fail("GeneralSurfaceCurveMasterUnsupported", g, Status::Unsupported));
            }
            const auto associated = Refs(g, "associated_geometry");
            const Index selected = preference == "PCURVE_S1" ? 0 : 1;
            const auto auxiliary = Ref(g, "curve_3d");
            // Keep this construction bounded and reuse the same 3D factory.
            // Nested surface-curve auxiliary definitions need a separate DAG
            // construction contract, rather than recursive native allocation.
            if (Is(auxiliary, "SURFACE_CURVE"))
            {
                return tl::unexpected(Fail("GeneralSurfaceCurveAuxiliaryUnsupported", auxiliary, Status::Unsupported));
            }
            Curve comparison;
            comparison.record = c.record;
            comparison.geometry = auxiliary;
            comparison.v0 = c.v0;
            comparison.v1 = c.v1;
            comparison.same = c.same;
            // The auxiliary's trimmed wrapper may oppose its native conic
            // basis. This is an established source sense, not a fitted flip to
            // make disagreeing representations pass a geometric comparison.
            if (Is(auxiliary, "TRIMMED_CURVE") && !B(auxiliary, "sense_agreement"))
            {
                std::swap(comparison.v0, comparison.v1);
                comparison.same = !comparison.same;
                comparison.forwardForComparison = false;
            }
            const auto auxiliaryMappingsBegin = m_report.parameterMappings.size();
            if (auto made = MakeCurve(comparison); !made)
            {
                if (made.error().key == "GeneralLineParameterOrder" || made.error().key == "GeneralSplineParameterOrder")
                {
                    return tl::unexpected(Fail("GeneralSurfaceCurveAuxiliarySenseConflict", auxiliary));
                }
                return tl::unexpected(std::move(made.error()));
            }
            // This private comparison definition is not the published edge.
            // Its own native parameter may differ from the selected master.
            for (Index i = auxiliaryMappingsBegin; i < m_report.parameterMappings.size(); ++i)
            {
                auto& evidence = m_report.parameterMappings[i];
                evidence.targetEdgeIndex.reset();
                evidence.kind = "SurfaceCurveAuxiliary3d" + evidence.kind;
                evidence.equation = "private comparison definition, no target-edge parameter identity: " + evidence.equation;
                if (!comparison.forwardForComparison)
                {
                    evidence.parameters.push_back({ "increasing_parameter_follows_native_master", { 0. } });
                }
            }
            comparisons.push_back(std::move(comparison));
            bool selectedSourceParameterIdentity = true;
            for (Index i = 0; i < associated.size(); ++i)
            {
                if (!Is(associated[i], "PCURVE"))
                {
                    return tl::unexpected(Fail("GeneralSurfaceCurveAssociationUnsupported", associated[i], Status::Unsupported));
                }
                const auto precisionBegin = m_report.pcurveCarrierPrecisions.size();
                const auto chartBegin = m_splineCharts->bindings.size();
                const auto lineBegin = m_pcurveLines->bindings.size();
                auto made = MakeSourcePcurve(*this, associated[i], c.record, i == selected);
                if (!made)
                {
                    return tl::unexpected(std::move(made.error()));
                }
                if (i == selected)
                {
                    selectedSourceParameterIdentity = made->sourceParameterIdentity;
                    c.native = std::move(made->curve);
                    c.bounds = made->conservativeBoundsMm;
                    m_report.parameterMappings.back().targetEdgeIndex = ei.at(c.record);
                    for (auto at = chartBegin; at < m_splineCharts->bindings.size(); ++at)
                    {
                        auto& binding = m_splineCharts->bindings[at];
                        binding.mapping.targetEdgeIndex = ei.at(c.record);
                        m_report.parameterMappings.at(binding.reportMapping) = binding.mapping;
                    }
                    for (auto at = precisionBegin; at < m_report.pcurveCarrierPrecisions.size(); ++at)
                    {
                        m_report.pcurveCarrierPrecisions[at].targetEdgeIndex = ei.at(c.record);
                    }
                    for (auto at = lineBegin; at < m_pcurveLines->bindings.size(); ++at)
                    {
                        auto& binding = m_pcurveLines->bindings[at];
                        binding.mapping.targetEdgeIndex = ei.at(c.record);
                        m_report.parameterMappings.at(binding.reportMapping) = binding.mapping;
                    }
                }
                else
                {
                    Curve alternative;
                    alternative.record = associated[i];
                    alternative.geometry = associated[i];
                    alternative.associationOccurrence = i + 1;
                    alternative.native = std::move(made->curve);
                    const auto domain = alternative.native->GetNaturalInterval();
                    auto first = CurveParameter(*alternative.native, a, domain.GetMin(), associated[i]);
                    if (!first)
                    {
                        return tl::unexpected(std::move(first.error()));
                    }
                    auto last = CurveParameter(*alternative.native, b, domain.GetMax(), associated[i]);
                    if (!last)
                    {
                        return tl::unexpected(std::move(last.error()));
                    }
                    alternative.t0 = *first;
                    alternative.t1 = *last;
                    if (!(alternative.t1 > alternative.t0))
                    {
                        // A different parameterization does not grant opposite
                        // source sense. Never sort away this representation
                        // conflict to make image-distance checks pass.
                        return tl::unexpected(Fail("GeneralSurfaceCurveAssociationSenseConflict", associated[i]));
                    }
                    comparisons.push_back(std::move(alternative));
                }
            }
            mapping.kind = "SurfaceCurveMasterPcurve";
            mapping.equation = selectedSourceParameterIdentity ? "source_t=native_t of selected " + preference +
                                                                     "; auxiliary images observed independently, not assumed to share t" :
                                                                 "native_tau follows the explicit SourcePcurveLineWindow map of selected " +
                                                                     preference + "; auxiliary images observed independently, not assumed to share t";
            mapping.parameters = { { "selected_source_pcurve_occurrence", { double(selected + 1) } } };
        }
        else if (Is(g, "LINE"))
        {
            auto originResult = Position(Ref(g, "pnt"));
            if (!originResult)
            {
                return tl::unexpected(std::move(originResult.error()));
            }
            const auto& origin = *originResult;
            const auto vector = Ref(g, "dir");
            auto magnitudeResult = N(vector, "magnitude");
            if (!magnitudeResult)
            {
                return tl::unexpected(std::move(magnitudeResult.error()));
            }
            const auto& magnitude = *magnitudeResult;
            if (auto businessCheck = Need(magnitude > 0, "GeneralDegenerateLine", g); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            auto dResult = Direction(Ref(vector, "orientation"));
            if (!dResult)
            {
                return tl::unexpected(std::move(dResult.error()));
            }
            const auto& d = *dResult;
            const auto t0 = Dot(Sub(a, origin), d), t1 = Dot(Sub(b, origin), d);
            if (auto businessCheck = Need(t1 > t0, "GeneralLineParameterOrder", c.record); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            mapping.kind = "LineAffineParameter";
            mapping.equation = "source_t=lerp(source_parameter_at_native_ends,(native_t-native_begin)/(native_end-native_begin))";
            mapping.parameters.push_back({ "source_parameter_at_native_ends", { t0 / (magnitude * scale), t1 / (magnitude * scale) } });
            SmLine* line = nullptr;
            if (auto operation = Step(g); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto createdStatus = SmLine::CreateLineSegment(*m_model->context, 3, P(Add(origin, Mul(d, t0))), P(Add(origin, Mul(d, t1))), line);
            c.native.reset(line);
            if (auto nativeStatusCheck = Good(createdStatus); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
        }
        else if (Is(g, "CIRCLE") || Is(g, "ELLIPSE"))
        {
            auto frameResult = Placement(Ref(g, "position"));
            if (!frameResult)
            {
                return tl::unexpected(std::move(frameResult.error()));
            }
            const auto& frame = *frameResult;
            const bool circle = Is(g, "CIRCLE");
            auto firstRadiusResult = N(g, circle ? "radius" : "semi_axis_1");
            if (!firstRadiusResult)
            {
                return tl::unexpected(std::move(firstRadiusResult.error()));
            }
            const auto& firstRadius = *firstRadiusResult;
            const auto rx = firstRadius * scale;
            double ry = rx;
            if (!circle)
            {
                auto secondRadiusResult = N(g, "semi_axis_2");
                if (!secondRadiusResult)
                {
                    return tl::unexpected(std::move(secondRadiusResult.error()));
                }
                const auto& secondRadius = *secondRadiusResult;
                ry = secondRadius * scale;
            }
            if (trimmed && (!(rx > SM_EFF_ZERO) || !(ry > SM_EFF_ZERO) || !std::isfinite(rx) || !std::isfinite(ry)))
            {
                return tl::unexpected(Fail("GeneralTrimmedConicRadiusRange", g, Status::Unsupported));
            }
            const auto angle = [&](V p)
            {
                const auto delta = Sub(p, frame.o);
                return std::atan2(Dot(delta, frame.y) / ry, Dot(delta, frame.x) / rx);
            };
            double t0 = angle(a), t1 = angle(b);
            std::optional<Index> trimMapping;
            if (c.v0 == c.v1)
            {
                t1 = t0 + 2 * std::numbers::pi;
            }
            else
            {
                while (t1 <= t0)
                {
                    t1 += 2 * std::numbers::pi;
                }
            }
            if (trimmed)
            {
                constexpr double tau = 2 * std::numbers::pi;
                auto trimBusiness = TrimmedConic(c.geometry, frame, rx, ry, consistency);
                if (!trimBusiness)
                {
                    return tl::unexpected(std::move(trimBusiness.error()));
                }
                const auto trim = std::move(*trimBusiness);
                if (c.v0 == c.v1)
                {
                    if (auto businessCheck = Need(trim.full, "GeneralTrimmedClosedEdgeRequiresFullConic", c.record); !businessCheck)
                    {
                        return tl::unexpected(std::move(businessCheck.error()));
                    }
                }
                else if (!trim.full)
                {
                    // Resolve each EC endpoint inside the source trim's
                    // increasing BASIS interval. The topological edge may use
                    // a subset; it cannot extend or ignore the wrapper limits.
                    const auto inside = [&](double t)
                    {
                        // atan2 and source decimal conversion can straddle a
                        // cut by a few ulps. Select the adjacent periodic branch
                        // before bounding that inversion to the ORIGINAL cut.
                        // This roundoff envelope is independent of quality tol;
                        // the usual native endpoint/vertex gap check below must
                        // still pass in mm, including for very large radii.
                        const double roundoff = 32 * std::numeric_limits<double>::epsilon() *
                                                std::max({ 1., std::abs(trim.low), std::abs(trim.high) });
                        double candidate = t + tau * std::ceil((trim.low - t - roundoff) / tau);
                        if (candidate < trim.low && candidate >= trim.low - roundoff)
                        {
                            candidate = trim.low;
                        }
                        if (candidate > trim.high && candidate <= trim.high + roundoff)
                        {
                            candidate = trim.high;
                        }
                        return candidate;
                    };
                    t0 = inside(angle(a));
                    t1 = inside(angle(b));
                    if (auto businessCheck = Need(t0 >= trim.low && t1 <= trim.high && t1 > t0, "GeneralTrimmedEdgeOutsideSourceDomain", c.record);
                        !businessCheck)
                    {
                        return tl::unexpected(std::move(businessCheck.error()));
                    }
                }
                ParameterMapping trace;
                trace.kind = "TrimmedConicSourceDomain";
                trace.sourceGeometryId = Id(c.geometry);
                trace.sourceBasisGeometryId = Id(g);
                trace.sourceUseId = Id(c.record);
                trace.targetEdgeIndex = ei.at(c.record);
                trace.equation =
                    "canonical_trim_s_rad=sign*(basis_angle_rad-trim_1_rad); periodic_branch_when_full; "
                    "source_s=canonical_trim_s_rad/angle_parameter_to_rad_when_established; native_t_is_rational_not_angle; preferred=" +
                    F(c.geometry, "master_representation").text;
                trace.parameters = { { "basis_trim_rad", { trim.first, trim.last } },
                                     { "canonical_trim_parameter_range_rad", { 0, trim.high - trim.low } },
                                     { "source_sense", { B(c.geometry, "sense_agreement") ? 1. : -1. } },
                                     { "edge_basis_angle_branch_rad", { t0, t1 } } };
                if (trim.angleScale)
                {
                    trace.parameters.push_back({ "angle_parameter_to_rad", { *trim.angleScale } });
                }
                trimMapping = m_report.parameterMappings.size();
                m_report.parameterMappings.push_back(std::move(trace));
            }
            // A whole-period shift preserves the curve; kernel analytic angles
            // are degrees in [-360,360], while evaluators use a NURBS parameter.
            const auto sourceBranchBegin = t0;
            if (trimmed)
            {
                // Trimming can lift either endpoint several periods in either
                // direction. Bring the beginning into [0,2pi) before choosing
                // the kernel chart; the original source branch stays recorded.
                const double shift = 2 * std::numbers::pi * std::floor(t0 / (2 * std::numbers::pi));
                t0 -= shift;
                t1 -= shift;
            }
            if (t1 > 2 * std::numbers::pi)
            {
                t0 -= 2 * std::numbers::pi;
                t1 -= 2 * std::numbers::pi;
            }
            if (trimMapping)
            {
                auto& trace = m_report.parameterMappings[*trimMapping];
                trace.parameters.push_back({ "native_chart_period_shift_rad", { sourceBranchBegin - t0 } });
                trace.parameters.push_back({ "native_chart_angle_range_rad", { t0, t1 } });
            }
            mapping.kind = "ConicRationalParameter";
            mapping.equation =
                "source_angle=atan2(dot(P(native_t)-origin,y)/ry,dot(P(native_t)-origin,x)/rx),unwrapped_to_angle_range_rad; native_t_is_not_angle";
            mapping.parameters = { { "angle_range_rad", { t0, t1 } },
                                   { "radii_mm", { rx, ry } },
                                   { "origin_mm", { frame.o.begin(), frame.o.end() } },
                                   { "x", { frame.x.begin(), frame.x.end() } },
                                   { "y", { frame.y.begin(), frame.y.end() } } };
            const auto degrees = c.v0 == c.v1 ? FullPeriodDegrees(t0) :
                                                std::array<double, 2>{ t0 * 180 / std::numbers::pi, t1 * 180 / std::numbers::pi };
            const SmExtent1d domain(degrees[0], degrees[1]);
            if (trimmed && (!(domain.GetMax() > domain.GetMin()) || domain.GetMin() < -360 || domain.GetMax() > 360))
            {
                return tl::unexpected(Fail("GeneralTrimmedNativeAngleRangeUnestablished", c.geometry, Status::Unsupported));
            }
            auto nativeValue3 = MakeConic(g, frame, rx, ry, domain, trimmed);
            if (!nativeValue3)
            {
                return tl::unexpected(std::move(nativeValue3.error()));
            }
            c.native = std::move(*nativeValue3);
        }
        else if (Is(g, "B_SPLINE_CURVE_WITH_KNOTS"))
        {
            auto nativeValue4 = MakeSplineCurve(g, c.record);
            if (!nativeValue4)
            {
                return tl::unexpected(std::move(nativeValue4.error()));
            }
            c.native = std::move(*nativeValue4);
        }
        else
        {
            return tl::unexpected(Fail("GeneralUnsupportedCurve", g, Status::Unsupported));
        }
        if (auto businessCheck = Need(bool(c.native), "GeneralCurveAllocation", g); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        const auto domain = c.native->GetNaturalInterval();
        c.t0 = domain.GetMin();
        c.t1 = domain.GetMax();
        if (Is(g, "B_SPLINE_CURVE_WITH_KNOTS") || lifted)
        {
            // Source topology trims the defining spline. Inversion chooses
            // parameters only; the original spline coefficients are unchanged.
            if (!lifted)
            {
                mapping.kind = "SplineIdentityWithInvertedTrim";
                mapping.equation = "source_t=native_t; endpoint_inversion_only; defining_coefficients_unchanged";
            }
            auto nativeValue5 = CurveParameter(*c.native, a, c.t0, g);
            if (!nativeValue5)
            {
                return tl::unexpected(std::move(nativeValue5.error()));
            }
            c.t0 = std::move(*nativeValue5);
            auto nativeValue6 = CurveParameter(*c.native, b, c.t1, g);
            if (!nativeValue6)
            {
                return tl::unexpected(std::move(nativeValue6.error()));
            }
            c.t1 = std::move(*nativeValue6);
            if (auto businessCheck = Need(c.t1 > c.t0, "GeneralSplineParameterOrder", c.record); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        mapping.parameters.push_back({ "native_natural_interval", { domain.GetMin(), domain.GetMax() } });
        mapping.parameters.push_back({ "native_edge_interval", { c.t0, c.t1 } });
        m_report.parameterMappings.push_back(std::move(mapping));
        for (const auto& endpoint : { std::pair{ c.t0, a }, std::pair{ c.t1, b } })
        {
            SmPoint3d actual;
            if (auto operation = Step(c.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(c.native->EvaluatePoint(endpoint.first, actual)); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (endpoint.first == c.t0)
            {
                c.p0 = P(actual);
            }
            else
            {
                c.p1 = P(actual);
            }
            const auto gap = Norm(Sub(P(actual), endpoint.second));
            consistency = std::max(consistency, gap);
            if (auto businessCheck = Need(std::isfinite(gap) && gap <= Q(m_options.quality.consistencyMm), "GeneralEdgeVertexGap", c.record);
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        if (lifted)
        {
            for (const auto& comparison : comparisons)
            {
                if (auto observed = ObserveCurveImages(c, comparison, g); !observed)
                {
                    return tl::unexpected(std::move(observed.error()));
                }
            }
            // Already bounded by a complete positive surface control net.
            // SmCrvOnSurf's cached approximate 3D bbox is not a coverage proof.
            return {};
        }
        SmExtent3d box;
        if (auto operation = Step(g); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto nativeStatusCheck = Good(c.native->CalculateBoundingBox(SmExtent1d(c.t0, c.t1), &box)); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        c.bounds = { P(box.GetMin()), P(box.GetMax()) };
        return {};
    }
    [[nodiscard]] Result<void> ObserveCurveImages(const Curve& master, const Curve& other, Index record)
    {
        double maximum = 0;
        double minimumDot = 1;
        std::size_t directionPairs = 0;
        const auto observe = [&](const Curve& from, const Curve& to) -> Result<void>
        {
            SmTArray<double> knots;
            if (auto operation = Step(record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto good = Good(from.native->GetKnots(knots)); !good)
            {
                return tl::unexpected(std::move(good.error()));
            }
            std::vector<double> cuts{ from.t0, from.t1 };
            for (ULONG i = 0; i < knots.GetSize(); ++i)
            {
                if (knots[i] > from.t0 && knots[i] < from.t1)
                {
                    cuts.push_back(knots[i]);
                }
            }
            std::sort(cuts.begin(), cuts.end());
            cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
            for (Index span = 1; span < cuts.size(); ++span)
            {
                for (unsigned sample = 0; sample <= 8; ++sample)
                {
                    if (auto operation = Step(record); !operation)
                    {
                        return tl::unexpected(std::move(operation.error()));
                    }
                    SmVector3d fromValues[2], toValues[2];
                    if (auto good = Good(from.native->Evaluate(std::lerp(cuts[span - 1], cuts[span], sample / 8.), 1, TRUE, fromValues)); !good)
                    {
                        return tl::unexpected(std::move(good.error()));
                    }
                    const SmPoint3d point(fromValues[0].x, fromValues[0].y, fromValues[0].z);
                    if (auto operation = Step(record); !operation)
                    {
                        return tl::unexpected(std::move(operation.error()));
                    }
                    SmBoolean success = FALSE;
                    double parameter = 0, distance = 0;
                    if (auto good = Good(
                            to.native
                                ->DropPoint(SmExtent1d(to.t0, to.t1), point, nullptr, m_model->queryTolerance, nullptr, success, parameter, distance)
                        );
                        !good)
                    {
                        return tl::unexpected(std::move(good.error()));
                    }
                    if (!success || !std::isfinite(parameter) || parameter < to.t0 || parameter > to.t1)
                    {
                        return tl::unexpected(Fail("GeneralSurfaceCurveImageInversionUnestablished", record, Status::Indeterminate));
                    }
                    if (auto operation = Step(record); !operation)
                    {
                        return tl::unexpected(std::move(operation.error()));
                    }
                    if (auto good = Good(to.native->Evaluate(parameter, 1, TRUE, toValues)); !good)
                    {
                        return tl::unexpected(std::move(good.error()));
                    }
                    const SmPoint3d nearest(toValues[0].x, toValues[0].y, toValues[0].z);
                    const double gap = point.DistanceBetween(nearest);
                    if (!std::isfinite(gap))
                    {
                        return tl::unexpected(Fail("GeneralSurfaceCurveImageInversionUnestablished", record, Status::Indeterminate));
                    }
                    maximum = std::max(maximum, gap);
                    if (gap > Q(m_options.quality.consistencyMm))
                    {
                        return tl::unexpected(Fail("GeneralSurfaceCurveRepresentationsConflict", record));
                    }
                    if (sample != 0 && sample != 8 && parameter > to.t0 && parameter < to.t1)
                    {
                        const auto& a = fromValues[1];
                        const auto& b = toValues[1];
                        const double lengthA = std::hypot(a.x, a.y, a.z), lengthB = std::hypot(b.x, b.y, b.z);
                        if (!std::isfinite(lengthA) || !std::isfinite(lengthB) || !(lengthA > 0) || !(lengthB > 0))
                        {
                            return tl::unexpected(Fail("GeneralSurfaceCurveDirectionUnestablished", record, Status::Indeterminate));
                        }
                        const double rawDot = (a.x / lengthA) * (b.x / lengthB) + (a.y / lengthA) * (b.y / lengthB) +
                                              (a.z / lengthA) * (b.z / lengthB);
                        const double dot = from.forwardForComparison == to.forwardForComparison ? rawDot : -rawDot;
                        if (!std::isfinite(dot))
                        {
                            return tl::unexpected(Fail("GeneralSurfaceCurveDirectionUnestablished", record, Status::Indeterminate));
                        }
                        if (!(dot > 0))
                        {
                            return tl::unexpected(Fail("GeneralSurfaceCurveDirectionsConflict", record));
                        }
                        minimumDot = std::min(minimumDot, dot);
                        ++directionPairs;
                    }
                }
            }
            return {};
        };
        // Source master chooses the definition, not permission to ignore a
        // conflicting auxiliary. Different representations need not share t.
        // This is the existing development level of numerical observation:
        // bidirectional knot-span samples, never a continuous agreement bound.
        if (auto observed = observe(master, other); !observed)
        {
            return tl::unexpected(std::move(observed.error()));
        }
        if (auto observed = observe(other, master); !observed)
        {
            return tl::unexpected(std::move(observed.error()));
        }
        m_report.tolerances.push_back({ "SourceSurfaceCurveImages", "BidirectionalKnotSpanSampleMaximum", Id(record), maximum });
        if (!directionPairs)
        {
            return tl::unexpected(Fail("GeneralSurfaceCurveDirectionUnestablished", record, Status::Indeterminate));
        }
        ParameterMapping direction;
        direction.kind = "SurfaceCurveDirectionObservation";
        direction.sourceGeometryId = Id(record);
        direction.sourceBasisGeometryId = Id(other.geometry);
        direction.sourceUseId = Id(master.record);
        direction.targetEdgeIndex = ei.at(master.record);
        direction.equation =
            master.forwardForComparison == other.forwardForComparison ?
                "normalized tangents at bidirectional interior image samples; parameters may differ; not a continuous sense proof" :
                "normalized tangents mapped through declared wrapper/basis sense at bidirectional interior image samples; parameters may differ; not a continuous sense proof";
        direction.parameters = { { "sampled_tangent_pairs", { double(directionPairs) } }, { "minimum_unit_tangent_dot", { minimumDot } } };
        if (other.associationOccurrence)
        {
            direction.parameters.push_back({ "source_association_occurrence", { double(other.associationOccurrence) } });
        }
        m_report.parameterMappings.push_back(std::move(direction));
        return {};
    }
    [[nodiscard]] Result<double> CurveParameter(SmCurve& curve, V point, double endpoint, Index record)
    {
        SmPoint3d actual;
        if (auto operation = Step(record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto nativeStatusCheck = Good(curve.EvaluatePoint(endpoint, actual)); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        if (Norm(Sub(P(actual), point)) <= Q(m_options.quality.consistencyMm))
        {
            return endpoint;
        }
        SmBoolean success = FALSE;
        double t = 0, distance = 0;
        if (auto operation = Step(record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto nativeStatusCheck = Good(
                curve.DropPoint(curve.GetNaturalInterval(), P(point), nullptr, m_model->queryTolerance, nullptr, success, t, distance)
            );
            !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        if (auto businessCheck = Need(success && distance <= Q(m_options.quality.consistencyMm), "GeneralSplineEndpointInversion", record);
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        return t;
    }
    [[nodiscard]] Result<double> ConeAngle(Index surface) const
    {
        // Reuse the same checked physical angle for both chart planning and
        // native surface construction. The source unit chain scales the declared
        // semi-angle once; placement directions and native UV are not angles.
        auto angleScale = AngleToRadians();
        if (!angleScale)
        {
            return tl::unexpected(std::move(angleScale.error()));
        }
        auto angle = N(surface, "semi_angle");
        if (!angle)
        {
            return tl::unexpected(std::move(angle.error()));
        }
        return *angle * *angleScale;
    }
    [[nodiscard]] Result<SmExtent2d> ToroidalChart(const Face& f, const Frame& frame, double major, double minor) const
    {
        constexpr double tau = 2 * std::numbers::pi;
        std::map<Index, std::size_t> counts;
        double outerScore = 0, innerScore = 0;
        for (const auto& loop : f.loops)
        {
            for (const auto& use : loop.uses)
            {
                ++counts[use.edge];
                for (const auto& point : { curves[use.edge].p0, curves[use.edge].p1 })
                {
                    const auto delta = Sub(point, frame.o);
                    const auto rho = std::hypot(Dot(delta, frame.x), Dot(delta, frame.y)), z = Dot(delta, frame.z);
                    outerScore += std::abs(std::hypot(rho - major, z) - minor);
                    innerScore += std::abs(std::hypot(rho + major, z) - minor);
                    if (!std::isfinite(outerScore) || !std::isfinite(innerScore))
                    {
                        return tl::unexpected(Fail("GeneralToroidalChartUnestablished", f.surface, Status::Indeterminate));
                    }
                }
            }
        }
        // Only choose a full-period computational chart here. Vertex scores
        // are candidates, not source conformity, region or regularity proofs.
        // The signed generator bound and complete UV hull must prove the used
        // region later; no surface region is omitted by either full-period chart.
        const bool inner = innerScore < outerScore;
        const auto longitude = [&](const V& point)
        {
            const auto delta = Sub(point, frame.o);
            auto angle = std::atan2(Dot(delta, frame.y), Dot(delta, frame.x)) - (inner ? std::numbers::pi : 0);
            angle = std::fmod(angle, tau);
            return angle < 0 ? angle + tau : angle;
        };
        double cut = 0;
        bool seam = false;
        std::vector<double> angles;
        for (const auto& [edge, count] : counts)
        {
            const auto a = longitude(curves[edge].p0), b = longitude(curves[edge].p1);
            angles.push_back(a);
            angles.push_back(b);
            if (!seam && count == 2 && std::abs(std::remainder(a - b, tau)) < 1e-10)
            {
                seam = true;
                cut = a;
            }
        }
        if (!seam && !angles.empty())
        {
            std::sort(angles.begin(), angles.end());
            double largest = -1;
            for (std::size_t i = 0; i < angles.size(); ++i)
            {
                const auto next = i + 1 < angles.size() ? angles[i + 1] : angles[0] + tau;
                if (next - angles[i] > largest)
                {
                    largest = next - angles[i];
                    cut = std::fmod(angles[i] + largest / 2, tau);
                }
            }
        }
        const auto degrees = FullPeriodDegrees(cut > 0 ? cut - tau : cut);
        return SmExtent2d(degrees[0], inner ? 0 : -180, degrees[1], inner ? 360 : 180);
    }
    [[nodiscard]] Result<std::optional<SeamPlan>> FragmentedRingSeamCandidate(const Face& f)
    {
        if (!Is(f.surface, "CYLINDRICAL_SURFACE") || f.loops.size() != 2 || f.loops[0].uses.empty() || f.loops[1].uses.empty())
        {
            return {};
        }
        auto frameResult = Placement(Ref(f.surface, "position"));
        if (!frameResult)
        {
            return tl::unexpected(std::move(frameResult.error()));
        }
        const auto& frame = *frameResult;
        auto radiusResult = N(f.surface, "radius");
        if (!radiusResult)
        {
            return tl::unexpected(std::move(radiusResult.error()));
        }
        const double radius = *radiusResult * scale;
        std::array<double, 2> heights{}, winding{};
        for (Index ring = 0; ring < 2; ++ring)
        {
            std::set<Index> seen;
            const auto& loop = f.loops[ring];
            for (Index i = 0; i < loop.uses.size(); ++i)
            {
                const auto& use = loop.uses[i];
                const auto& curve = curves[use.edge];
                const auto circle = dynamic_cast<const SmCircle*>(curve.native.get());
                if (!Is(curve.geometry, "CIRCLE") || !circle || !seen.insert(use.edge).second)
                {
                    return {};
                }
                const auto& position = circle->GetPosition();
                const auto center = Sub(P(position.GetOriginRef()), frame.o);
                const auto axis = position.GetZAxis();
                const double height = Dot(center, frame.z), parallel = Dot(V{ axis.x, axis.y, axis.z }, frame.z);
                // This profile recognizes two complete, oppositely traversed
                // coaxial circular rings. It does not infer an annulus from an
                // arbitrary closed UV loop or replace a defining source curve.
                if (std::abs(std::abs(parallel) - 1) > 1e-12 || Norm(Sub(center, Mul(frame.z, height))) > Q(m_options.quality.definitionMm) ||
                    std::abs(circle->GetXRadius() - radius) > Q(m_options.quality.definitionMm) ||
                    std::abs(circle->GetYRadius() - radius) > Q(m_options.quality.definitionMm))
                {
                    return {};
                }
                if (i == 0)
                {
                    heights[ring] = height;
                }
                else if (std::abs(height - heights[ring]) > Q(m_options.quality.definitionMm))
                {
                    return {};
                }
                const auto& next = curves[loop.uses[(i + 1) % loop.uses.size()].edge];
                const auto end = use.forward ? curve.v1 : curve.v0;
                const auto start = loop.uses[(i + 1) % loop.uses.size()].forward ? next.v0 : next.v1;
                if (end != start)
                {
                    return {};
                }
                const double sweep = (use.forward ? 1 : -1) * (parallel > 0 ? 1 : -1) * circle->GetSTEPInterval().GetLength();
                // A net winding of one is insufficient when arcs backtrack.
                // Require one physical traversal sense as well as a full turn;
                // an inside-out analytic helper needs a separate convention.
                if (!std::isfinite(sweep) || sweep == 0 || circle->GetInsideOut() || (i && sweep * winding[ring] <= 0))
                {
                    return {};
                }
                winding[ring] += sweep;
            }
            // Angular roundoff only selects this representation profile; it
            // never enlarges a physical gap or bypasses native region checks.
            if (std::abs(std::abs(winding[ring]) - 360) > 1e-10 * loop.uses.size())
            {
                return {};
            }
        }
        if (winding[0] * winding[1] >= 0 || std::abs(heights[1] - heights[0]) <= Tol())
        {
            return {};
        }
        const auto firstEdge = f.loops[0].uses[0].edge;
        const auto& anchor = curves[firstEdge];
        const auto firstVertex = f.loops[0].uses[0].forward ? anchor.v0 : anchor.v1;
        const auto delta = Sub(points[firstVertex], frame.o);
        auto radialResult = Unit(Sub(delta, Mul(frame.z, Dot(delta, frame.z))));
        if (!radialResult)
        {
            return tl::unexpected(std::move(radialResult.error()));
        }
        const auto& radial = *radialResult;
        const auto a = Add(Add(frame.o, Mul(frame.z, heights[0])), Mul(radial, radius));
        const auto b = Add(Add(frame.o, Mul(frame.z, heights[1])), Mul(radial, radius));
        SeamPlan seam{ firstVertex, 0, a, b, std::atan2(Dot(radial, frame.y), Dot(radial, frame.x)), firstEdge, 0 };
        // Prefer an existing source vertex at the selected longitude. Otherwise
        // identify one interior arc; PrepareRingSubdivisions freezes its one
        // global subdivision before any cap or side adopts that source edge.
        for (const auto& use : f.loops[1].uses)
        {
            const auto& curve = curves[use.edge];
            for (const auto vertex : { curve.v0, curve.v1 })
            {
                if (Norm(Sub(b, points[vertex])) <= Q(m_options.quality.consistencyMm))
                {
                    seam.v1 = vertex;
                    seam.secondEdge = use.edge;
                    return seam;
                }
            }
        }
        const auto definition = m_options.quality.definitionMm;
        const double maximum = std::nextafter(
            std::nextafter(double(definition.Numerator()), 0.) / std::nextafter(double(definition.Denominator()), INFINITY),
            0.
        );
        bool found = false;
        for (const auto& use : f.loops[1].uses)
        {
            const auto& curve = curves[use.edge];
            SmBoolean projected = FALSE;
            double t = 0, gap = 0;
            if (auto operation = Step(curve.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto checked = Good(curve.native->DropPoint({ curve.t0, curve.t1 }, P(b), nullptr, maximum, nullptr, projected, t, gap)); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            if (projected && std::isfinite(t) && std::isfinite(gap) && gap >= 0 && gap <= maximum && t > curve.t0 && t < curve.t1)
            {
                if (found)
                {
                    return tl::unexpected(Fail("FragmentedRingAmbiguousCut", f.record, Status::Indeterminate));
                }
                found = true;
                seam.v1 = curve.v0; // Replaced by the shared derived vertex before construction.
                seam.secondEdge = use.edge;
            }
        }
        if (!found)
        {
            return tl::unexpected(Fail("FragmentedRingCutUnestablished", f.record, Status::Indeterminate));
        }
        return seam;
    }
    [[nodiscard]] Result<std::optional<SeamPlan>> RingSeamCandidate(const Face& f)
    {
        const bool cone = Is(f.surface, "CONICAL_SURFACE");
        if ((!Is(f.surface, "CYLINDRICAL_SURFACE") && !cone) || f.loops.size() != 2)
        {
            return {};
        }
        for (const auto& loop : f.loops)
        {
            if (loop.uses.size() != 1)
            {
                return FragmentedRingSeamCandidate(f);
            }
            const auto& c = curves[loop.uses[0].edge];
            if (!Is(c.geometry, "CIRCLE") || c.v0 != c.v1)
            {
                return {};
            }
        }
        const auto a = curves[f.loops[0].uses[0].edge].v0, b = curves[f.loops[1].uses[0].edge].v0;
        auto frameResult = Placement(Ref(f.surface, "position"));
        if (!frameResult)
        {
            return tl::unexpected(std::move(frameResult.error()));
        }
        const auto& frame = *frameResult;
        auto sourceRadiusResult = N(f.surface, "radius");
        if (!sourceRadiusResult)
        {
            return tl::unexpected(std::move(sourceRadiusResult.error()));
        }
        const auto& sourceRadius = *sourceRadiusResult;
        const auto radius = sourceRadius * scale;
        const auto da = Sub(points[a], frame.o), db = Sub(points[b], frame.o);
        const auto z0 = Dot(da, frame.z), z1 = Dot(db, frame.z);
        // CONICAL_SURFACE uses a physical radian semi-angle in the checked SI
        // context. Heights here are axial millimetres, as in Surface(): the
        // straight cut follows one fixed longitude on that defining cone.
        double slope = 0;
        if (cone)
        {
            auto angle = ConeAngle(f.surface);
            if (!angle)
            {
                return tl::unexpected(std::move(angle.error()));
            }
            slope = std::tan(*angle);
        }
        const double radius0 = radius + slope * z0, radius1 = radius + slope * z1;
        if (auto businessCheck = Need(
                std::isfinite(radius0) && std::isfinite(radius1) && radius0 > SM_EFF_ZERO && radius1 > SM_EFF_ZERO,
                "GeneralImplicitSeamRadius",
                f.record
            );
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        auto radialResult = Unit(Sub(da, Mul(frame.z, z0)));
        if (!radialResult)
        {
            return tl::unexpected(std::move(radialResult.error()));
        }
        const auto& radial = *radialResult;
        const auto pa = Add(Add(frame.o, Mul(frame.z, z0)), Mul(radial, radius0));
        const auto pb = Add(Add(frame.o, Mul(frame.z, z1)), Mul(radial, radius1));
        if (auto businessCheck = Need(std::abs(z1 - z0) > Tol(), "GeneralImplicitSeamHeight", f.record); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        return SeamPlan{ a, b, pa, pb, std::atan2(Dot(radial, frame.y), Dot(radial, frame.x)), f.loops[0].uses[0].edge, f.loops[1].uses[0].edge };
    }
    [[nodiscard]] Result<std::optional<SeamPlan>> MissingSeam(const Face& f)
    {
        if (const auto it = m_plannedSeams.find(f.record); it != m_plannedSeams.end())
        {
            return it->second;
        }
        auto seamBusiness = RingSeamCandidate(f);
        if (!seamBusiness)
        {
            return tl::unexpected(std::move(seamBusiness.error()));
        }
        auto seam = std::move(*seamBusiness);
        if (!seam)
        {
            return {};
        }
        // Preserve the historical aligned-ring path and its work/report bytes.
        // A different phase requires the separately proved global subdivision.
        if (Norm(Sub(seam->a, points[seam->v0])) > Q(m_options.quality.consistencyMm) ||
            Norm(Sub(seam->b, points[seam->v1])) > Q(m_options.quality.consistencyMm))
        {
            return tl::unexpected(Fail("GeneralImplicitSeamRequiresRingSplit", f.record, Status::Unsupported));
        }
        return seam;
    }
    [[nodiscard]] Result<Index> RegisterRingCut(Index source, double parameter, V point, Index faceRecord)
    {
        const auto& curve = curves[source];
        auto& plan = m_ringSubdivisions[source];
        plan.sourceCurve = source;
        plan.record = curve.record;
        plan.originalVertex = curve.v0;
        if (curve.v1 != curve.v0)
        {
            plan.originalEndVertex = curve.v1;
        }
        plan.interval = { curve.t0, curve.t1 };
        const auto existing = std::find_if(
            plan.cuts.begin(),
            plan.cuts.end(),
            [&](const auto& cut)
            {
                return cut.parameter == parameter;
            }
        );
        if (existing != plan.cuts.end())
        {
            if (auto checked = Need(
                    Norm(Sub(m_ringVertices.at(existing->vertex - vertices.size()).point, point)) == 0,
                    "RingSubdivisionSharedCutPointChanged",
                    faceRecord
                );
                !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            return existing->vertex;
        }
        const auto vertex = vertices.size() + m_ringVertices.size();
        m_ringVertices.push_back({ source, parameter, point, nullptr });
        plan.cuts.push_back({ parameter, vertex });
        return vertex;
    }
    [[nodiscard]] Result<void> PrepareMixedCylinderSeams(double maximum)
    {
        for (const auto& face : faces)
        {
            if (!Is(face.surface, "CYLINDRICAL_SURFACE") || face.loops.size() != 2 || m_plannedSeams.contains(face.record))
            {
                continue;
            }
            bool anchor = false, mixed = false, eligible = true;
            std::set<Index> included;
            std::vector<CylinderSeamCurve> definitions;
            std::array<std::vector<CylinderSeamUse>, 2> loops;
            for (Index i = 0; i < face.loops.size(); ++i)
            {
                for (const auto& use : face.loops[i].uses)
                {
                    const auto& curve = curves[use.edge];
                    const bool circle = Is(curve.geometry, "CIRCLE");
                    const bool other = Is(curve.geometry, "LINE") || Is(curve.geometry, "B_SPLINE_CURVE_WITH_KNOTS");
                    eligible = eligible && (circle || other);
                    mixed = mixed || other;
                    anchor = anchor || (face.loops[i].uses.size() == 1 && circle && curve.v0 == curve.v1);
                    loops[i].push_back({ use.edge, use.forward });
                    if (included.insert(use.edge).second)
                    {
                        definitions.push_back({ use.edge, curve.v0, curve.v1, curve.native.get(), { curve.t0, curve.t1 } });
                    }
                }
            }
            if (!eligible || !mixed || !anchor)
            {
                continue;
            }
            // Survey before ANY real face adopts these curves. A partial native
            // seam edit after construction would otherwise invalidate shared cap
            // edges and earlier ring provenance. The survey owns only deep copies.
            auto support = Surface(face);
            if (!support)
            {
                return tl::unexpected(std::move(support.error()));
            }
            const auto cylinder = dynamic_cast<const SmCylinder*>(support->get());
            if (!cylinder)
            {
                continue;
            }
            auto planned = PlanCylinderSeam(
                *cylinder,
                points,
                definitions,
                loops,
                *m_context,
                maximum,
                Tol(),
                m_options.sourceLimits.values,
                [&]
                {
                    return Step(face.record);
                }
            );
            if (!planned)
            {
                if (!planned.error().record && planned.error().nativeKind != NativeFailureKind::ConstructionOperation)
                {
                    planned.error().record = face.record;
                }
                return tl::unexpected(std::move(planned.error()));
            }
            if (!*planned)
            {
                continue;
            }
            const auto& cut = **planned;
            Index vertex = 0;
            if (cut.cutVertex)
            {
                vertex = *cut.cutVertex;
            }
            else
            {
                auto registered = RegisterRingCut(cut.cutEdge, cut.cutParameter, cut.last, face.record);
                if (!registered)
                {
                    return tl::unexpected(std::move(registered.error()));
                }
                vertex = *registered;
            }
            m_plannedSeams.emplace(
                face.record,
                SeamPlan{ cut.anchorVertex, vertex, cut.first, cut.last, cut.angleRadians, cut.anchorEdge, cut.cutEdge, true }
            );
            m_ringNormalization = true;
        }
        return {};
    }
    [[nodiscard]] Result<void> PrepareRingSubdivisions()
    {
        const auto definition = m_options.quality.definitionMm;
        const double maximum = std::nextafter(
            std::nextafter(double(definition.Numerator()), 0.) / std::nextafter(double(definition.Denominator()), INFINITY),
            0.
        );
        for (const auto& face : faces)
        {
            auto seamBusiness = RingSeamCandidate(face);
            if (!seamBusiness)
            {
                return tl::unexpected(std::move(seamBusiness.error()));
            }
            auto seam = std::move(*seamBusiness);
            if (!seam)
            {
                continue;
            }
            if (Is(face.surface, "CONICAL_SURFACE") &&
                (Norm(Sub(seam->a, points[seam->v0])) > maximum || Norm(Sub(seam->b, points[seam->v1])) > maximum))
            {
                auto frameResult = Placement(Ref(face.surface, "position"));
                if (!frameResult)
                {
                    return tl::unexpected(std::move(frameResult.error()));
                }
                const auto& frame = *frameResult;
                auto angle = ConeAngle(face.surface);
                if (!angle)
                {
                    return tl::unexpected(std::move(angle.error()));
                }
                auto radius = N(face.surface, "radius");
                if (!radius)
                {
                    return tl::unexpected(std::move(radius.error()));
                }
                const auto& first = curves[seam->firstEdge];
                const auto& second = curves[seam->secondEdge];
                const std::array<ConicalRingInput, 2> rings{
                    ConicalRingInput{ dynamic_cast<const SmCircle*>(first.native.get()), { first.t0, first.t1 }, points[first.v0] },
                    ConicalRingInput{ dynamic_cast<const SmCircle*>(second.native.get()), { second.t0, second.t1 }, points[second.v0] }
                };
                auto planned = PlanConicalRingCut(
                    { frame.o, frame.x, frame.y, frame.z, *radius * scale, std::tan(*angle) },
                    rings,
                    seam->angle,
                    maximum,
                    Q(m_options.quality.consistencyMm),
                    Tol(),
                    [&]
                    {
                        return Step(face.record);
                    }
                );
                if (!planned)
                {
                    return tl::unexpected(std::move(planned.error()));
                }
                if (*planned)
                {
                    const auto& cut = **planned;
                    std::array<Index, 2> verticesAtCut;
                    for (Index k = 0; k < 2; ++k)
                    {
                        const auto edge = k == 0 ? seam->firstEdge : seam->secondEdge;
                        auto registered = RegisterRingCut(edge, cut.parameters[k], cut.pointsMm[k], face.record);
                        if (!registered)
                        {
                            return tl::unexpected(std::move(registered.error()));
                        }
                        verticesAtCut[k] = *registered;
                        m_ringVertices.at(*registered - vertices.size()).consistencyConnection = true;
                    }
                    // Original points/curves do not move. Only NEW native cut
                    // vertices use the existing connection budget; locating
                    // parameters on the original circles still uses definitionMm.
                    seam->v0 = verticesAtCut[0];
                    seam->v1 = verticesAtCut[1];
                    seam->a = cut.pointsMm[0];
                    seam->b = cut.pointsMm[1];
                    seam->angle = cut.angleRadians;
                    seam->separatedSourceVertices = true;
                    m_plannedSeams[face.record] = *seam;
                    m_ringNormalization = true;
                    continue;
                }
            }
            if (face.loops[0].uses.size() != 1 || face.loops[1].uses.size() != 1)
            {
                m_ringNormalization = true;
                m_plannedSeams.emplace(face.record, *seam);
            }
            if (Norm(Sub(seam->a, points[seam->v0])) > Q(m_options.quality.consistencyMm))
            {
                return tl::unexpected(Fail("GeneralImplicitSeamRequiresRingSplit", face.record, Status::Unsupported));
            }
            if (Is(face.surface, "CONICAL_SURFACE"))
            {
                // A newly supported cone chart also owns its derived-cut
                // correspondence when both original circle phases already align.
                m_ringNormalization = true;
                m_plannedSeams.emplace(face.record, *seam);
            }
            if (Norm(Sub(seam->b, points[seam->v1])) <= Q(m_options.quality.consistencyMm))
            {
                continue;
            }
            if (auto businessCheck = Need(Norm(Sub(seam->a, points[seam->v0])) <= maximum, "RingSubdivisionAnchorDefinitionGap", face.record);
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            if (m_options.normalizePeriodicFaces || m_options.restrictUnusedSupportDomains || m_options.faceOrientationRepair)
            {
                return tl::unexpected(Fail("RingSubdivisionCompositionUnestablished", face.record, Status::Unsupported));
            }
            const auto source = seam->secondEdge;
            auto& curve = curves[source];
            double parameter = 0;

            auto nativeValue7 = LocateRingCut(
                *curve.native,
                { curve.t0, curve.t1 },
                seam->b,
                maximum,
                [&]
                {
                    return Step(curve.record);
                }
            );
            if (!nativeValue7)
            {
                if (!nativeValue7.error().record)
                {
                    nativeValue7.error().record = curve.record;
                }
                return tl::unexpected(std::move(nativeValue7.error()));
            }
            parameter = std::move(*nativeValue7);

            auto vertex = RegisterRingCut(source, parameter, seam->b, face.record);
            if (!vertex)
            {
                return tl::unexpected(std::move(vertex.error()));
            }
            seam->v1 = *vertex;
            m_plannedSeams[face.record] = *seam;
            m_ringNormalization = true;
        }
        if (auto operation = PrepareMixedCylinderSeams(maximum); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (!m_ringNormalization)
        {
            return {};
        }
        if (m_options.normalizePeriodicFaces || m_options.restrictUnusedSupportDomains || m_options.faceOrientationRepair)
        {
            return tl::unexpected(Fail("RingSubdivisionCompositionUnestablished", shape, Status::Unsupported));
        }
        for (const auto& face : faces)
        {
            auto pole = MissingSpherePole(face);
            if (!pole)
            {
                return tl::unexpected(std::move(pole.error()));
            }
            if (*pole)
            {
                return tl::unexpected(Fail("RingSubdivisionPoleCompositionUnestablished", face.record, Status::Unsupported));
            }
        }
        for (auto& [source, plan] : m_ringSubdivisions)
        {

            if (auto operation = PrepareRingSegments(
                    plan,
                    *curves[source].native,
                    *m_context,
                    [&]
                    {
                        return Step(plan.record);
                    }
                );
                !operation)
            {
                if (!operation.error().record)
                {
                    operation.error().record = plan.record;
                }
                return tl::unexpected(std::move(operation.error()));
            }
        }
        return {};
    }
    [[nodiscard]] Result<void> PrepareRingInventory()
    {
        if (!m_ringNormalization)
        {
            return {};
        }
        if (auto businessCheck = Need(!m_model->topologyNormalization && derivedPoles.empty(), "RingSubdivisionInventoryComposition", shape);
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        TopologyNormalizationInfo info;
        for (Index i = 0; i < faces.size(); ++i)
        {
            const auto domain = m_model->faces[i].face->GetSurface()->GetNaturalUVDomain();
            info.faces.push_back(
                { i, Id(faces[i].record), Id(faces[i].surface), { domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y }, {}, {} }
            );
            m_model->targetFaceSources.push_back(i);
        }
        const auto append = [&](SmEdge* edge, Index source, std::optional<Index> derived) -> Result<void>
        {
            if (auto businessCheck = Need(edge != nullptr, "RingSubdivisionMissingTargetEdge", shape); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            const auto interval = edge->GetInterval();
            const auto owner = derived.value_or(source < curves.size() ? curves[source].record : shape);
            if (auto businessCheck = Need(
                    m_ringEdgeOrdinals.emplace(edge, m_model->targetEdges.size()).second,
                    "RingSubdivisionRepeatedTargetEdge",
                    owner
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            info.edges.push_back(
                { m_model->targetEdges.size(),
                  source < curves.size() ? std::optional<std::string>(Id(curves[source].record)) : std::nullopt,
                  derived ? std::optional<std::string>(Id(*derived)) : std::nullopt,
                  { interval.GetMin(), interval.GetMax() } }
            );
            m_model->targetEdges.push_back(edge);
            m_model->targetEdgeOwners.push_back(owner);
            m_targetCurveSources.push_back(source);
            return {};
        };
        for (Index i = 0; i < curves.size(); ++i)
        {
            if (auto businessCheck = append(m_model->edges[i], i, {}); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        for (const auto& [source, plan] : m_ringSubdivisions)
        {
            for (Index i = 1; i < plan.segments.size(); ++i)
            {
                if (auto businessCheck = append(plan.segments[i].edge, source, {}); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
            }
        }
        for (Index i = curves.size(); i < m_model->edges.size(); ++i)
        {
            if (auto businessCheck = append(m_model->edges[i], curves.size(), derivedEdgeFaces.at(i - curves.size())); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        m_model->targetVertices = m_model->vertices;
        for (Index i = 0; i < vertices.size(); ++i)
        {
            info.vertices.push_back({ i, Id(vertices[i]), {}, {} });
            m_model->targetVertexOwners.push_back(vertices[i]);
        }
        for (const auto& cut : m_ringVertices)
        {
            const auto source = curves[cut.curve].record;
            info.vertices.push_back({ m_model->targetVertices.size(), {}, { { Id(source), cut.parameter } }, {} });
            m_model->targetVertices.push_back(cut.native);
            m_model->targetVertexOwners.push_back(source);
        }
        // Original full-basis mappings predate target segmentation. Do not let
        // their old ordinal falsely claim that one child spans the complete edge.
        // This is produced from the source-bound construction plan, not from a
        // caller-mutable report. The accepted snapshot owns the complete ledger.
        info.sourceUseSegments = m_ringUseOrigins;
        for (auto& mapping : m_report.parameterMappings)
        {
            if (mapping.targetEdgeIndex && m_ringSubdivisions.contains(*mapping.targetEdgeIndex))
            {
                mapping.targetEdgeIndex.reset();
                mapping.equation += "; target child intervals are recorded by topology_normalization and source_use_segments";
            }
        }
        m_model->topologyNormalization = std::move(info);
        m_report.topologyNormalization = m_model->topologyNormalization;
        return {};
    }
    [[nodiscard]] Result<void> CompleteRingSubdivisions()
    {
        if (!m_ringNormalization)
        {
            return {};
        }
        for (const auto& [source, plan] : m_ringSubdivisions)
        {

            if (auto operation = VerifyRingSegments(
                    plan,
                    [&]
                    {
                        return Step(curves[source].record);
                    }
                );
                !operation)
            {
                if (!operation.error().record)
                {
                    operation.error().record = plan.record;
                }
                return tl::unexpected(std::move(operation.error()));
            }

            for (const auto& segment : plan.segments)
            {
                if (auto businessCheck = Need(
                        segment.edge->GetStartVertex() == RingVertexAt(segment.firstVertex) &&
                            segment.edge->GetEndVertex() == RingVertexAt(segment.lastVertex),
                        "RingSubdivisionChangedEndpointIdentity",
                        plan.record
                    );
                    !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
            }
        }
        for (const auto& cut : m_ringVertices)
        {
            if (auto businessCheck = Need(
                    Norm(Sub(P(cut.native->GetPoint()), cut.point)) == 0,
                    "RingSubdivisionMovedDerivedVertex",
                    curves[cut.curve].record
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        for (Index i = 0; i < curves.size(); ++i)
        {
            if (!m_ringSubdivisions.contains(i))
            {
                const auto interval = m_model->edges[i]->GetInterval();
                if (auto businessCheck = Need(
                        interval.GetMin() == curves[i].t0 && interval.GetMax() == curves[i].t1,
                        "RingSubdivisionUnsplitSourceIntervalChanged",
                        curves[i].record
                    );
                    !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
            }
        }
        SmTArray<SmEdge*> edges;
        SmTArray<SmVertex*> nativeVertices;
        if (auto operation = Step(shape); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        m_model->brep->GetEdges(edges);
        if (auto operation = Step(shape); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        m_model->brep->GetVertices(nativeVertices);
        if (auto businessCheck = Need(
                edges.GetSize() == TargetEdges().size() && nativeVertices.GetSize() == TargetVertices().size(),
                "RingSubdivisionIncompleteInventory",
                shape
            );
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        std::set<SmEdge*> expectedEdges(TargetEdges().begin(), TargetEdges().end());
        std::set<SmVertex*> expectedVertices(TargetVertices().begin(), TargetVertices().end());
        for (const auto edge : edges)
        {
            if (auto businessCheck = Need(expectedEdges.erase(edge) == 1, "RingSubdivisionIncompleteInventory", shape); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        for (const auto vertex : nativeVertices)
        {
            if (auto businessCheck = Need(expectedVertices.erase(vertex) == 1, "RingSubdivisionIncompleteInventory", shape); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        if (auto businessCheck = Need(expectedEdges.empty() && expectedVertices.empty(), "RingSubdivisionIncompleteInventory", shape); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        auto& info = *m_model->topologyNormalization;
        // Verify the owning incidence ledger against the actual upward
        // (surface-normal SAME) faceuses, including cap and cylinder occurrences.
        // Source IDs are provenance, never unique keys for the target segments.
        std::map<std::tuple<std::string, Index, bool>, Index> actualUses;
        for (const auto& face : m_model->faces)
        {
            SmTArray<SmEdgeuse*> uses;
            if (auto operation = Step(shape); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            face.face->GetUpwardEdgeuses(uses);
            for (const auto use : uses)
            {
                const auto source = SourceCurveOf(use->GetEdge());
                if (source && m_ringSubdivisions.contains(*source))
                {
                    ++actualUses[{ face.faceId, m_ringEdgeOrdinals.at(use->GetEdge()), use->GetOrientation() == SM_OT_SAME }];
                }
            }
        }
        for (const auto& use : info.sourceUseSegments)
        {
            const auto& target = use.target;
            if (auto businessCheck = Need(
                    target.edge < TargetEdges().size() && target.startVertex < TargetVertices().size() && target.endVertex < TargetVertices().size(),
                    "RingSubdivisionUseTargetRange",
                    shape
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            const auto edge = TargetEdges()[target.edge];
            const auto interval = edge->GetInterval();
            if (auto businessCheck = Need(
                    info.edges[target.edge].sourceEdgeId == use.sourceEdgeId && interval.GetMin() == target.basisParameterInterval[0] &&
                        interval.GetMax() == target.basisParameterInterval[1] &&
                        (use.forwardInNativeCurve ? edge->GetStartVertex() : edge->GetEndVertex()) == TargetVertices()[target.startVertex] &&
                        (use.forwardInNativeCurve ? edge->GetEndVertex() : edge->GetStartVertex()) == TargetVertices()[target.endVertex],
                    "RingSubdivisionUseCorrespondenceChanged",
                    shape
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            const auto actual = actualUses.find({ use.sourceFaceId, target.edge, use.forwardInNativeCurve });
            if (auto businessCheck = Need(actual != actualUses.end() && actual->second > 0, "RingSubdivisionUseCorrespondenceChanged", shape);
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            --actual->second;
        }
        if (auto businessCheck = Need(
                std::all_of(
                    actualUses.begin(),
                    actualUses.end(),
                    [](const auto& use)
                    {
                        return use.second == 0;
                    }
                ),
                "RingSubdivisionUseCoverageIncomplete",
                shape
            );
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        info.sourceCurveIntervalsCovered = info.completed = true;
        info.sourceVerticesPreserved = m_pointOverrides.empty();
        m_model->derivedVertexConnections.clear();
        for (Index i = 0; i < m_ringVertices.size(); ++i)
        {
            const auto& cut = m_ringVertices[i];
            if (!cut.consistencyConnection)
            {
                continue;
            }
            const auto& basis = m_ringSubdivisions.at(cut.curve).reference;
            SmPoint3d onBasis;
            if (auto operation = Step(curves[cut.curve].record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto checked = Good(basis->EvaluatePoint(cut.parameter, onBasis)); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            const double residual = onBasis.DistanceBetween(cut.native->GetPoint());
            if (auto checked = Need(
                    std::isfinite(residual) && residual <= Q(m_options.quality.consistencyMm),
                    "ConicalDerivedVertexConnectionGap",
                    curves[cut.curve].record
                );
                !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            const auto sourceId = Id(curves[cut.curve].record);
            m_model->derivedVertexConnections.push_back({ vertices.size() + i, sourceId, cut.parameter, residual, m_options.quality.consistencyMm });
            m_report.tolerances.push_back({ "ConicalDerivedVertexConnection", "MeasuredResidualMm", sourceId, residual });
            m_report.tolerances.push_back(
                { "ConicalDerivedVertexConnection", "ExistingConsistencyBudgetMm", sourceId, Q(m_options.quality.consistencyMm) }
            );
        }
        m_report.topologyNormalization = info;
        return {};
    }
    std::optional<Index> SourceMeridianParameterCurve(const Curve& curve, const Face& face) const
    {
        if (!Is(curve.geometry, "SURFACE_CURVE"))
        {
            return std::nullopt;
        }
        const auto preference = F(curve.geometry, "master_representation").text;
        if (preference != "PCURVE_S1" && preference != "PCURVE_S2")
        {
            return std::nullopt;
        }
        const auto pcurve = Refs(curve.geometry, "associated_geometry").at(preference == "PCURVE_S1" ? 0 : 1);
        if (!Is(pcurve, "PCURVE") || Ref(pcurve, "basis_surface") != face.surface)
        {
            return std::nullopt;
        }
        const auto parameter = Refs(Ref(pcurve, "reference_to_curve"), "items").at(0);
        if (!Is(parameter, "B_SPLINE_CURVE_WITH_KNOTS") || Is(parameter, "RATIONAL_B_SPLINE_CURVE"))
        {
            return std::nullopt;
        }
        const auto controls = Refs(parameter, "control_points_list");
        const auto& first = F(controls.at(0), "coordinates").children.at(0).text;
        // Literal equality is a sufficient exact source proof of constant U.
        // Endpoints alone cannot prove a meridian; distinct decimals that round
        // to one binary64 must not acquire this chart-selection qualification.
        for (const auto point : controls)
        {
            if (F(point, "coordinates").children.at(0).text != first)
            {
                return std::nullopt;
            }
        }
        return parameter;
    }
    [[nodiscard]] Result<std::array<double, 2>> AngularWindow(const Face& f, const Frame& frame)
    {
        constexpr double tau = 2 * std::numbers::pi;
        if (const auto cut = m_vertexLoopPlans.find(f.record); cut != m_vertexLoopPlans.end())
        {
            const auto angle = cut->second.geometry.longitudeRadians;
            return FullPeriodDegrees(angle > 0 ? angle - tau : angle);
        }
        auto missing = MissingSeam(f);
        if (!missing)
        {
            return tl::unexpected(std::move(missing.error()));
        }
        if (const auto& seam = *missing)
        {
            const double start = seam->angle > 0 ? seam->angle - tau : seam->angle;
            return FullPeriodDegrees(start);
        }
        // STEP may already represent a periodic seam by two opposite uses of
        // one generatrix. Align the analytic domain to that exact source edge;
        // introducing an unrelated zero-angle seam would invalidate the trim.
        std::map<Index, Index> useCounts;
        for (const auto& loop : f.loops)
        {
            for (const auto& use : loop.uses)
            {
                ++useCounts[use.edge];
            }
        }
        for (const auto& [edge, count] : useCounts)
        {
            const auto& c = curves[edge];
            if (count == 2 && (Is(c.geometry, "LINE") || SourceMeridianParameterCurve(c, f)))
            {
                const auto a = Sub(c.p0, frame.o), b = Sub(c.p1, frame.o);
                const double t0 = std::atan2(Dot(a, frame.y), Dot(a, frame.x));
                const double t1 = std::atan2(Dot(b, frame.y), Dot(b, frame.x));
                if (std::abs(std::remainder(t0 - t1, tau)) > 1e-10)
                {
                    continue;
                }
                const double start = t0 > 0 ? t0 - tau : t0;
                return FullPeriodDegrees(start);
            }
        }
        bool splineBoundary = false;
        std::vector<double> meridianAngles;
        double maximumRadius = 0;
        for (const auto& [edge, count] : useCounts)
        {
            (void)count;
            const auto& c = curves[edge];
            splineBoundary = splineBoundary || Is(c.geometry, "B_SPLINE_CURVE_WITH_KNOTS");
            if (!Is(c.geometry, "LINE"))
            {
                continue;
            }
            for (const auto vertex : { c.v0, c.v1 })
            {
                const auto p = Sub(points[vertex], frame.o);
                const double x = Dot(p, frame.x), y = Dot(p, frame.y);
                meridianAngles.push_back(std::atan2(y, x));
                maximumRadius = std::max(maximumRadius, std::hypot(x, y));
            }
        }
        if (splineBoundary && meridianAngles.size() >= 4)
        {
            double x = 0, y = 0;
            for (const auto angle : meridianAngles)
            {
                x += std::cos(angle);
                y += std::sin(angle);
            }
            const double center = std::atan2(y, x);
            double spread = 0;
            for (const auto angle : meridianAngles)
            {
                spread = std::max(spread, std::abs(std::remainder(angle - center, tau)));
            }
            // A tolerant source may encode opposite seam sides as distinct
            // generatrices near one meridian. Align only the target chart to a
            // candidate inside their bounded angular spread. The 3D definitions
            // stay unchanged; UV deviation and the native seam checks still gate
            // publication. This does not weld the distinct source edges.
            if (maximumRadius > 0 && spread * maximumRadius <= Q(m_options.quality.derivedUvMm) / 4)
            {
                const double start = center > 0 ? center - tau : center;
                return FullPeriodDegrees(start);
            }
        }
        std::vector<std::pair<double, double>> cover;
        const auto normalized = [](double t)
        {
            double result = std::fmod(t, tau);
            return result < 0 ? result + tau : result;
        };
        for (const auto& loop : f.loops)
        {
            for (const auto& use : loop.uses)
            {
                const auto& c = curves[use.edge];
                if (Is(c.geometry, "CIRCLE"))
                {
                    auto crResult = Placement(Ref(c.geometry, "position"));
                    if (!crResult)
                    {
                        return tl::unexpected(std::move(crResult.error()));
                    }
                    const auto& cr = *crResult;
                    const auto dc = Sub(cr.o, frame.o);
                    const double parallel = Dot(cr.z, frame.z);
                    // Closed-form angular coverage applies only to coaxial circular
                    // arcs. Other intersections retain a full period and require a
                    // separate seam-normalization proof; sampling is not a proof.
                    if (std::abs(std::abs(parallel) - 1) > 1e-12 ||
                        Norm(Sub(dc, Mul(frame.z, Dot(dc, frame.z)))) > Q(m_options.quality.consistencyMm))
                    {
                        return std::array<double, 2>{ 0, 360 };
                    }
                    const auto a = Sub(points[c.v0], cr.o), b = Sub(points[c.v1], cr.o);
                    double t0 = std::atan2(Dot(a, cr.y), Dot(a, cr.x));
                    double t1 = std::atan2(Dot(b, cr.y), Dot(b, cr.x));
                    if (c.v0 == c.v1)
                    {
                        return std::array<double, 2>{ 0, 360 };
                    }
                    while (t1 <= t0)
                    {
                        t1 += tau;
                    }
                    const double offset = std::atan2(Dot(cr.x, frame.y), Dot(cr.x, frame.x));
                    const double start = normalized(offset + (parallel > 0 ? t0 : -t1)), length = t1 - t0;
                    if (start + length <= tau)
                    {
                        cover.push_back({ start, start + length });
                    }
                    else
                    {
                        cover.push_back({ start, tau });
                        cover.push_back({ 0, start + length - tau });
                    }
                }
                else if (Is(c.geometry, "LINE"))
                {
                    const auto a = Sub(points[c.v0], frame.o), b = Sub(points[c.v1], frame.o);
                    const double t0 = normalized(std::atan2(Dot(a, frame.y), Dot(a, frame.x)));
                    const double t1 = normalized(std::atan2(Dot(b, frame.y), Dot(b, frame.x)));
                    const double lower = std::min(t0, t1), upper = std::max(t0, t1);
                    const double start = upper - lower <= std::numbers::pi ? lower : upper;
                    const double span = std::min(upper - lower, tau - (upper - lower));
                    // A projected line lies in the convex angular cone spanned by
                    // its endpoints when that cone is narrower than pi. A tolerant
                    // source generatrix need not have identical endpoint angles.
                    if (span >= std::numbers::pi - 1e-10)
                    {
                        return std::array<double, 2>{ 0, 360 };
                    }
                    if (start + span <= tau)
                    {
                        cover.push_back({ start, start + span });
                    }
                    else
                    {
                        cover.push_back({ start, tau });
                        cover.push_back({ 0, start + span - tau });
                    }
                }
                else if (Is(c.geometry, "B_SPLINE_CURVE_WITH_KNOTS"))
                {
                    const auto curve = c.native ? c.native.get() : m_model->edges[use.edge]->GetCurve();
                    const auto bsp = dynamic_cast<const SmBSplineCurve*>(curve);
                    if (auto businessCheck = Need(bsp != nullptr, "GeneralSplineRepresentation", c.geometry); !businessCheck)
                    {
                        return tl::unexpected(std::move(businessCheck.error()));
                    }
                    auto knotsResult = Numbers(c.geometry, "knots");
                    if (!knotsResult)
                    {
                        return tl::unexpected(std::move(knotsResult.error()));
                    }
                    const auto& knots = *knotsResult;
                    for (ULONG k = 1; k < knots.GetSize(); ++k)
                    {
                        const double lo = knots[k - 1], hi = knots[k];
                        // Query strictly inside one knot span: GetPolygon returns
                        // its complete contributing control hull, not sampled curve
                        // points. A cone of width <pi is convex, hence bounds every
                        // point of this nonrational span including its endpoints.
                        const double a = lo + (hi - lo) * 0.25, b = lo + (hi - lo) * 0.75;
                        if (auto businessCheck = Need(a > lo && b < hi && b > a, "GeneralAngularHullPrecision", c.geometry); !businessCheck)
                        {
                            return tl::unexpected(std::move(businessCheck.error()));
                        }
                        SmTArray<SmPoint3d> polygon;
                        if (auto operation = Step(c.geometry); !operation)
                        {
                            return tl::unexpected(std::move(operation.error()));
                        }
                        if (auto nativeStatusCheck = Good(bsp->GetPolygon(SmExtent1d(a, b), polygon)); !nativeStatusCheck)
                        {
                            return tl::unexpected(std::move(nativeStatusCheck.error()));
                        }
                        std::vector<double> angles;
                        for (ULONG n = 0; n < polygon.GetSize(); ++n)
                        {
                            const auto p = Sub(P(polygon[n]), frame.o);
                            const double x = Dot(p, frame.x), y = Dot(p, frame.y);
                            if (std::hypot(x, y) <= Q(m_options.quality.definitionMm))
                            {
                                return std::array<double, 2>{ 0, 360 };
                            }
                            angles.push_back(normalized(std::atan2(y, x)));
                        }
                        std::sort(angles.begin(), angles.end());
                        if (angles.empty())
                        {
                            return std::array<double, 2>{ 0, 360 };
                        }
                        double gap = -1, start = 0;
                        for (Index n = 0; n < angles.size(); ++n)
                        {
                            const auto next = angles[(n + 1) % angles.size()] + (n + 1 == angles.size() ? tau : 0);
                            if (next - angles[n] > gap)
                            {
                                gap = next - angles[n];
                                start = normalized(next);
                            }
                        }
                        const double span = tau - gap;
                        if (span >= std::numbers::pi - 1e-10)
                        {
                            return std::array<double, 2>{ 0, 360 };
                        }
                        if (start + span <= tau)
                        {
                            cover.push_back({ start, start + span });
                        }
                        else
                        {
                            cover.push_back({ start, tau });
                            cover.push_back({ 0, start + span - tau });
                        }
                    }
                }
                else
                {
                    return std::array<double, 2>{ 0, 360 };
                }
            }
        }
        if (cover.empty())
        {
            return std::array<double, 2>{ 0, 360 };
        }
        std::sort(cover.begin(), cover.end());
        std::vector<std::pair<double, double>> merged;
        for (const auto& arc : cover)
        {
            if (merged.empty() || arc.first > merged.back().second + 1e-12)
            {
                merged.push_back(arc);
            }
            else
            {
                merged.back().second = std::max(merged.back().second, arc.second);
            }
        }
        double gap = -1, start = 0;
        for (Index i = 0; i < merged.size(); ++i)
        {
            const double next = merged[(i + 1) % merged.size()].first + (i + 1 == merged.size() ? tau : 0);
            const double length = next - merged[i].second;
            if (length > gap)
            {
                gap = length;
                start = normalized(next);
            }
        }
        if (gap <= 1e-10)
        {
            return std::array<double, 2>{ 0, 360 };
        }
        // Padding expands a computational window only. The source trims remain
        // unchanged; it adds neither a cap nor material to the imported body.
        start -= 1e-12;
        double end = start + tau - gap + 2e-12;
        if (end > tau)
        {
            start -= tau;
            end -= tau;
        }
        return std::array<double, 2>{ start * 180 / std::numbers::pi, end * 180 / std::numbers::pi };
    }
    [[nodiscard]] Result<std::unique_ptr<SmSurface>> HelicalCylinderChart(const Face& f, const Frame& frame)
    {
        if (f.loops.size() != 1 || f.loops[0].uses.empty())
        {
            return {};
        }
        Index longEdge = curves.size();
        double pitch = 0, originAngle = 0, reference = 0, guideSweep = 0;
        constexpr double tau = 2 * std::numbers::pi;
        const auto unwrapped = [&](const SmTArray<SmPoint3d>& cp)
        {
            std::vector<double> angles;
            for (ULONG i = 0; i < cp.GetSize(); ++i)
            {
                const auto p = Sub(P(cp[i]), frame.o);
                double a = std::atan2(Dot(p, frame.y), Dot(p, frame.x));
                if (!angles.empty())
                {
                    a = angles.back() + std::remainder(a - angles.back(), tau);
                }
                angles.push_back(a);
            }
            return angles;
        };
        // A chart gauge is not a fitted defining helix. It only changes which
        // cylinder points are covered by a finite computational rectangle.
        for (const auto& use : f.loops[0].uses)
        {
            const auto& c = curves[use.edge];
            if (!Is(c.geometry, "B_SPLINE_CURVE_WITH_KNOTS") && !Is(c.geometry, "LINE") && !Is(c.geometry, "CIRCLE"))
            {
                return {};
            }
            if (!Is(c.geometry, "B_SPLINE_CURVE_WITH_KNOTS"))
            {
                continue;
            }
            const auto native = c.native ? c.native.get() : m_model->edges[use.edge]->GetCurve();
            const auto bsp = dynamic_cast<const SmBSplineCurve*>(native);
            if (!bsp)
            {
                return {};
            }
            SmTArray<SmPoint3d> cp;
            SmTArray<double> weights;
            if (auto nativeStatusCheck = Good(bsp->GetControlPolygon(cp, weights)); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (cp.GetSize() < 2)
            {
                continue;
            }
            const auto angles = unwrapped(cp);
            const double turn = angles.back() - angles.front();
            if (std::abs(turn) < 1.5 * std::numbers::pi)
            {
                continue;
            }
            const double dz = Dot(Sub(P(cp[cp.GetSize() - 1]), P(cp[0])), frame.z);
            const double candidate = dz * tau / turn;
            if (std::abs(candidate) <= 2 * Tol())
            {
                continue;
            }
            if (std::abs(turn) <= guideSweep)
            {
                continue;
            }
            guideSweep = std::abs(turn);
            longEdge = use.edge;
            pitch = candidate;
            originAngle = *std::min_element(angles.begin(), angles.end()) - std::numbers::pi / 18;
        }
        if (longEdge == curves.size())
        {
            return {};
        }
        const auto toU = [&](double angle)
        {
            const double a = angle - originAngle, q = std::floor(a / (std::numbers::pi / 2));
            const double part = a - q * (std::numbers::pi / 2);
            const double left = std::sin(part / 2), right = std::sin((std::numbers::pi / 2 - part) / 2);
            return (q + left / (left + right)) / 4;
        };
        const auto& guide = curves[longEdge];
        const auto guideCurve = dynamic_cast<const SmBSplineCurve*>(guide.native ? guide.native.get() : m_model->edges[longEdge]->GetCurve());
        SmTArray<SmPoint3d> guideCp;
        SmTArray<double> guideWeights;
        if (auto nativeStatusCheck = Good(guideCurve->GetControlPolygon(guideCp, guideWeights)); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        const auto guideAngles = unwrapped(guideCp);
        reference = 0.5 * (Dot(Sub(P(guideCp[0]), frame.o), frame.z) + Dot(Sub(P(guideCp[guideCp.GetSize() - 1]), frame.o), frame.z) -
                           pitch * (toU(guideAngles.front()) + toU(guideAngles.back())));
        double lower = INFINITY, upper = -INFINITY;
        std::array<double, 5> upperByQuarter;
        upperByQuarter.fill(-INFINITY);
        for (const auto& use : f.loops[0].uses)
        {
            const auto& c = curves[use.edge];
            const auto native = c.native ? c.native.get() : m_model->edges[use.edge]->GetCurve();
            const auto curve = dynamic_cast<const SmBSplineCurve*>(native);
            if (!curve)
            {
                return {};
            }
            SmTArray<SmPoint3d> cp;
            SmTArray<double> weights, knots;
            if (auto nativeStatusCheck = Good(curve->GetControlPolygon(cp, weights)); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (auto nativeStatusCheck = Good(curve->GetKnotsAll(knots)); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (cp.GetSize() == 0)
            {
                return {};
            }
            auto angles = unwrapped(cp);
            double best = INFINITY, shift = 0;
            const auto [amin, amax] = std::minmax_element(angles.begin(), angles.end());
            double za = INFINITY, zb = -INFINITY;
            for (ULONG i = 0; i < cp.GetSize(); ++i)
            {
                const auto z = Dot(Sub(P(cp[i]), frame.o), frame.z);
                za = std::min(za, z);
                zb = std::max(zb, z);
            }
            for (int n = -3; n <= 3; ++n)
            {
                const double offset = n * tau;
                if (*amin + offset < originAngle || *amax + offset > originAngle + 2.5 * std::numbers::pi)
                {
                    continue;
                }
                const double score = std::abs(0.5 * (za + zb - pitch * (toU(*amin + offset) + toU(*amax + offset))) - reference);
                if (score < best)
                {
                    best = score;
                    shift = offset;
                }
            }
            if (!std::isfinite(best))
            {
                return {};
            }
            for (auto& angle : angles)
            {
                angle += shift;
            }
            double expectedAngle = angles.front();
            for (ULONG k = 1; k < knots.GetSize(); ++k)
            {
                const double a = std::max(knots[k - 1], c.t0), b = std::min(knots[k], c.t1);
                if (!(b > a))
                {
                    continue;
                }
                // Exact knot insertion on a private copy extracts one Bezier
                // span. The broad original basis hull can cover nearly half a
                // turn even when its actual span covers only a small arc.
                SmCurve* rawCopy = nullptr;
                if (auto operation = Step(c.geometry); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                const auto createdStatus = curve->Copy(*m_model->context, rawCopy);
                std::unique_ptr<SmCurve> copy(rawCopy);
                if (auto nativeStatusCheck = Good(createdStatus); !nativeStatusCheck)
                {
                    return tl::unexpected(std::move(nativeStatusCheck.error()));
                }
                if (auto businessCheck = Need(bool(copy), "GeneralCurveCopyAllocation", c.geometry); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                SmExtent1d interval(a, b);
                if (auto operation = Step(c.geometry); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (auto nativeStatusCheck = Good(copy->Trim(interval)); !nativeStatusCheck)
                {
                    return tl::unexpected(std::move(nativeStatusCheck.error()));
                }
                const auto bezier = dynamic_cast<const SmBSplineCurve*>(copy.get());
                if (!bezier)
                {
                    return {};
                }
                SmTArray<SmPoint3d> hull;
                SmTArray<double> hullWeights;
                if (auto nativeStatusCheck = Good(bezier->GetControlPolygon(hull, hullWeights)); !nativeStatusCheck)
                {
                    return tl::unexpected(std::move(nativeStatusCheck.error()));
                }
                auto hullAngles = unwrapped(hull);
                if (hullAngles.empty())
                {
                    return {};
                }
                const double turns = std::nearbyint((expectedAngle - hullAngles.front()) / tau);
                for (auto& angle : hullAngles)
                {
                    angle += turns * tau;
                }
                expectedAngle = hullAngles.back();
                double z0 = INFINITY, z1 = -INFINITY, t0 = INFINITY, t1 = -INFINITY;
                for (ULONG j = 0; j < hull.GetSize(); ++j)
                {
                    const double z = Dot(Sub(P(hull[j]), frame.o), frame.z);
                    z0 = std::min(z0, z);
                    z1 = std::max(z1, z);
                    t0 = std::min(t0, hullAngles[j]);
                    t1 = std::max(t1, hullAngles[j]);
                }
                // Positive-basis control hulls enclose Z and a convex angular
                // cone. Their product interval encloses the complete boundary,
                // not a set of sampled points. Reject an ambiguous angular cone.
                if (t1 - t0 >= std::numbers::pi - 1e-10)
                {
                    return {};
                }
                const double p0 = pitch * toU(t0), p1 = pitch * toU(t1);
                lower = std::min(lower, z0 - std::max(p0, p1));
                upper = std::max(upper, z1 - std::min(p0, p1));
                const double u0 = toU(t0), u1 = toU(t1);
                for (unsigned q = 0; q < 5; ++q)
                {
                    if (u1 >= double(q) / 4 && u0 <= double(q + 1) / 4)
                    {
                        upperByQuarter[q] = std::max(upperByQuarter[q], z1 - std::min(p0, p1));
                    }
                }
            }
        }
        const double padding = 4 * Tol();
        lower -= padding;
        upper += padding;
        const double height = upper - lower;
        if (!(height > 0))
        {
            return {};
        }
        std::array<double, 6> heights;
        heights.fill(height);
        if (height >= std::abs(pitch) - 2 * Tol())
        {
            if (pitch <= 0)
            {
                return {};
            }
            std::array<double, 5> required;
            for (unsigned q = 0; q < 5; ++q)
            {
                required[q] = std::max(8 * Tol(), std::isfinite(upperByQuarter[q]) ? upperByQuarter[q] + padding - lower : 8 * Tol());
            }
            heights[0] = required[0];
            heights[5] = required[4];
            for (unsigned q = 1; q < 5; ++q)
            {
                heights[q] = std::max(required[q - 1], required[q]);
            }
            // Only the first and fifth quarters repeat an angle. With positive
            // pitch, their axial intervals cannot overlap if the first quarter's
            // V width is smaller than one pitch. Widening the last quarter can
            // cover a thread termination without creating a duplicate 3D image.
            if (std::max(heights[0], heights[1]) >= pitch - 2 * Tol())
            {
                return {};
            }
        }
        auto sourceRadiusResult = N(f.surface, "radius");
        if (!sourceRadiusResult)
        {
            return tl::unexpected(std::move(sourceRadiusResult.error()));
        }
        const auto& sourceRadius = *sourceRadiusResult;
        const auto net = MakeCylinderGaugeNet({ frame.o, frame.x, frame.y, frame.z, sourceRadius * scale, originAngle, pitch, lower, heights });
        SmTArray<SmPoint3d> control;
        SmTArray<double> weights;
        for (const auto& point : net.pointsMm)
        {
            control.Add(P(point));
        }
        for (const auto weight : net.weights)
        {
            weights.Add(weight);
        }
        SmTArray<ULONG> um, vm;
        SmTArray<double> uk, vk;
        for (unsigned k = 0; k < 6; ++k)
        {
            um.Add(k == 0 || k == 5 ? 4 : 3);
            uk.Add(double(k) / 4);
        }
        vm.Add(2);
        vm.Add(2);
        vk.Add(0);
        vk.Add(1);
        SmBSplineSurface* raw = nullptr;
        if (auto operation = Step(f.surface); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const auto createdStatus = SmBSplineSurface::
            CreateCanonical(*m_model->context, 3, 1, control, SM_SF_UNSPECIFIED, um, vm, uk, vk, SM_KT_UNSPECIFIED, &weights, nullptr, raw);
        std::unique_ptr<SmSurface> owned(raw);
        if (auto nativeStatusCheck = Good(createdStatus); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        // Persist enough data to reconstruct the exact gauge and associate it
        // with its source support and target face. No hidden study parameters.
        ParameterMapping mapping;
        mapping.kind = "ExactCylinderHelicalGaugeChart";
        mapping.sourceGeometryId = Id(f.surface);
        mapping.sourceUseId = Id(f.record);
        mapping.targetFaceIndex = m_model->faces.size();
        mapping.equation =
            "native_u=(quarter+t)/4; angle=origin_angle+quarter*pi/2+atan2(2*w*t*(1-t)+t*t,(1-t)*(1-t)+2*w*t*(1-t)),w=sqrt(0.5); z=lower+pitch*native_u+lerp(width[quarter],width[quarter+1],t)*native_v";
        mapping.parameters = { { "origin_angle_rad", { originAngle } },
                               { "pitch_mm_per_native_u", { pitch } },
                               { "lower_mm", { lower } },
                               { "widths_mm", { heights.begin(), heights.end() } },
                               { "origin_mm", { frame.o.begin(), frame.o.end() } },
                               { "x", { frame.x.begin(), frame.x.end() } },
                               { "y", { frame.y.begin(), frame.y.end() } },
                               { "z", { frame.z.begin(), frame.z.end() } },
                               { "radius_mm", { sourceRadius * scale } },
                               { "native_domain", { 0, 0, 1.25, 1 } } };
        m_report.parameterMappings.push_back(std::move(mapping));
        RepresentationNormalization normalization;
        normalization.kind = "ExactCylinderHelicalGaugeChart";
        normalization.sourceFaceId = Id(f.record);
        for (const auto& loop : f.loops)
        {
            normalization.sourceBoundIds.push_back(Id(loop.bound));
        }
        m_report.normalizations.push_back(std::move(normalization));
        return owned;
    }
    [[nodiscard]] Result<void> PrepareVertexLoopCuts()
    {
        for (const auto& face : faces)
        {
            const auto point = std::find_if(
                face.loops.begin(),
                face.loops.end(),
                [](const Loop& loop)
                {
                    return loop.vertex.has_value();
                }
            );
            if (point == face.loops.end())
            {
                continue;
            }
            // First qualified target representation: one complete direct circle
            // and one declared apex on a cone. Other source point boundaries are
            // retained by semantics but cannot publish an invented native region.
            if (!Is(face.surface, "CONICAL_SURFACE") || face.loops.size() != 2 || point->outer || m_options.normalizePeriodicFaces ||
                m_options.restrictUnusedSupportDomains || m_options.faceOrientationRepair || m_options.cylindricalBandRepair)
            {
                return tl::unexpected(Fail("GeneralVertexLoopNormalizationUnestablished", point->record, Status::Unsupported));
            }
            const auto& ring = face.loops[point == face.loops.begin() ? 1 : 0];
            if (ring.vertex || ring.uses.size() != 1)
            {
                return tl::unexpected(Fail("GeneralVertexLoopNormalizationUnestablished", point->record, Status::Unsupported));
            }
            const auto& use = ring.uses[0];
            const auto& edge = curves[use.edge];
            const auto interval = edge.native->GetNaturalInterval();
            if (!Is(edge.geometry, "CIRCLE") || edge.v0 != edge.v1 || edge.v0 == *point->vertex || edge.t0 != interval.GetMin() ||
                edge.t1 != interval.GetMax())
            {
                return tl::unexpected(Fail("GeneralConicalVertexLoopBoundaryUnestablished", ring.record, Status::Unsupported));
            }
            auto frame = Placement(Ref(face.surface, "position"));
            auto circle = Placement(Ref(edge.geometry, "position"));
            auto radius = N(face.surface, "radius");
            auto ringRadius = N(edge.geometry, "radius");
            auto angle = ConeAngle(face.surface);
            if (!frame)
            {
                return tl::unexpected(std::move(frame.error()));
            }
            if (!circle)
            {
                return tl::unexpected(std::move(circle.error()));
            }
            if (!radius)
            {
                return tl::unexpected(std::move(radius.error()));
            }
            if (!ringRadius)
            {
                return tl::unexpected(std::move(ringRadius.error()));
            }
            if (!angle)
            {
                return tl::unexpected(std::move(angle.error()));
            }
            if (auto work = Step(face.record); !work)
            {
                return tl::unexpected(std::move(work.error()));
            }
            const auto inward = [](Fraction budget)
            {
                return std::nextafter(std::nextafter(double(budget.Numerator()), 0.) / std::nextafter(double(budget.Denominator()), INFINITY), 0.);
            };
            auto planned = PlanConicalVertexLoop(
                { frame->o,
                  frame->x,
                  frame->y,
                  frame->z,
                  circle->o,
                  circle->z,
                  points[edge.v0],
                  OriginalPoint(*point->vertex),
                  edge.p0,
                  edge.p1,
                  *radius * scale,
                  std::tan(*angle),
                  *ringRadius * scale,
                  use.forward },
                inward(m_options.quality.definitionMm),
                inward(m_options.quality.consistencyMm)
            );
            if (!planned)
            {
                auto failure = std::move(planned.error());
                failure.record = face.record;
                return tl::unexpected(std::move(failure));
            }
            m_vertexLoopPlans.emplace(face.record, VertexLoopPlan{ ring.bound, point->bound, point->record, edge.v0, *point->vertex, *planned });
        }
        return {};
    }
    [[nodiscard]] Result<std::optional<SpherePolePlan>> MissingSpherePole(const Face& face) const
    {
        if (!Is(face.surface, "SPHERICAL_SURFACE") || face.loops.size() != 1 || !face.loops[0].outer || face.loops[0].uses.size() != 1)
        {
            return {};
        }
        const auto& use = face.loops[0].uses[0];
        const auto& edge = curves[use.edge];
        if (edge.v0 != edge.v1 || !Is(edge.geometry, "CIRCLE"))
        {
            return {};
        }
        auto frameResult = Placement(Ref(face.surface, "position"));
        if (!frameResult)
        {
            return tl::unexpected(std::move(frameResult.error()));
        }
        const auto& frame = *frameResult;
        auto circleResult = Placement(Ref(edge.geometry, "position"));
        if (!circleResult)
        {
            return tl::unexpected(std::move(circleResult.error()));
        }
        const auto& circle = *circleResult;
        auto sourceRadiusResult = N(face.surface, "radius");
        if (!sourceRadiusResult)
        {
            return tl::unexpected(std::move(sourceRadiusResult.error()));
        }
        const auto& sourceRadius = *sourceRadiusResult;
        const double radius = sourceRadius * scale;
        auto sourceRingResult = N(edge.geometry, "radius");
        if (!sourceRingResult)
        {
            return tl::unexpected(std::move(sourceRingResult.error()));
        }
        const auto& sourceRing = *sourceRingResult;
        const double ring = sourceRing * scale;
        // Round the rational allowance inward, as for support restriction:
        // conversion to binary64 must not enlarge a normalization allowance.
        const auto allowance = m_options.quality.definitionMm;
        const double budget = std::nextafter(
            std::nextafter(double(allowance.Numerator()), 0.) / std::nextafter(double(allowance.Denominator()), INFINITY),
            0.
        );
        const auto offset = Sub(circle.o, frame.o);
        const double height = Dot(offset, frame.z), parallel = Dot(circle.z, frame.z);
        // This slice has one complete coaxial circular boundary. Closed-form
        // axis/center/radius checks establish the cap candidate; arbitrary loops,
        // tilted cuts and source pcurves do not acquire a guessed region.
        if (!(radius > SM_EFF_ZERO && ring > SM_EFF_ZERO && std::abs(height) < radius) || std::abs(std::abs(parallel) - 1) > 1e-12 ||
            Norm(Cross(circle.z, frame.z)) * radius > budget || Norm(Sub(offset, Mul(frame.z, height))) > budget ||
            std::abs(std::hypot(ring, height) - radius) > budget)
        {
            return {};
        }
        // Do not anchor a new seam to a merely tolerance-near source vertex:
        // this representation normalization uses the tighter definition budget.
        if (Norm(Sub(points[edge.v0], edge.p0)) > budget || Norm(Sub(points[edge.v0], edge.p1)) > budget)
        {
            return {};
        }
        const auto start = Sub(points[edge.v0], frame.o);
        const auto radialPart = Sub(start, Mul(frame.z, Dot(start, frame.z)));
        if (Norm(radialPart) <= SM_EFF_ZERO || std::abs(Norm(start) - radius) > budget)
        {
            return {};
        }
        // Use.forward already composes edge, bound and face senses. Increasing
        // sphere longitude leaves the north cap on the left; reversing the
        // circle axis reverses that longitude direction, not the material side.
        const int sign = use.forward == (parallel > 0) ? 1 : -1;
        auto radialResult = Unit(radialPart);
        if (!radialResult)
        {
            return tl::unexpected(std::move(radialResult.error()));
        }
        const auto& radial = *radialResult;
        return SpherePolePlan{ edge.v0,
                               frame,
                               Add(frame.o, Mul(frame.z, sign * radius)),
                               radial,
                               Mul(frame.z, sign),
                               radius,
                               std::atan2(sign * height, ring) * 180 / std::numbers::pi,
                               std::atan2(Dot(radial, frame.y), Dot(radial, frame.x)),
                               sign };
    }
    [[nodiscard]] Result<void> PreparePoleInventory()
    {
        if (derivedPoles.empty() && m_vertexLoopCuts.empty())
        {
            return {};
        }
        // Support-domain composition needs updated domain evidence for these
        // origins. The periodic pass separately distinguishes no-op requests
        // from actual splitting before it may mutate the target inventory.
        if (m_options.restrictUnusedSupportDomains)
        {
            return tl::unexpected(Fail("SphericalPoleNormalizationCompositionUnestablished", shape, Status::Unsupported));
        }
        if (m_ringNormalization)
        {
            if (auto checked = Need(
                    m_model->topologyNormalization.has_value() && derivedPoles.empty(),
                    "GeneralVertexLoopRingInventoryUnestablished",
                    shape
                );
                !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            // The shared ring builder owns the one-to-many edge inventory.
            // Rebind point-cut edges into THAT inventory, without overwriting
            // its original-basis subdivisions or source-use correspondence.
            for (auto& cut : m_vertexLoopCuts)
            {
                auto* seam = m_model->edges.at(cut.targetSeamEdge);
                const auto found = std::find(m_model->targetEdges.begin(), m_model->targetEdges.end(), seam);
                if (auto checked = Need(found != m_model->targetEdges.end(), "GeneralVertexLoopRingCutMissing", shape); !checked)
                {
                    return tl::unexpected(std::move(checked.error()));
                }
                cut.targetSeamEdge = static_cast<std::size_t>(found - m_model->targetEdges.begin());
            }
            m_model->topologyNormalization->vertexLoopCuts = m_vertexLoopCuts;
            return {};
        }
        TopologyNormalizationInfo info;
        info.vertexLoopCuts = m_vertexLoopCuts;
        for (Index i = 0; i < faces.size(); ++i)
        {
            const auto domain = m_model->faces[i].face->GetSurface()->GetNaturalUVDomain();
            TargetFaceOrigin origin;
            origin.target = i;
            origin.sourceFaceId = Id(faces[i].record);
            origin.sourceSurfaceId = Id(faces[i].surface);
            origin.nativeDomain = { domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y };
            info.faces.push_back(std::move(origin));
            m_model->targetFaceSources.push_back(i);
        }
        for (Index i = 0; i < m_model->edges.size(); ++i)
        {
            TargetEdgeOrigin origin;
            origin.target = i;
            const auto owner = i < curves.size() ? curves[i].record : derivedEdgeFaces.at(i - curves.size());
            if (i < curves.size())
            {
                origin.sourceEdgeId = Id(owner);
            }
            else
            {
                origin.derivedFromFaceId = Id(owner);
            }
            const auto interval = m_model->edges[i]->GetInterval();
            origin.basisParameterInterval = { interval.GetMin(), interval.GetMax() };
            info.edges.push_back(std::move(origin));
            m_model->targetEdgeOwners.push_back(owner);
        }
        m_model->targetEdges = m_model->edges;
        m_model->targetVertices = m_model->vertices;
        for (Index i = 0; i < vertices.size(); ++i)
        {
            TargetVertexOrigin origin;
            origin.target = i;
            origin.sourceVertexId = Id(vertices[i]);
            info.vertices.push_back(std::move(origin));
            m_model->targetVertexOwners.push_back(vertices[i]);
        }
        for (const auto& pole : derivedPoles)
        {
            TargetVertexOrigin origin;
            origin.target = m_model->targetVertices.size();
            origin.surfacePole = SurfacePoleOrigin{ Id(faces[pole.face].record), Id(faces[pole.face].surface), pole.sign };
            info.vertices.push_back(std::move(origin));
            m_model->targetVertices.push_back(pole.vertex);
            m_model->targetVertexOwners.push_back(faces[pole.face].record);
        }
        // Pending evidence remains private until source preservation, native
        // validation, final UV checks and the normal publication gate succeed.
        m_model->topologyNormalization = std::move(info);
        return {};
    }
    [[nodiscard]] Result<void> VerifyPoleInventory()
    {
        if (derivedPoles.empty() && m_vertexLoopCuts.empty())
        {
            return {};
        }
        SmTArray<SmFace*> actualFaces;
        SmTArray<SmEdge*> actualEdges;
        SmTArray<SmVertex*> actualVertices;
        if (auto operation = Step(shape); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        m_model->brep->GetFaces(actualFaces);
        if (auto operation = Step(shape); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        m_model->brep->GetEdges(actualEdges);
        if (auto operation = Step(shape); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        m_model->brep->GetVertices(actualVertices);
        std::vector<SmFace*> expectedFaces;
        for (const auto& face : m_model->faces)
        {
            expectedFaces.push_back(face.face);
        }
        const auto complete = [](const auto& expected, const auto& actual)
        {
            if (expected.size() != actual.GetSize())
            {
                return false;
            }
            std::set<const void*> remaining;
            for (const auto pointer : actual)
            {
                remaining.insert(pointer);
            }
            for (const auto pointer : expected)
            {
                if (remaining.erase(pointer) != 1)
                {
                    return false;
                }
            }
            return remaining.empty();
        };


        // Every live target must have exactly one origin. A native consumer
        // must not leave an untracked replacement/addition in a published model.
        if (auto businessCheck = Need(
                complete(expectedFaces, actualFaces) && complete(m_model->targetEdges, actualEdges) &&
                    complete(m_model->targetVertices, actualVertices),
                "SphericalPoleTargetInventoryChanged",
                shape
            );
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        for (auto& cut : m_vertexLoopCuts)
        {
            if (auto work = Step(faces.at(cut.targetFace).record); !work)
            {
                return tl::unexpected(std::move(work.error()));
            }
            const auto& plan = m_vertexLoopPlans.at(faces[cut.targetFace].record);
            auto* seam = m_model->targetEdges.at(cut.targetSeamEdge);
            auto* pole = m_model->targetVertices.at(cut.targetVertex);
            SmTArray<SmEdgeuse*> uses;
            m_model->faces[cut.targetFace].face->GetUpwardEdgeuses(uses);
            unsigned count = 0, orientations = 0;
            for (const auto* use : uses)
            {
                if (use->GetEdge() == seam)
                {
                    ++count;
                    orientations |= use->GetOrientation() == SM_OT_SAME ? 1U : use->GetOrientation() == SM_OT_OPPOSITE ? 2U : 0U;
                }
            }
            // This owning correspondence describes actual final topology, not
            // just the planned cut. Check it after every native consumer too.
            if (auto checked = Need(
                    count == 2 && orientations == 3 && pole == m_model->vertices.at(plan.poleVertex) &&
                        seam->GetStartVertex() == m_model->vertices.at(plan.baseVertex) && seam->GetEndVertex() == pole &&
                        seam->GetInterval().GetMin() == 0 && seam->GetInterval().GetMax() == 1 && seam->GetCurve()->GetOwner() == seam,
                    "GeneralVertexLoopCutIdentityChanged",
                    faces[cut.targetFace].record
                );
                !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            if (auto work = Step(faces[cut.targetFace].record); !work)
            {
                return tl::unexpected(std::move(work.error()));
            }
            SmTArray<SmPoint3d> polePoints;
            ULONG approximate = SM_SS_NONE;
            const auto singularities = m_model->faces[cut.targetFace].face->GetSurface()->GetSingularities(&polePoints, nullptr, &approximate);
            const std::array<ULONG, 4> sides{ SM_SS_UMIN, SM_SS_VMIN, SM_SS_UMAX, SM_SS_VMAX };
            const auto side = std::find(sides.begin(), sides.end(), singularities);
            if (auto checked = Need(
                    side != sides.end() && polePoints.GetSize() == 4 && approximate == singularities,
                    "GeneralVertexLoopNativePoleUnestablished",
                    faces[cut.targetFace].record
                );
                !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            const double residual = P(OriginalPoint(plan.poleVertex)).DistanceBetween(polePoints[static_cast<ULONG>(side - sides.begin())]);
            const auto budget = m_options.quality.consistencyMm;
            const double inward = std::nextafter(
                std::nextafter(double(budget.Numerator()), 0.) / std::nextafter(double(budget.Denominator()), INFINITY),
                0.
            );
            if (auto checked = Need(
                    std::isfinite(residual) && residual <= inward,
                    "GeneralVertexLoopNativeConnectionGap",
                    faces[cut.targetFace].record
                );
                !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            cut.observedPoleConnectionMm = residual;
        }
        m_model->topologyNormalization->vertexLoopCuts = m_vertexLoopCuts;
        return {};
    }
    [[nodiscard]] Result<std::unique_ptr<SmSurface>> Surface(const Face& f)
    {
        const auto g = f.surface;
        if (Is(g, "SURFACE_OF_REVOLUTION"))
        {
            double start = 0;
            std::map<Index, Index> uses;
            for (const auto& loop : f.loops)
            {
                for (const auto& use : loop.uses)
                {
                    ++uses[use.edge];
                }
            }
            for (const auto& [edge, count] : uses)
            {
                if (count != 2)
                {
                    continue;
                }
                const auto parameter = SourceMeridianParameterCurve(curves[edge], f);
                if (!parameter)
                {
                    continue;
                }
                const auto& value = F(Refs(*parameter, "control_points_list").at(0), "coordinates").children.at(0);
                auto number = Number(value);
                if (!number)
                {
                    return tl::unexpected(std::move(number.error()));
                }
                auto radians = AngleToRadians();
                if (!radians)
                {
                    return tl::unexpected(std::move(radians.error()));
                }
                const double degrees = *number * *radians * 180 / std::numbers::pi;
                if (!std::isfinite(degrees) || std::abs(degrees) > 360000000)
                {
                    return tl::unexpected(Fail("GeneralRevolutionAngularChartUnestablished", g, Status::Unsupported));
                }
                start = std::fmod(degrees, 360.);
                if (start > 0)
                {
                    start -= 360;
                }
                break;
            }
            auto surface = MakeSourceRevolution(*this, g, f.record, start);
            if (surface)
            {
                // Construction has not inserted this face yet. Bind the pure
                // definition's parameter evidence to its actual native use.
                m_report.parameterMappings.back().targetFaceIndex = m_model->faces.size();
            }
            return surface;
        }
        if (Is(g, "B_SPLINE_SURFACE_WITH_KNOTS"))
        {
            return MakeSourceSplineSurface(*this, g, f.record);
        }
        auto frameResult = Placement(Ref(g, "position"));
        if (!frameResult)
        {
            return tl::unexpected(std::move(frameResult.error()));
        }
        const auto& frame = *frameResult;
        // Finite computational windows must cover the entire defining boundary,
        // not just vertices (closed circles may have only one source vertex).
        // Project conservative curve boxes; this changes no trim or geometry.
        Bounds local{ V{ INFINITY, INFINITY, INFINITY }, V{ -INFINITY, -INFINITY, -INFINITY } };
        for (const auto& loop : f.loops)
        {
            if (loop.vertex)
            {
                const auto delta = Sub(OriginalPoint(*loop.vertex), frame.o);
                const V point{ Dot(delta, frame.x), Dot(delta, frame.y), Dot(delta, frame.z) };
                for (Index k = 0; k < 3; ++k)
                {
                    local[0][k] = std::min(local[0][k], point[k]);
                    local[1][k] = std::max(local[1][k], point[k]);
                }
            }
            for (const auto& use : loop.uses)
            {
                const auto& box = curves[use.edge].bounds;
                for (unsigned mask = 0; mask < 8; ++mask)
                {
                    V corner;
                    for (Index k = 0; k < 3; ++k)
                    {
                        corner[k] = box[(mask >> k) & 1U][k];
                    }
                    const auto d = Sub(corner, frame.o);
                    const V p{ Dot(d, frame.x), Dot(d, frame.y), Dot(d, frame.z) };
                    for (Index k = 0; k < 3; ++k)
                    {
                        local[0][k] = std::min(local[0][k], p[k]);
                        local[1][k] = std::max(local[1][k], p[k]);
                    }
                }
            }
        }
        const auto padding = std::max(
            Q(m_options.quality.definitionMm),
            64 * std::numeric_limits<double>::epsilon() * (1 + Norm(local[0]) + Norm(local[1]))
        );
        for (Index k = 0; k < 3; ++k)
        {
            local[0][k] -= padding;
            local[1][k] += padding;
        }
        if (Is(g, "PLANE"))
        {
            if (auto operation = Step(g); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            return std::unique_ptr<SmSurface>(new (*m_model->context) SmPlane(
                P(frame.o),
                D(frame.x),
                D(frame.y),
                SmVector2d(1, 1),
                SmExtent2d(local[0][0], local[0][1], local[1][0], local[1][1]),
                m_model->context.get()
            ));
        }
        if (Is(g, "CYLINDRICAL_SURFACE"))
        {
            auto chart = HelicalCylinderChart(f, frame);
            if (!chart)
            {
                return tl::unexpected(std::move(chart.error()));
            }
            if (*chart)
            {
                return std::move(*chart);
            }
            auto anglesResult = AngularWindow(f, frame);
            if (!anglesResult)
            {
                return tl::unexpected(std::move(anglesResult.error()));
            }
            const auto angles = std::move(*anglesResult);
            if (auto operation = Step(g); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            auto radiusResult = N(g, "radius");
            if (!radiusResult)
            {
                return tl::unexpected(std::move(radiusResult.error()));
            }
            const auto& radius = *radiusResult;
            return std::unique_ptr<SmSurface>(new (*m_model->context) SmCylinder(
                P(Add(frame.o, Mul(frame.z, local[0][2]))),
                D(frame.x),
                D(frame.y),
                radius * scale,
                angles[0],
                angles[1],
                local[1][2] - local[0][2],
                FALSE,
                FALSE,
                FALSE,
                m_model->context.get()
            ));
        }
        if (Is(g, "CONICAL_SURFACE"))
        {
            auto angleBusiness = ConeAngle(g);
            if (!angleBusiness)
            {
                return tl::unexpected(std::move(angleBusiness.error()));
            }
            const double angle = std::move(*angleBusiness);
            // The kernel cone's V is axial height, STEP's V is generatrix
            // distance. Physical construction uses radius/angle/frame directly;
            // no identity V mapping is asserted in the source report.
            if (auto operation = Step(g); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            auto sourceRadiusResult = N(g, "radius");
            if (!sourceRadiusResult)
            {
                return tl::unexpected(std::move(sourceRadiusResult.error()));
            }
            const auto& sourceRadius = *sourceRadiusResult;
            const double radius = sourceRadius * scale, slope = std::tan(angle);
            const auto vertexWindow = m_vertexLoopPlans.find(f.record);
            if (vertexWindow != m_vertexLoopPlans.end())
            {
                // The unbounded analytic cone determines its apex at
                // z=-radius/slope. An approximate source incidence cannot
                // truncate the COMPUTATIONAL support before that known pole.
                // This changes no source point, frame, angle or bound domain.
                const auto apex = vertexWindow->second.geometry.analyticApexHeightMm;
                local[0][2] = std::min(local[0][2], apex);
                local[1][2] = std::max(local[1][2], apex);
            }
            if (slope > 0)
            {
                local[0][2] = std::max(local[0][2], -radius / slope);
            }
            if (slope < 0)
            {
                local[1][2] = std::min(local[1][2], -radius / slope);
            }
            auto shifted = frame;
            shifted.o = Add(frame.o, Mul(frame.z, local[0][2]));
            auto anglesResult = AngularWindow(f, frame);
            if (!anglesResult)
            {
                return tl::unexpected(std::move(anglesResult.error()));
            }
            const auto angles = std::move(*anglesResult);
            double lowerRadius = std::max(0., radius + slope * local[0][2]);
            double upperRadius = std::max(0., radius + slope * local[1][2]);
            if (vertexWindow != m_vertexLoopPlans.end())
            {
                // Zero is an analytic identity at THIS cone's known apex,
                // not a heuristic collapse of a near-axis NURBS generator.
                // Record/check arithmetic cancellation at the old definition
                // allowance before giving the native constructor the zero end.
                const double residual = std::abs(radius + slope * (slope > 0 ? local[0][2] : local[1][2]));
                if (auto checked = Need(
                        std::isfinite(residual) && residual <= Q(m_options.quality.definitionMm),
                        "GeneralConicalVertexLoopApexRoundoff",
                        f.record
                    );
                    !checked)
                {
                    return tl::unexpected(std::move(checked.error()));
                }
                (slope > 0 ? lowerRadius : upperRadius) = 0;
                m_report.tolerances.push_back({ "ConicalSourceVertexLoopCut", "AnalyticApexRadiusCancellationMm", Id(f.record), residual });
                ParameterMapping window;
                window.kind = "ConicalVertexLoopAnalyticApexWindow";
                window.sourceGeometryId = Id(g);
                window.sourceUseId = Id(f.record);
                window.targetFaceIndex = m_model->faces.size();
                window.equation =
                    "finite native axial window includes z=-source_radius_mm/tan(physical_semi_angle); original source vertex is unchanged; STEP generatrix V is not native axial V";
                window.parameters = { { "source_origin_mm", { frame.o.begin(), frame.o.end() } },
                                      { "native_origin_mm", { shifted.o.begin(), shifted.o.end() } },
                                      { "source_radius_mm", { radius } },
                                      { "physical_semi_angle_radians", { angle } },
                                      { "native_axial_window_mm", { local[0][2], local[1][2] } } };
                m_report.parameterMappings.push_back(std::move(window));
            }
            SmCone* raw = nullptr;
            const auto createdStatus = SmCone::CreateCanonical(
                *m_model->context,
                shifted.Native(),
                lowerRadius,
                upperRadius,
                local[1][2] - local[0][2],
                raw
            );
            std::unique_ptr<SmCone> cone(raw);
            if (auto nativeStatusCheck = Good(createdStatus); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (auto businessCheck = Need(bool(cone), "GeneralConeAllocation", g); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            const auto domain = cone->GetSTEPUVDomain();
            if (auto operation = Step(g); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(cone->AdjustSTEPUVDomain(SmExtent2d(angles[0], domain.GetMin().y, angles[1], domain.GetMax().y)));
                !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            return cone;
        }
        if (Is(g, "SPHERICAL_SURFACE"))
        {
            auto sourceRadiusResult = N(g, "radius");
            if (!sourceRadiusResult)
            {
                return tl::unexpected(std::move(sourceRadiusResult.error()));
            }
            const auto& sourceRadius = *sourceRadiusResult;
            const double radius = sourceRadius * scale;
            // Source positive-length validation does not establish the kernel's
            // finite nondegenerate range. Never use its negative-radius inside-
            // out convention to encode source face orientation.
            if (!std::isfinite(radius) || radius <= SM_EFF_ZERO)
            {
                return tl::unexpected(Fail("GeneralSphericalKernelRadiusRange", g, Status::Unsupported));
            }
            SmSphere* raw = nullptr;
            if (auto operation = Step(g); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto status = SmSphere::CreateCanonical(*m_model->context, frame.Native(), radius, raw);
            std::unique_ptr<SmSphere> sphere(raw);
            if (auto nativeStatusCheck = Good(status); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (auto businessCheck = Need(sphere && sphere->GetRadius() == radius, "GeneralSphericalDefinitionChanged", g); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            auto poleResult = MissingSpherePole(f);
            if (!poleResult)
            {
                return tl::unexpected(std::move(poleResult.error()));
            }
            if (const auto& pole = *poleResult)
            {
                const auto angles = FullPeriodDegrees(pole->longitude > 0 ? pole->longitude - 2 * std::numbers::pi : pole->longitude);
                if (auto operation = Step(g); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (auto nativeStatusCheck = Good(sphere->AdjustSTEPUVDomain(SmExtent2d(angles[0], -90, angles[1], 90))); !nativeStatusCheck)
                {
                    return tl::unexpected(std::move(nativeStatusCheck.error()));
                }
                ParameterMapping mapping;
                mapping.kind = "ImplicitSphericalPoleChart";
                mapping.sourceGeometryId = Id(g);
                mapping.sourceUseId = Id(f.record);
                mapping.targetFaceIndex = m_model->faces.size();
                mapping.equation = "full-period target chart in degrees; source frame/radius unchanged; pole has no assigned source longitude";
                mapping.parameters = { { "native_analytic_domain_degrees", { angles[0], -90, angles[1], 90 } } };
                m_report.parameterMappings.push_back(std::move(mapping));
            }
            // Choose a full-period chart using the endpoint longitude of a
            // repeated source arc as a cut candidate. Endpoint agreement alone
            // does not prove the whole arc is a meridian: final seam/UV checks
            // still gate publication. Only the target chart cut moves; no source
            // seam is created/welded and the spherical locus is unchanged.
            std::map<Index, Index> counts;
            for (const auto& loop : f.loops)
            {
                for (const auto& use : loop.uses)
                {
                    ++counts[use.edge];
                }
            }
            for (const auto& [edge, count] : counts)
            {
                const auto& curve = curves[edge];
                if (count != 2 || !Is(curve.geometry, "CIRCLE"))
                {
                    continue;
                }
                const auto a = Sub(curve.p0, frame.o), b = Sub(curve.p1, frame.o);
                const double ax = Dot(a, frame.x), ay = Dot(a, frame.y), bx = Dot(b, frame.x), by = Dot(b, frame.y);
                const bool firstHasLongitude = std::hypot(ax, ay) > SM_EFF_ZERO;
                const bool lastHasLongitude = std::hypot(bx, by) > SM_EFF_ZERO;
                if (!firstHasLongitude && !lastHasLongitude)
                {
                    continue; // Neither pole establishes a unique longitude.
                }
                // A source seam may terminate at a pole. Its other endpoint
                // still supplies a chart-cut candidate; never assign atan2(0,0)
                // to the pole or move its source vertex to establish longitude.
                const double first = firstHasLongitude ? std::atan2(ay, ax) : std::atan2(by, bx);
                const double last = lastHasLongitude ? std::atan2(by, bx) : first;
                if (std::abs(std::remainder(first - last, 2 * std::numbers::pi)) > 1e-10)
                {
                    continue;
                }
                const auto angles = FullPeriodDegrees(first > 0 ? first - 2 * std::numbers::pi : first);
                if (auto operation = Step(g); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (auto nativeStatusCheck = Good(sphere->AdjustSTEPUVDomain(SmExtent2d(angles[0], -90, angles[1], 90))); !nativeStatusCheck)
                {
                    return tl::unexpected(std::move(nativeStatusCheck.error()));
                }
                ParameterMapping mapping;
                mapping.kind = "SphericalLongitudeChart";
                mapping.sourceGeometryId = Id(g);
                mapping.sourceUseId = Id(f.record);
                mapping.targetFaceIndex = m_model->faces.size();
                mapping.equation =
                    "target analytic longitude domain is degrees with one full period; source frame/radius unchanged; no source-pcurve or ordinary-NURBS-UV identity asserted";
                mapping.parameters = { { "native_analytic_domain_degrees", { angles[0], -90, angles[1], 90 } } };
                m_report.parameterMappings.push_back(std::move(mapping));
                break;
            }
            if (auto businessCheck = Need(sphere->GetRadius() == radius, "GeneralSphericalDefinitionChanged", g); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            // STEP u/v are angular parameters; the native analytic chart uses
            // degrees and its ordinary evaluator uses rational NURBS UV. The
            // source definition is retained; no identity UV mapping is asserted.
            // Existing seam, installed-trim and native checks must still pass.
            return sphere;
        }
        if (Is(g, "TOROIDAL_SURFACE"))
        {
            auto sourceMajorResult = N(g, "major_radius");
            if (!sourceMajorResult)
            {
                return tl::unexpected(std::move(sourceMajorResult.error()));
            }
            const auto& sourceMajor = *sourceMajorResult;
            const double major = sourceMajor * scale;
            auto sourceMinorResult = N(g, "minor_radius");
            if (!sourceMinorResult)
            {
                return tl::unexpected(std::move(sourceMinorResult.error()));
            }
            const auto& sourceMinor = *sourceMinorResult;
            const double minor = sourceMinor * scale;
            // Defaults handle nonsingular ring tori. An explicit restriction
            // request may construct a private full horn/spindle candidate, but
            // every used region must later prove regularity and original-basis
            // equivalence. These surface values are not EXPRESS violations.
            const bool spindle = major <= minor;
            if (!(std::isfinite(major) && std::isfinite(minor) && minor > 0 && major > 0) ||
                (spindle && (!m_options.restrictUnusedSupportDomains || f.loops.empty())))
            {
                return tl::unexpected(Fail("GeneralToroidalRingRequired", g, Status::Unsupported));
            }
            // The checked factory rejects radii at/below SM_EFF_ZERO. Its
            // direct constructor assumes a successful generating-circle
            // allocation and cannot safely handle that source-positive range.
            if (minor <= SM_EFF_ZERO || major <= SM_EFF_ZERO)
            {
                return tl::unexpected(Fail("GeneralToroidalKernelRadiusRange", g, Status::Unsupported));
            }
            // SmTorus's analytic chart uses degrees; its EvaluatePoint takes
            // the nonlinearly related NURBS UV. No source parameter is changed
            // or claimed identical to target UV. Existing source seams/edges
            // remain defining geometry; installed UV must pass the same gates.
            // A full analytic chart remains private until its actual UV region
            // can be extracted and continuously compared in RestrictSupports.
            // Center V on a candidate profile sheet to avoid an artificial cut.
            // This does not discard any source surface or authorize a
            // singular support. Defaults retain the earlier ring-only guard.
            auto domain = SmExtent2d(0, 0, 360, 360);
            if (spindle)
            {
                auto chart = ToroidalChart(f, frame, major, minor);
                if (!chart)
                {
                    return tl::unexpected(std::move(chart.error()));
                }
                domain = std::move(*chart);
            }
            SmTorus* raw = nullptr;
            if (auto operation = Step(g); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto status = SmTorus::CreateCanonical(*m_model->context, frame.Native(), major, minor, raw, FALSE, FALSE, nullptr, &domain);
            std::unique_ptr<SmTorus> torus(raw);
            if (auto nativeStatusCheck = Good(status); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (auto operation = Need(
                    torus && torus->GetMajorRadius() == major && torus->GetMinorRadius() == minor,
                    "GeneralToroidalDefinitionChanged",
                    g
                );
                !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (spindle)
            {
                m_pendingToroidalSupports.insert(f.record);
                // The analytic projection path can preserve an isocurve image
                // while losing its paired edge parameter. Project on the same
                // complete ordinary NURBS basis instead. The original analytic
                // object/angle converter remains owned until snapshot release.
                // This is still a private, unqualified full support: mandatory
                // region/radius/restriction proofs run before native acceptance.
                if (auto operation = Step(g); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                auto ordinary = std::unique_ptr<SmBSplineSurface>(new (*m_model->context) SmBSplineSurface(*torus));
                ordinary->SetOwner(nullptr);
                ordinary->SetBSplineSurfaceForm(SM_SF_UNSPECIFIED);
                m_toroidalOriginals.emplace(f.record, torus.get());
                m_model->retainedSupportDefinitions.push_back(std::move(torus));
                return ordinary;
            }
            return torus;
        }
        return tl::unexpected(Fail("GeneralUnsupportedSurface", g, Status::Unsupported));
    }
    [[nodiscard]] Result<void> AppendSourceUse(
        SmBrepConstructor& c,
        const Use& u,
        LoopMapping& mapping,
        std::optional<Index> startAt = {},
        std::optional<Index> onlySegment = {}
    )
    {
        auto& edge = curves[u.edge];
        if (auto found = m_ringSubdivisions.find(u.edge); found != m_ringSubdivisions.end())
        {
            auto& segments = found->second.segments;
            std::vector<Index> traversal(segments.size());
            std::iota(traversal.begin(), traversal.end(), 0);
            if (!u.forward)
            {
                std::reverse(traversal.begin(), traversal.end());
            }
            if (onlySegment)
            {
                if (auto checked = Need(*onlySegment < segments.size(), "RingSubdivisionSegmentRange", edge.record); !checked)
                {
                    return tl::unexpected(std::move(checked.error()));
                }
                traversal = { *onlySegment };
            }
            if (startAt)
            {
                const auto first = std::find_if(
                    traversal.begin(),
                    traversal.end(),
                    [&](Index i)
                    {
                        return (u.forward ? segments[i].firstVertex : segments[i].lastVertex) == *startAt;
                    }
                );
                if (auto businessCheck = Need(first != traversal.end(), "RingSubdivisionLoopStart", edge.record); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                std::rotate(traversal.begin(), first, traversal.end());
            }
            // Every incident source use expands through this one global plan.
            // Shared cap/side incidences therefore reuse the very same subedges.
            for (auto ordinal : traversal)
            {
                auto& segment = segments[ordinal];
                if (auto operation = Step(edge.record); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                const auto native = c.StartEdge(
                    u.forward ? SM_OT_SAME : SM_OT_OPPOSITE,
                    segment.edge,
                    RingVertexAt(segment.firstVertex),
                    RingVertexAt(segment.lastVertex)
                );
                if (auto businessCheck = Need(native && (!segment.edge || segment.edge == native), "RingSubdivisionSharedEdgeIdentity", edge.record);
                    !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                if (!segment.edge)
                {
                    native->SetTolerance(Tol(), FALSE);
                    if (auto operation = Step(edge.record); !operation)
                    {
                        return tl::unexpected(std::move(operation.error()));
                    }
                    const auto adopted = c.SetEdgeCurve(segment.curve.get(), FALSE);
                    const bool owned = segment.curve->GetOwner() == native;
                    if (owned)
                    {
                        segment.curve.release();
                    }
                    if (auto nativeStatusCheck = Good(adopted); !nativeStatusCheck)
                    {
                        return tl::unexpected(std::move(nativeStatusCheck.error()));
                    }
                    if (auto businessCheck = Need(owned, "RingSubdivisionCurveOwnership", edge.record); !businessCheck)
                    {
                        return tl::unexpected(std::move(businessCheck.error()));
                    }
                    if (auto operation = Step(edge.record); !operation)
                    {
                        return tl::unexpected(std::move(operation.error()));
                    }
                    if (auto nativeStatusCheck = Good(c.SetLimits(SmVertexNode(segment.interval[0]), SmVertexNode(segment.interval[1])));
                        !nativeStatusCheck)
                    {
                        return tl::unexpected(std::move(nativeStatusCheck.error()));
                    }
                    segment.edge = native;
                    if (ordinal == 0)
                    {
                        m_model->edges[u.edge] = native;
                    }
                }
                const auto firstVertex = u.forward ? segment.firstVertex : segment.lastVertex;
                const auto lastVertex = u.forward ? segment.lastVertex : segment.firstVertex;
                UseMapping use{ Id(u.source),
                                Id(edge.record),
                                firstVertex < vertices.size() ? Id(vertices[firstVertex]) : "",
                                lastVertex < vertices.size() ? Id(vertices[lastVertex]) : "",
                                u.forward,
                                false,
                                {},
                                {} };
                m_ringUseOrigins.push_back(
                    { Id(u.face),
                      Id(u.bound),
                      Id(u.loop),
                      Id(u.source),
                      Id(edge.record),
                      u.forward,
                      { RingTargetEdge(u.edge, ordinal), firstVertex, lastVertex, segment.interval } }
                );
                mapping.uses.push_back(std::move(use));
                if (auto operation = Step(edge.record); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (auto nativeStatusCheck = Good(c.EndEdge()); !nativeStatusCheck)
                {
                    return tl::unexpected(std::move(nativeStatusCheck.error()));
                }
            }
            return {};
        }
        const auto existing = m_model->edges[u.edge];
        if (auto operation = Step(edge.record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        auto native = c.StartEdge(u.forward ? SM_OT_SAME : SM_OT_OPPOSITE, existing, m_model->vertices[edge.v0], m_model->vertices[edge.v1]);
        if (auto businessCheck = Need(native != nullptr && (!existing || existing == native), "GeneralSharedEdgeIdentity", edge.record);
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (!existing)
        {
            native->SetTolerance(Tol(), FALSE);
            m_model->edges[u.edge] = native;
            if (auto operation = Step(edge.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto adoptedCurve = c.SetEdgeCurve(edge.native.get(), FALSE);
            const bool curveOwned = edge.native->GetOwner() == native;
            if (curveOwned)
            {
                edge.native.release();
            }
            if (auto nativeStatusCheck = Good(adoptedCurve); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (auto businessCheck = Need(curveOwned, "GeneralCurveOwnership", edge.record); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            if (auto operation = Step(edge.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(c.SetLimits(SmVertexNode(edge.t0), SmVertexNode(edge.t1))); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
        }
        mapping.uses.push_back(
            { Id(u.source),
              Id(edge.record),
              Id(vertices[u.forward ? edge.v0 : edge.v1]),
              Id(vertices[u.forward ? edge.v1 : edge.v0]),
              u.forward,
              false,
              {},
              {} }
        );
        if (auto operation = Step(edge.record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto nativeStatusCheck = Good(c.EndEdge()); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        return {};
    }
    [[nodiscard]] Result<void> AppendRingLoop(SmBrepConstructor& c, const Loop& loop, LoopMapping& mapping, Index startAt)
    {
        if (loop.uses.size() == 1)
        {
            return AppendSourceUse(c, loop.uses[0], mapping, startAt);
        }
        struct Entry
        {
            const Use* source;
            std::optional<Index> segment;
            Index firstVertex, lastVertex;
        };
        std::vector<Entry> traversal;
        for (const auto& use : loop.uses)
        {
            if (const auto found = m_ringSubdivisions.find(use.edge); found != m_ringSubdivisions.end())
            {
                const auto& segments = found->second.segments;
                for (Index i = 0; i < segments.size(); ++i)
                {
                    const Index index = use.forward ? i : segments.size() - i - 1;
                    const auto& part = segments[index];
                    traversal.push_back(
                        { &use, index, use.forward ? part.firstVertex : part.lastVertex, use.forward ? part.lastVertex : part.firstVertex }
                    );
                }
            }
            else
            {
                const auto& edge = curves[use.edge];
                traversal.push_back({ &use, {}, use.forward ? edge.v0 : edge.v1, use.forward ? edge.v1 : edge.v0 });
            }
        }
        const auto first = std::find_if(
            traversal.begin(),
            traversal.end(),
            [&](const Entry& entry)
            {
                return entry.firstVertex == startAt;
            }
        );
        if (auto checked = Need(first != traversal.end(), "FragmentedRingLoopStart", loop.record); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        // Rotate the COMPLETE ring traversal, not just the split arc's children.
        // Otherwise the arc's tail and head become adjacent across an unrelated
        // source arc and the cut loop silently loses its source connectivity.
        std::rotate(traversal.begin(), first, traversal.end());
        for (Index i = 0; i < traversal.size(); ++i)
        {
            if (auto checked = Need(
                    traversal[i].lastVertex == traversal[(i + 1) % traversal.size()].firstVertex,
                    "FragmentedRingTraversalDisconnected",
                    loop.record
                );
                !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            if (auto operation = AppendSourceUse(c, *traversal[i].source, mapping, {}, traversal[i].segment); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
        }
        return {};
    }
    [[nodiscard]] Result<SmEdge*> AppendSeamUse(SmBrepConstructor& c, const SeamPlan& seam, Index face, SmEdge* existing, bool forward)
    {
        if (auto operation = Step(face); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        auto edge = c.StartEdge(forward ? SM_OT_SAME : SM_OT_OPPOSITE, existing, RingVertexAt(seam.v0), RingVertexAt(seam.v1));
        if (auto businessCheck = Need(edge != nullptr && (!existing || edge == existing), "GeneralTargetSeamIdentity", face); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (!existing)
        {
            edge->SetTolerance(Tol(), FALSE);
            SmLine* raw = nullptr;
            if (auto operation = Step(face); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto createdStatus = SmLine::CreateLineSegment(*m_model->context, 3, P(seam.a), P(seam.b), raw);
            std::unique_ptr<SmLine> curve(raw);
            if (auto nativeStatusCheck = Good(createdStatus); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (auto operation = Step(face); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto status = c.SetEdgeCurve(curve.get(), FALSE);
            const bool adopted = curve->GetOwner() == edge;
            if (adopted)
            {
                curve.release();
            }
            if (auto nativeStatusCheck = Good(status); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (auto businessCheck = Need(adopted, "GeneralTargetSeamOwnership", face); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            if (auto operation = Step(face); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(c.SetLimits(SmVertexNode(0.), SmVertexNode(1.))); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            m_model->edges.push_back(edge);
            derivedEdgeFaces.push_back(face);
        }
        if (auto operation = Step(face); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto nativeStatusCheck = Good(c.EndEdge()); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        return edge;
    }
    [[nodiscard]] Result<SmEdge*> AppendPoleSeam(
        SmBrepConstructor& c,
        const SpherePolePlan& pole,
        SmVertex* vertex,
        Index face,
        SmEdge* existing,
        bool forward
    )
    {
        if (auto operation = Step(face); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        auto edge = c.StartEdge(forward ? SM_OT_SAME : SM_OT_OPPOSITE, existing, m_model->vertices[pole.baseVertex], vertex);
        if (auto businessCheck = Need(edge && (!existing || edge == existing), "SphericalPoleSeamIdentity", face); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (!existing)
        {
            edge->SetTolerance(Tol(), FALSE);
            const Frame meridian{ pole.frame.o, pole.radial, pole.toward, Cross(pole.radial, pole.toward) };
            const SmExtent1d angular(pole.latitudeDegrees, 90);
            SmCircle* raw = nullptr;
            if (auto operation = Step(face); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto status = SmCircle::CreateCanonical(*m_model->context, meridian.Native(), pole.radius, raw, &angular);
            std::unique_ptr<SmCircle> curve(raw);
            if (auto nativeStatusCheck = Good(status); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (auto businessCheck = Need(bool(curve), "SphericalPoleSeamAllocation", face); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            const auto interval = curve->GetNaturalInterval();
            if (auto operation = Step(face); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto attached = c.SetEdgeCurve(curve.get(), FALSE);
            const bool owned = curve->GetOwner() == edge;
            if (owned)
            {
                curve.release();
            }
            if (auto nativeStatusCheck = Good(attached); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (auto businessCheck = Need(owned, "SphericalPoleSeamOwnership", face); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            if (auto operation = Step(face); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(c.SetLimits(SmVertexNode(interval.GetMin()), SmVertexNode(interval.GetMax()))); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            m_model->edges.push_back(edge);
            derivedEdgeFaces.push_back(face);
        }
        if (auto operation = Step(face); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto nativeStatusCheck = Good(c.EndEdge()); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        return edge;
    }
    [[nodiscard]] Result<void> BuildFace(SmBrepConstructor& c, Face& f)
    {
        const auto vertexPlan = m_vertexLoopPlans.find(f.record);
        if (vertexPlan != m_vertexLoopPlans.end())
        {
            const auto ring = std::find_if(
                f.loops.begin(),
                f.loops.end(),
                [&](const Loop& loop)
                {
                    return loop.bound == vertexPlan->second.ringBound;
                }
            );
            std::rotate(f.loops.begin(), ring, ring + 1);
        }
        else if (
            std::none_of(
                f.loops.begin(),
                f.loops.end(),
                [](const Loop& loop)
                {
                    return loop.outer;
                }
            )
        )
        {
            // Bootstrap the kernel's first-loop representation with a candidate
            // envelope. This is not a region decision: ClassifyRegion must prove
            // unique containment and orientation before any model can publish.
            const auto envelope = [&](const Loop& loop)
            {
                Bounds box{ V{ INFINITY, INFINITY, INFINITY }, V{ -INFINITY, -INFINITY, -INFINITY } };
                for (const auto& use : loop.uses)
                {
                    for (Index k = 0; k < 3; ++k)
                    {
                        box[0][k] = std::min(box[0][k], curves[use.edge].bounds[0][k]);
                        box[1][k] = std::max(box[1][k], curves[use.edge].bounds[1][k]);
                    }
                }
                return Norm(Sub(box[1], box[0]));
            };
            const auto candidate = std::max_element(
                f.loops.begin(),
                f.loops.end(),
                [&](const Loop& a, const Loop& b)
                {
                    return envelope(a) < envelope(b);
                }
            );
            std::rotate(f.loops.begin(), candidate, candidate + 1);
        }
        NativeFaceData nf;
        nf.faceId = Id(f.record);
        if (auto operation = Step(f.record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        nf.face = c.StartFace(f.same ? SM_OT_SAME : SM_OT_OPPOSITE);
        if (auto businessCheck = Need(nf.face != nullptr, "GeneralFaceAllocation", f.record); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        nf.face->SetTolerance(Tol(), FALSE);
        auto surfaceResult = Surface(f);
        if (!surfaceResult)
        {
            return tl::unexpected(std::move(surfaceResult.error()));
        }
        auto surface = std::move(*surfaceResult);
        if (auto businessCheck = Need(bool(surface), "GeneralSurfaceAllocation", f.surface); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        const auto chartPlan = m_splineCharts->plans.find(f.surface);
        if (chartPlan != m_splineCharts->plans.end())
        {
            const SplineChartCallbacks callbacks{ [&]
                                                  {
                                                      return Step(f.record);
                                                  },
                                                  [&](std::size_t work)
                                                  {
                                                      return m_execution.SpendExactGeometryWork(m_report, work, f.record);
                                                  } };
            auto conditioned = ApplySplineSurfaceChart(*surface, chartPlan->second.chart, *m_context, callbacks);
            if (!conditioned)
            {
                if (!conditioned.error().record)
                {
                    conditioned.error().record = f.record;
                }
                return tl::unexpected(std::move(conditioned.error()));
            }
            // Apply before surface adoption and before any trims/gap caches
            // exist. InstallUv later projects physical 3D curves against THIS
            // surface; it never assumes a carrier's independent UV is identical.
            surface = std::move(*conditioned);
        }
        const auto domain = surface->GetNaturalUVDomain();
        if (auto operation = Step(f.surface); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const auto adoptedSurface = c.SetFaceSurface(surface.get(), domain);
        const bool surfaceOwned = surface->GetOwner() == nf.face;
        if (surfaceOwned)
        {
            surface.release();
        }
        if (auto nativeStatusCheck = Good(adoptedSurface); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        if (auto businessCheck = Need(surfaceOwned, "GeneralSurfaceOwnership", f.record); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (chartPlan != m_splineCharts->plans.end())
        {
            auto mapping = SplineChartMapping(chartPlan->second.chart, Id(f.surface), Id(f.record), false);
            mapping.targetFaceIndex = m_model->faces.size();
            const auto reportIndex = m_report.parameterMappings.size();
            m_report.parameterMappings.push_back(mapping);
            m_splineCharts->bindings.push_back(
                { f.record, chartPlan->second.witness, nf.face->GetSurface(), nullptr, nullptr, nf.face, std::move(mapping), reportIndex }
            );
        }
        if (auto* precisions = m_execution.TargetSurfaces())
        {
            if (auto operation = Step(f.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            auto applied = precisions->Apply(Id(Role("definition")), Id(shape), Id(f.record), Id(f.surface), *nf.face, m_model->faces.size());
            if (!applied)
            {
                return tl::unexpected(std::move(applied.error()));
            }
            if (*applied)
            {
                m_targetSurfacePrecisionActions.emplace_back(**applied, nf.face);
            }
        }
        auto poleResult = MissingSpherePole(f);
        if (!poleResult)
        {
            return tl::unexpected(std::move(poleResult.error()));
        }
        const auto& polePlan = *poleResult;
        std::optional<SeamPlan> seamPlan;
        if (!polePlan)
        {
            auto missing = MissingSeam(f);
            if (!missing)
            {
                return tl::unexpected(std::move(missing.error()));
            }
            seamPlan = std::move(*missing);
        }
        if (vertexPlan != m_vertexLoopPlans.end())
        {
            const auto& plan = vertexPlan->second;
            const auto& ring = f.loops[0];
            const auto& point = f.loops[1];
            if (auto operation = Step(f.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            auto* loop = c.StartLoop(SM_OT_SAME);
            if (auto checked = Need(loop != nullptr, "GeneralVertexLoopCutAllocation", f.record); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            nf.loops.push_back(loop);
            LoopMapping mapping{ Id(f.record), Id(ring.bound), Id(ring.record), ring.outer, ring.orientation, {} };
            if (auto operation = AppendSourceUse(c, ring.uses[0], mapping); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            // The singular source boundary becomes a visit to its original
            // vertex, through two opposite uses of ONE derived straight seam.
            // No source vertex, circular definition, unit, bound or sense moves.
            const auto seamIndex = m_model->edges.size();
            const SeamPlan cutSeam{ plan.baseVertex,
                                    plan.poleVertex,
                                    points[plan.baseVertex],
                                    points[plan.poleVertex],
                                    plan.geometry.longitudeRadians };
            auto seam = AppendSeamUse(c, cutSeam, f.record, nullptr, true);
            if (!seam)
            {
                return tl::unexpected(std::move(seam.error()));
            }
            if (auto reverse = AppendSeamUse(c, cutSeam, f.record, *seam, false); !reverse)
            {
                return tl::unexpected(std::move(reverse.error()));
            }
            if (auto operation = Step(f.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatus = Good(c.EndLoop()); !nativeStatus)
            {
                return tl::unexpected(std::move(nativeStatus.error()));
            }
            m_report.loops.push_back(std::move(mapping));
            m_report.loops.push_back({ Id(f.record), Id(point.bound), Id(point.record), point.outer, point.orientation, {} });
            m_vertexLoopCuts.push_back(
                { Id(f.record),
                  Id(point.bound),
                  Id(point.record),
                  Id(vertices[plan.poleVertex]),
                  m_model->faces.size(),
                  plan.poleVertex,
                  seamIndex,
                  point.orientation,
                  {},
                  m_options.quality.consistencyMm }
            );
            m_report.normalizations.push_back(
                { "ConicalSourceVertexLoopCut",
                  Id(f.record),
                  { Id(ring.bound), Id(point.bound) },
                  { Id(vertices[plan.baseVertex]), Id(vertices[plan.poleVertex]) },
                  seamIndex + RingExtraEdges() }
            );
            m_report.tolerances.push_back(
                { "ConicalSourceVertexLoopCut", "AnalyticDefinitionResidualMm", Id(f.record), plan.geometry.definitionResidualMm }
            );
            m_report.tolerances.push_back(
                { "ConicalSourceVertexLoopCut", "OriginalVertexAnalyticConnectionMm", Id(f.record), plan.geometry.vertexConnectionMm }
            );
        }
        else if (const auto& pole = polePlan)
        {
            if (auto operation = Step(f.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            auto vertex = c.StartVertex(P(pole->pole));
            if (auto businessCheck = Need(vertex != nullptr, "SphericalPoleVertexAllocation", f.record); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            vertex->SetTolerance(Tol(), FALSE);
            if (auto operation = Step(f.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(c.EndVertex()); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            derivedPoles.push_back({ vertex, m_model->faces.size(), pole->sign, pole->pole });
            if (auto operation = Step(f.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            auto loop = c.StartLoop(SM_OT_SAME);
            if (auto businessCheck = Need(loop != nullptr, "SphericalPoleLoopAllocation", f.record); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            nf.loops.push_back(loop);
            const auto& sourceLoop = f.loops[0];
            LoopMapping mapping{ Id(f.record), Id(sourceLoop.bound), Id(sourceLoop.record), sourceLoop.outer, sourceLoop.orientation, {} };
            if (auto operation = AppendSourceUse(c, sourceLoop.uses[0], mapping); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto seamIndex = m_model->edges.size();
            auto seamResult = AppendPoleSeam(c, *pole, vertex, f.record, nullptr, true);
            if (!seamResult)
            {
                return tl::unexpected(std::move(seamResult.error()));
            }
            auto seam = std::move(*seamResult);
            if (auto seamUse = AppendPoleSeam(c, *pole, vertex, f.record, seam, false); !seamUse)
            {
                return tl::unexpected(std::move(seamUse.error()));
            }
            if (auto operation = Step(f.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(c.EndLoop()); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            m_report.loops.push_back(std::move(mapping));
            m_report.normalizations.push_back({ "ImplicitSphericalPoleCut", Id(f.record), { Id(sourceLoop.bound) }, {}, seamIndex });
        }
        else if (const auto& seam = seamPlan)
        {
            // Cutting a cylindrical/conical annulus opens its chart into one loop. Both
            // source rings remain defining boundaries; the one derived seam is
            // used twice in opposite directions and contributes no material.
            if (auto operation = Step(f.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            auto loop = c.StartLoop(SM_OT_SAME);
            if (auto businessCheck = Need(loop != nullptr, "GeneralTargetSeamLoop", f.record); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            nf.loops.push_back(loop);
            LoopMapping mapping;
            const auto first = std::find_if(
                f.loops.begin(),
                f.loops.end(),
                [&](const auto& loop)
                {
                    return std::any_of(
                        loop.uses.begin(),
                        loop.uses.end(),
                        [&](const Use& use)
                        {
                            return use.edge == seam->firstEdge;
                        }
                    );
                }
            );
            const auto second = std::find_if(
                f.loops.begin(),
                f.loops.end(),
                [&](const auto& loop)
                {
                    return std::any_of(
                        loop.uses.begin(),
                        loop.uses.end(),
                        [&](const Use& use)
                        {
                            return use.edge == seam->secondEdge;
                        }
                    );
                }
            );
            if (auto businessCheck = Need(first != f.loops.end() && second != f.loops.end(), "RingSubdivisionSourceLoopIdentity", f.record);
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            if (auto operation = AppendRingLoop(c, *first, mapping, seam->v0); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto seamIndex = m_model->edges.size() + RingExtraEdges();
            auto edgeResult = AppendSeamUse(c, *seam, f.record, nullptr, true);
            if (!edgeResult)
            {
                return tl::unexpected(std::move(edgeResult.error()));
            }
            auto edge = std::move(*edgeResult);
            if (auto operation = AppendRingLoop(c, *second, mapping, seam->v1); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto seamUse = AppendSeamUse(c, *seam, f.record, edge, false); !seamUse)
            {
                return tl::unexpected(std::move(seamUse.error()));
            }
            if (!m_ringSubdivisions.empty())
            {
                mapping.faceId = Id(f.record);
                // Both source boundaries contribute to this target chart loop.
                // Individual source oriented-edge identities stay on each segment.
                m_report.loops.push_back(std::move(mapping));
            }
            if (auto operation = Step(f.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(c.EndLoop()); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            m_report.normalizations.push_back(
                { seam->separatedSourceVertices                       ? "ConicalCutAwayFromSourceVertexGaps" :
                  seam->mixedBoundary                                 ? "PeriodicCutFromMixedCylindricalBoundaries" :
                  first->uses.size() == 1 && second->uses.size() == 1 ? "PeriodicCutFromTwoSourceRings" :
                                                                        "PeriodicCutFromFragmentedCircularRings",
                  Id(f.record),
                  { Id(f.loops[0].bound), Id(f.loops[1].bound) },
                  { Id(vertices[curves[seam->firstEdge].v0]), Id(vertices[curves[seam->secondEdge].v0]) },
                  seamIndex }
            );
        }
        else
        {
            for (const auto& l : f.loops)
            {
                if (auto operation = Step(l.bound); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                auto loop = c.StartLoop(SM_OT_SAME);
                if (auto businessCheck = Need(loop != nullptr, "GeneralLoopAllocation", l.bound); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                nf.loops.push_back(loop);
                LoopMapping mapping{ Id(f.record), Id(l.bound), Id(l.record), l.outer, l.orientation, {} };
                for (const auto& u : l.uses)
                {
                    if (auto operation = AppendSourceUse(c, u, mapping); !operation)
                    {
                        return tl::unexpected(std::move(operation.error()));
                    }
                }
                m_report.loops.push_back(std::move(mapping));
                if (auto operation = Step(l.bound); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (auto nativeStatusCheck = Good(c.EndLoop()); !nativeStatusCheck)
                {
                    return tl::unexpected(std::move(nativeStatusCheck.error()));
                }
            }
        }
        if (auto operation = Step(f.record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto nativeStatusCheck = Good(c.EndFace()); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        m_model->faces.push_back(std::move(nf));
        return {};
    }
    [[nodiscard]] Result<void> InstallUv()
    {
        for (Index i = 0; i < faces.size(); ++i)
        {
            const auto& f = faces[i];
            auto& native = m_model->faces[i];
            double controlledProjectionGap = 0;
            const bool cylinderProjection = Is(f.surface, "CYLINDRICAL_SURFACE");
            const bool coneProjection = Is(f.surface, "CONICAL_SURFACE");
            const bool splineProjection = Is(f.surface, "B_SPLINE_SURFACE_WITH_KNOTS");
            const bool revolutionProjection = Is(f.surface, "SURFACE_OF_REVOLUTION");
            const bool toroidalProjection = m_options.restrictUnusedSupportDomains && Is(f.surface, "TOROIDAL_SURFACE");
            if (cylinderProjection || coneProjection || splineProjection || revolutionProjection || toroidalProjection)
            {
                SmTArray<SmEdgeuse*> uses;
                native.face->GetUpwardEdgeuses(uses);
                for (ULONG k = 0; k < uses.GetSize(); ++k)
                {
                    const auto edge = uses[k]->GetEdge();
                    const auto source = SourceCurveOf(edge);
                    if (!source || uses[k]->GetUVTrimCurve())
                    {
                        continue;
                    }
                    const auto index = *source;
                    const bool closed = !m_ringSubdivisions.empty() ? edge->GetStartVertex() == edge->GetEndVertex() :
                                                                      curves[index].v0 == curves[index].v1;
                    if ((closed && !toroidalProjection) ||
                        !(Is(curves[index].geometry, "CIRCLE") || Is(curves[index].geometry, "B_SPLINE_CURVE_WITH_KNOTS") ||
                          Is(curves[index].geometry, "SURFACE_CURVE")))
                    {
                        continue;
                    }
                    // Spline supports and analytic cylinders/cones/revolutions share this
                    // bounded derived-data path. Legacy UV retry may enlarge
                    // edge tolerance even for an exact on-surface source curve.
                    // Keep the defining spline and its parameter interval; a
                    // single paired candidate must satisfy the unchanged budget.
                    const auto tolerance = SmTol::GetZoneTol3d(edge);
                    auto projectedResult = ProjectDerivedUv(
                        *uses[k],
                        Q(m_options.quality.derivedUvMm),
                        [&]
                        {
                            return Step(f.record);
                        },
                        // Implicit master curves can have nonaffine physical
                        // parameters even on a cylinder. Project onto the same
                        // checked NURBS basis instead of assuming an analytic
                        // line's parameter map. All paired gap guards remain.
                        toroidalProjection || Is(curves[index].geometry, "SURFACE_CURVE")
                    );
                    if (!projectedResult)
                    {
                        return tl::unexpected(std::move(projectedResult.error()));
                    }
                    auto projected = std::move(*projectedResult);
                    if (auto businessCheck = Need(SmTol::GetZoneTol3d(edge) == tolerance, "GeneralProjectionChangedWorkTolerance", f.record);
                        !businessCheck)
                    {
                        return tl::unexpected(std::move(businessCheck.error()));
                    }
                    if (projected.status != DerivedUvStatus::Ready)
                    {
                        continue;
                    }
                    if (auto operation = Step(f.record); !operation)
                    {
                        return tl::unexpected(std::move(operation.error()));
                    }
                    const auto candidate = projected.curve.get();
                    uses[k]->SetUVTrimCurve(candidate, 0., TRUE);
                    const bool adopted = candidate->GetOwner() == uses[k] || candidate->GetOwner() == uses[k]->GetMate();
                    if (adopted)
                    {
                        projected.curve.release();
                    }
                    if (auto businessCheck = Need(adopted && uses[k]->GetUVTrimCurve() == candidate, "GeneralProjectedUvOwnership", f.record);
                        !businessCheck)
                    {
                        return tl::unexpected(std::move(businessCheck.error()));
                    }
                    controlledProjectionGap = std::max(controlledProjectionGap, *projected.projectionGapMm);
                    // Preserve the defining curve and its native parameter t.
                    // Face closure/seam handling and final paired measurements
                    // still run below, followed by global topology/tolerance checks.
                    const char* kind = Is(curves[index].geometry, "SURFACE_CURVE")             ? "ControlledSourcePcurveUvProjection" :
                                       toroidalProjection                                      ? "ControlledToroidalCurveUvProjection" :
                                       splineProjection                                        ? "ControlledSplineSurfaceUvProjection" :
                                       revolutionProjection                                    ? "ControlledRevolutionUvProjection" :
                                       coneProjection                                          ? "ControlledConeUvProjection" :
                                       Is(curves[index].geometry, "B_SPLINE_CURVE_WITH_KNOTS") ? "ControlledCylinderSplineUvProjection" :
                                                                                                 "ControlledCircleUvProjection";
                    const auto targetIndex = !m_ringSubdivisions.empty() ? m_ringEdgeOrdinals.at(edge) : index;
                    RepresentationNormalization normalization{ kind, Id(f.record), {}, {}, targetIndex };
                    for (const auto& loop : f.loops)
                    {
                        normalization.sourceBoundIds.push_back(Id(loop.bound));
                    }
                    m_report.normalizations.push_back(std::move(normalization));
                    m_report.tolerances.push_back(
                        { kind, "RequestedProjectionAccuracy", Id(curves[index].record), Q(m_options.quality.derivedUvMm) / 8 }
                    );
                    m_report.tolerances.push_back({ kind, "ProjectionSampleMaximum", Id(curves[index].record), *projected.projectionGapMm });
                    m_report.tolerances.push_back({ kind, "RawPairedParameterSampleMaximum", Id(curves[index].record), projected.rawPair->maximumMm });
                    m_rebuiltUv = true;
                }
            }
            double mean = 0, max = 0, meanVertex = 0, maxVertex = 0, meanParam = 0, maxParam = 0;
            if (auto operation = Step(f.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(
                    native.face->CreateUVTrimCurves(FALSE, nullptr, nullptr, mean, max, meanVertex, maxVertex, meanParam, maxParam)
                );
                !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            // Cached preinstalled trims do not contribute to the face builder's
            // new-projection statistic. Include their observed normal gap while
            // preserving invalid native observations as failures.
            if (std::isfinite(max) && max >= 0)
            {
                max = std::max(max, controlledProjectionGap);
            }
            // FALSE forbids working-tolerance changes in the current kernel.
            // Older SDK retries did not honor that contract; ObserveTolerances
            // independently rejects any actual budget excess on every runtime.
            // Report kernel observations as
            // measurements, never as the planar implementation's interval proof.
            // A failed budget still retains the finite observation; absence of
            // data must remain distinguishable from a measured excess.
            if (std::isfinite(max) && max >= 0)
            {
                m_report.tolerances.push_back({ "UvProjection", "MeasuredCurveSurfaceGap", Id(f.record), max });
            }
            if (auto businessCheck = Need(std::isfinite(max) && max >= 0 && max <= Q(m_options.quality.derivedUvMm), "GeneralDerivedUvGap", f.record);
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            if (auto businessCheck = Need(
                    std::isfinite(maxVertex) && maxVertex >= 0 && maxVertex <= Q(m_options.quality.consistencyMm),
                    "GeneralDerivedVertexGap",
                    f.record
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            uvGap = std::max(uvGap, max);
            // CreateUVTrimCurves also fixes UV endpoints/reparameterization after
            // collecting its projection statistic. Measure the final installed
            // data separately; neither that old statistic nor LEVEL2 covers it.
            SmTArray<SmEdgeuse*> uses;
            native.face->GetUpwardEdgeuses(uses);
            double finalGap = 0;
            for (ULONG k = 0; k < uses.GetSize(); ++k)
            {
                const auto edge = uses[k]->GetEdge();
                const auto uv = uses[k]->GetUVTrimCurve();
                if (auto businessCheck = Need(edge && edge->GetCurve() && uv, "GeneralFinalTrimMissing", f.record); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                const auto interval = edge->GetInterval();
                if (auto operation = Step(f.record); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                auto measured = MeasureFinalTrim(*edge->GetCurve(), *uv, *native.face->GetSurface(), interval.GetMin(), interval.GetMax());
                if (!measured)
                {
                    return tl::unexpected(NativeConstructionFailure(TrimMeasurementErrorName(measured.error())));
                }
                if (measured->maximumMm > Q(m_options.quality.derivedUvMm))
                {
                    // The installed face trim is derived UV. For source-master
                    // pcurves, the defining S(C(t)) and its original parameters
                    // remain unchanged while this target trim is regenerated.
                    // Keep the usual path/cost unchanged for an adequate trim.
                    auto rebuiltResult = RebuildDerivedUv(
                        *uses[k],
                        Q(m_options.quality.derivedUvMm),
                        [&]
                        {
                            return Step(f.record);
                        }
                    );
                    if (!rebuiltResult)
                    {
                        return tl::unexpected(std::move(rebuiltResult.error()));
                    }
                    auto rebuilt = std::move(*rebuiltResult);
                    if (rebuilt.projectionGapMm)
                    {
                        m_report.tolerances.push_back(
                            { "DerivedUvEndpointMatching", "FreshProjectionSampleMaximum", Id(f.record), *rebuilt.projectionGapMm }
                        );
                    }
                    if (rebuilt.rawPair)
                    {
                        m_report.tolerances.push_back(
                            { "DerivedUvEndpointMatching", "RawPairedParameterSampleMaximum", Id(f.record), rebuilt.rawPair->maximumMm }
                        );
                    }
                    if (rebuilt.matchedPair)
                    {
                        m_report.tolerances.push_back(
                            { "DerivedUvEndpointMatching", "EndpointMatchedSampleMaximum", Id(f.record), rebuilt.matchedPair->maximumMm }
                        );
                    }
                    if (rebuilt.status == DerivedUvStatus::Ready)
                    {
                        const auto& targets = TargetEdges();
                        const auto found = std::find(targets.begin(), targets.end(), edge);
                        if (auto businessCheck = Need(found != targets.end(), "GeneralRebuiltUvEdgeIdentity", f.record); !businessCheck)
                        {
                            return tl::unexpected(std::move(businessCheck.error()));
                        }
                        const auto target = static_cast<Index>(found - targets.begin());
                        if (auto operation = Step(f.record); !operation)
                        {
                            return tl::unexpected(std::move(operation.error()));
                        }
                        // Zero forbids tolerance growth; TRUE deletes the old
                        // pair-owned trim. Release only after native adoption,
                        // including on a later ownership-contract failure.
                        const auto candidate = rebuilt.curve.get();
                        uses[k]->SetUVTrimCurve(candidate, 0., TRUE);
                        const bool adopted = rebuilt.curve->GetOwner() == uses[k] || rebuilt.curve->GetOwner() == uses[k]->GetMate();
                        if (adopted)
                        {
                            rebuilt.curve.release();
                        }
                        const auto installed = uses[k]->GetUVTrimCurve();
                        if (auto businessCheck = Need(adopted && installed == candidate, "GeneralRebuiltUvOwnership", f.record); !businessCheck)
                        {
                            return tl::unexpected(std::move(businessCheck.error()));
                        }
                        if (auto operation = Step(f.record); !operation)
                        {
                            return tl::unexpected(std::move(operation.error()));
                        }
                        measured = MeasureFinalTrim(*edge->GetCurve(), *installed, *native.face->GetSurface(), interval.GetMin(), interval.GetMax());
                        if (!measured)
                        {
                            return tl::unexpected(NativeConstructionFailure(TrimMeasurementErrorName(measured.error())));
                        }
                        RepresentationNormalization normalization{ "DerivedUvEndpointMatching", Id(f.record), {}, {}, target };
                        for (const auto& loop : f.loops)
                        {
                            normalization.sourceBoundIds.push_back(Id(loop.bound));
                        }
                        m_report.normalizations.push_back(std::move(normalization));
                        m_rebuiltUv = true;
                    }
                }
                if (auto businessCheck = Need(measured->maximumMm <= Q(m_options.quality.derivedUvMm), "GeneralFinalTrimGap", f.record);
                    !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                finalGap = std::max(finalGap, measured->maximumMm);
            }
            uvGap = std::max(uvGap, finalGap);
            m_report.tolerances.push_back({ "FinalInstalledUv", "KnotSpanPairedParameterSampleMaximum", Id(f.record), finalGap });
            bool explicitOuter = false;
            for (const auto& l : f.loops)
            {
                explicitOuter = explicitOuter || l.outer;
            }
            bool poleRegion = false;
            if (explicitOuter && Is(f.surface, "SURFACE_OF_REVOLUTION"))
            {
                if (auto operation = Step(f.record); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                ULONG approximate = 0;
                poleRegion = native.face->GetSurface()->GetSingularities(nullptr, nullptr, &approximate) != SM_SS_NONE || approximate != SM_SS_NONE;
            }
            // An outer-bound declaration identifies the region; it does not
            // establish absence of another edge's interior pole-zone visit.
            if (!explicitOuter || poleRegion)
            {
                if (auto operation = ClassifyRegion(f, native); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
            }
        }
        return {};
    }
    [[nodiscard]] Result<void> RestrictSupports()
    {
        // Quality is an exact positive int64 ratio. Round both integer casts
        // and the division outward toward a smaller budget: converting large
        // numerator and denominator values independently can otherwise round
        // above the declared rational allowance. The proof also checks its
        // floating environment before using this bound.
        const auto definition = m_options.quality.definitionMm;
        const double lowerDefinition = std::nextafter(
            std::nextafter(double(definition.Numerator()), 0.) / std::nextafter(double(definition.Denominator()), INFINITY),
            0.
        );
        for (Index i = 0; i < faces.size(); ++i)
        {
            const auto& source = faces[i];
            const auto outerCount = std::count_if(
                source.loops.begin(),
                source.loops.end(),
                [](const Loop& loop)
                {
                    return loop.outer;
                }
            );
            if (m_pendingToroidalSupports.contains(source.record) && (m_options.normalizePeriodicFaces || m_model->topologyNormalization ||
                                                                      m_options.faceOrientationRepair || m_options.cylindricalBandRepair))
            {
                return tl::unexpected(Fail("ToroidalSupportNormalizationCompositionUnestablished", source.surface, Status::Unsupported));
            }
            auto evidenceResult = RestrictUnusedSupportDomain(
                *m_model->faces[i].face,
                outerCount == 1,
                lowerDefinition,
                m_options.sourceLimits.values,
                m_options.sourceLimits.values,
                [&]
                {
                    return Step(source.surface);
                },
                m_toroidalOriginals.contains(source.record) ? m_toroidalOriginals.at(source.record) : nullptr
            );
            if (!evidenceResult)
            {
                return tl::unexpected(std::move(evidenceResult.error()));
            }
            const auto evidence = std::move(*evidenceResult);
            if (evidence.status == SupportDomainStatus::WorkLimit)
            {
                return tl::unexpected(Fail("SupportDomainProofWorkLimit", source.surface, Status::ResourceLimit));
            }
            if (evidence.status != SupportDomainStatus::Normalized)
            {
                if (m_pendingToroidalSupports.contains(source.record))
                {
                    const auto key = "ToroidalSupport" + evidence.reason;
                    return tl::unexpected(Fail(
                        key.c_str(),
                        source.surface,
                        evidence.status == SupportDomainStatus::Unestablished ? Status::Indeterminate : Status::Unsupported
                    ));
                }
                continue; // Original candidate still faces every native check.
            }
            if (m_pendingToroidalSupports.contains(source.record))
            {
                if (auto operation = Need(
                        m_toroidalOriginals.contains(source.record) && evidence.numeric.maximumMm.has_value(),
                        "ToroidalSupportEvidenceMissing",
                        source.surface
                    );
                    !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                SurfaceNormalizationInfo info;
                info.sourceFaceId = Id(source.record);
                info.sourceSurfaceId = Id(source.surface);
                info.targetFaceIndex = i;
                info.originalNativeDomain = evidence.original;
                info.retainedNativeDomain = evidence.actual;
                info.originalAnalyticDomainDegrees = evidence.analyticDomainDegrees;
                info.generatorRadiusBoundsMm = evidence.generatorRadiusBoundsMm;
                info.minimumAbsoluteGeneratorRadiusMm = evidence.minimumAbsoluteGeneratorRadiusMm;
                info.continuousDifferenceUpperMm = *evidence.numeric.maximumMm;
                info.tensorPatches = evidence.numeric.patches;
                m_model->surfaceNormalizations.push_back(info);
                m_report.surfaceNormalizations.push_back(std::move(info));
            }
            ParameterMapping mapping;
            const bool toroidal = m_pendingToroidalSupports.contains(source.record);
            mapping.kind = toroidal ? "ToroidalOriginalBasisRestriction" : "UnusedSupportDomainRestriction";
            mapping.sourceGeometryId = Id(source.surface);
            mapping.sourceUseId = Id(source.record);
            mapping.targetFaceIndex = i;
            mapping.equation =
                "native_uv=original_native_uv on retained domain; source definition unchanged in owning snapshot; tensor Bernstein interval bound covers the whole retained rectangle";
            mapping.parameters = { { "original_native_domain", { evidence.original.begin(), evidence.original.end() } },
                                   { "requested_control_hull_domain", { evidence.requested.begin(), evidence.requested.end() } },
                                   { "actual_native_domain", { evidence.actual.begin(), evidence.actual.end() } },
                                   { "proved_native_difference_mm", { *evidence.numeric.maximumMm } },
                                   { "proved_tensor_patches", { double(evidence.numeric.patches) } } };
            m_report.parameterMappings.push_back(std::move(mapping));
            RepresentationNormalization normalization{ toroidal ? "ToroidalOriginalBasisRestriction" : "UnusedSupportDomainRestriction",
                                                       Id(source.record),
                                                       {},
                                                       {},
                                                       {} };
            for (const auto& loop : source.loops)
            {
                normalization.sourceBoundIds.push_back(Id(loop.bound));
            }
            m_report.normalizations.push_back(std::move(normalization));
            m_report.tolerances.push_back(
                { "SupportDomainRestriction", "ContinuousNativeSurfaceDifferenceUpper", Id(source.surface), *evidence.numeric.maximumMm }
            );
            m_rebuiltUv = true; // Recheck final UV after every native consumer.
        }
        return {};
    }
    [[nodiscard]] Result<void> VerifyRebuiltUv()
    {
        // Recheck every installed face trim because native validation can visit
        // neighboring caches. This is still sampled development evidence; loop,
        // seam/pole validity and the original mm quality gate remain required.
        for (Index i = 0; i < m_model->faces.size(); ++i)
        {
            const auto& face = faces[SourceFaceIndex(i)];
            const auto native = m_model->faces[i].face;
            SmTArray<SmEdgeuse*> uses;
            native->GetUpwardEdgeuses(uses);
            double maximum = 0;
            for (ULONG k = 0; k < uses.GetSize(); ++k)
            {
                const auto edge = uses[k]->GetEdge();
                const auto trim = uses[k]->GetUVTrimCurve();
                if (auto businessCheck = Need(edge && edge->GetCurve() && trim, "GeneralFinalTrimMissing", face.record); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                const auto interval = edge->GetInterval();
                if (auto operation = Step(face.record); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                const auto measured = MeasureFinalTrim(*edge->GetCurve(), *trim, *native->GetSurface(), interval.GetMin(), interval.GetMax());
                if (!measured)
                {
                    return tl::unexpected(NativeConstructionFailure(TrimMeasurementErrorName(measured.error())));
                }
                if (auto businessCheck = Need(measured->maximumMm <= Q(m_options.quality.derivedUvMm), "GeneralFinalTrimGap", face.record);
                    !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                maximum = std::max(maximum, measured->maximumMm);
            }
            m_report.tolerances.push_back({ "FinalCandidateUv", "KnotSpanPairedParameterSampleMaximum", Id(face.record), maximum });
            uvGap = std::max(uvGap, maximum);
        }
        for (auto& observation : m_report.tolerances)
        {
            if (observation.stage == "Postcheck" && observation.kind == "MeasuredDerivedUvGap")
            {
                observation.valueMm = uvGap;
            }
        }
        return {};
    }
    [[nodiscard]] Result<void> ObserveRevolutionPoleVisits(const Face& f, NativeFaceData& native, std::size_t expectedMask)
    {
        // These transformations may change face indices or the native pole
        // center/domain. Their composition needs its own final correspondence
        // proof; an old certificate is never carried across them implicitly.
        if (m_options.normalizePeriodicFaces || m_options.restrictUnusedSupportDomains || m_model->topologyNormalization)
        {
            return tl::unexpected(Fail("GeneralRevolutionPoleNormalizationCompositionUnestablished", f.record, Status::Unsupported));
        }
        // Stage 2 may skip its crossing search when ANY vertex already
        // marks a pole. Prove visits on EVERY actual defining edge instead;
        // a marker on one edge cannot authorize another edge's interior.
        terminal_spline::Limits limits;
        limits.controlSlots = m_options.sourceLimits.values;
        std::size_t work = 0;
        auto proof = CheckRevolutionPoleVisits(
            *native.face,
            m_model->vertices,
            limits,
            [&](std::size_t units) -> Result<void>
            {
                auto spent = m_execution.SpendExactGeometryWork(m_report, units, f.record);
                if (spent)
                {
                    work += units;
                }
                return spent;
            },
            [&]
            {
                return Step(f.record);
            }
        );
        if (!proof)
        {
            auto failure = std::move(proof.error());
            if (!failure.record)
            {
                failure.record = f.record;
            }
            return tl::unexpected(std::move(failure));
        }
        if (auto same = Need(proof->mask == expectedMask, "GeneralRevolutionPoleClassificationChanged", f.record); !same)
        {
            return tl::unexpected(std::move(same.error()));
        }
        RevolutionPoleVisitInfo info;
        info.sourceFaceId = Id(f.record);
        info.sourceSurfaceId = Id(f.surface);
        info.targetFaceIndex = static_cast<std::size_t>(&native - m_model->faces.data());
        info.nativePoleMask = proof->mask;
        info.checkedEdges = proof->checkedEdges;
        info.patches = proof->patches;
        info.exactWorkUnits = work;
        info.nativePoleRadiusMm = proof->radiusMm;
        info.composedEdges = proof->composedEdges;
        info.composedRegions = proof->composedRegions;
        m_model->revolutionPoleVisits.push_back(info);
        m_report.revolutionPoleVisits.push_back(std::move(info));
        return {};
    }
    [[nodiscard]] Result<void> RecordFinalRevolutionPoleVisits()
    {
        // Queries and validators can materialize derived data. Bind owning
        // evidence only after ALL consumers and representation changes, using
        // final face indices and final actual curve intervals/zone/pole data.
        for (Index i = 0; i < m_model->faces.size(); ++i)
        {
            const auto& source = faces[SourceFaceIndex(i)];
            if (!Is(source.surface, "SURFACE_OF_REVOLUTION"))
            {
                continue;
            }
            if (auto operation = Step(source.record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            ULONG approximate = 0;
            auto& native = m_model->faces[i];
            const auto poles = native.face->GetSurface()->GetSingularities(nullptr, nullptr, &approximate);
            if (poles == SM_SS_NONE && approximate == SM_SS_NONE)
            {
                continue;
            }
            if (auto observed = ObserveRevolutionPoleVisits(source, native, poles); !observed)
            {
                return tl::unexpected(std::move(observed.error()));
            }
        }
        return {};
    }
    [[nodiscard]] Result<void> ClassifyRegion(const Face& f, NativeFaceData& native)
    {
        // Source-unmarked regions use curve-intersection containment AFTER
        // installed-UV validation. Bounds SET order and the initial envelope
        // only bootstrap construction; neither establishes the outer region.
        const bool cylinder = Is(f.surface, "CYLINDRICAL_SURFACE");
        const bool cone = Is(f.surface, "CONICAL_SURFACE");
        const bool spline = Is(f.surface, "B_SPLINE_SURFACE_WITH_KNOTS");
        const bool revolution = Is(f.surface, "SURFACE_OF_REVOLUTION");
        const bool periodicSupport = cylinder || cone || revolution;
        // This development increment uses an ordinary finite spline chart.
        // F is an admission restriction, not a proof of the source assertion.
        // Unknown/closed declarations still need their own seam/pole contract.
        if ((!Is(f.surface, "PLANE") && !periodicSupport && !spline) ||
            (spline && (F(f.surface, "u_closed").text != "F" || F(f.surface, "v_closed").text != "F" || F(f.surface, "self_intersect").text != "F")))
        {
            return tl::unexpected(Fail("GeneralRegionClassificationPending", f.record, Status::Deferred));
        }
        SmFaceProps properties(native.face, 0, m_model->context.get());
        if (auto operation = Step(f.record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        // The property collector uses a healer-stage enum as a readiness gate.
        // Tolerances and UV/seam prerequisites were established directly above;
        // no healer or geometry mutation is invoked.
        if (auto nativeStatusCheck = Good(properties.SetProps(SM_PROPSTAGE_3, native.face, 0, SM_HO_FIX_SPLITEDGE_ATSEAM)); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        // SetProps_Stage2 only computes seam enums for closed surfaces. For
        // this positively nonclosed chart they remain UNKNOWN/not applicable;
        // do not mistake that untouched state for a passed periodic test.
        if (spline && (properties.m_bClosedU != FALSE || properties.m_bClosedV != FALSE || properties.m_lPoles != SM_SS_NONE ||
                       properties.m_lApproxPoles != SM_SS_NONE || properties.m_bBadCrossedPole != FALSE ||
                       properties.m_bBadUnpairedNoArea_Loop != FALSE || properties.m_lBadMissingPoles != 0))
        {
            return tl::unexpected(Fail("GeneralSplineRegionChartUnestablished", f.record, Status::Deferred));
        }
        // STEP_IMPORT/README.md, revolution pole regions: the retained chart is
        // one angular period in U, with the unswapped finite generator in V.
        // Admit native exact V-end poles only when each already has a face
        // vertex. Stage 3's missing-pole count alone does not establish that
        // inventory for every chart orientation (SmFaceProps::SetProps_Stage3).
        // No additional approximate pole, new vertex, seam/pole crossing or
        // support change is allowed. In particular this does NOT exempt a
        // defining SmCrvOnSurf/SmSTEPSurface from the later complete LEVEL2.
        if (revolution && (properties.m_bClosedU != TRUE || properties.m_bClosedV != FALSE ||
                           (properties.m_lPoles & ~(SM_SS_VMIN | SM_SS_VMAX)) != 0 || properties.m_lApproxPoles != properties.m_lPoles ||
                           (properties.m_lPoles != SM_SS_NONE && properties.m_lNoVertexPoles != SM_SS_NONE) || properties.m_bBadCrossedPole != FALSE))
        {
            return tl::unexpected(Fail("GeneralRevolutionRegionChartUnestablished", f.record, Status::Deferred));
        }
        if (periodicSupport)
        {
            if (!(properties.m_eBadMissingSeam == SM_SP_NEITHER && properties.m_eBadCrossedSeam == SM_SP_NEITHER &&
                  properties.m_eBadNearMissSeam == SM_SP_NEITHER && properties.m_bBadUnpairedNoArea_Loop == FALSE &&
                  properties.m_lBadMissingPoles == 0))
            {
                auto failure = Fail("GeneralRegionSeamUnestablished", f.record);
                // Preserve the exact refusal and billing. Only classify the
                // complete retained-source-vertex observation; never move a
                // vertex or reinterpret a mixed native failure as this case.
                const auto count = std::min(vertices.size(), m_model->vertices.size());
                if (IsPeriodicSourceVertexNearMiss(properties, { m_model->vertices.data(), count }))
                {
                    failure.nativeKind = NativeFailureKind::PeriodicSourceVertexNearMiss;
                    if (m_captureRepairFailure && m_options.conicalApexRepair)
                    {
                        const auto plan = m_vertexLoopPlans.find(f.record);
                        if (plan != m_vertexLoopPlans.end())
                        {
                            SmVertex* vertex = m_model->vertices.at(plan->second.poleVertex);
                            m_apexRepairFailure = IsPeriodicSourceVertexNearMiss(properties, { &vertex, 1 });
                        }
                    }
                }
                return tl::unexpected(std::move(failure));
            }
        }
        if (auto businessCheck = Need(
                properties.m_lOuterLoopCnt == 1 && properties.m_lBadOrient_LoopCnt == 0 && properties.m_lBadNested_LoopCnt == 0 &&
                    properties.m_lBadNoArea_LoopCnt == 0 && properties.m_lBadClosed3d_LoopCnt == 0 && properties.m_lBadClosedPtr_LoopCnt == 0,
                "GeneralRegionContainmentUnestablished",
                f.record
            );
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        SmLoop* outer = nullptr;
        for (ULONG k = 0; k < properties.m_sLoopProps.GetSize(); ++k)
        {
            const auto& loop = properties.m_sLoopProps[k];
            if (auto businessCheck = Need(
                    loop.m_eContainmentType == SM_CMT_OUTERLOOP || loop.m_eContainmentType == SM_CMT_INNERLOOP,
                    "GeneralRegionContainmentUnestablished",
                    f.record
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            if (loop.m_eContainmentType == SM_CMT_OUTERLOOP)
            {
                outer = const_cast<SmLoop*>(loop.m_cpLoop);
            }
        }
        const auto it = std::find(native.loops.begin(), native.loops.end(), outer);
        if (auto businessCheck = Need(it != native.loops.end(), "GeneralClassifiedLoopIdentity", f.record); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        const auto index = static_cast<Index>(it - native.loops.begin());
        const bool declaredOuter = std::any_of(
            f.loops.begin(),
            f.loops.end(),
            [](const auto& loop)
            {
                return loop.outer;
            }
        );
        if (declaredOuter && !f.loops[index].outer)
        {
            return tl::unexpected(Fail("GeneralDeclaredOuterRegionUnestablished", f.record, Status::Indeterminate));
        }
        if (revolution && properties.m_lPoles != SM_SS_NONE)
        {
            if (m_options.normalizePeriodicFaces || m_options.restrictUnusedSupportDomains || m_model->topologyNormalization)
            {
                return tl::unexpected(Fail("GeneralRevolutionPoleNormalizationCompositionUnestablished", f.record, Status::Unsupported));
            }
            // Final defining curves are certified once after all consumers.
            // Region classification and an outer marker grant no exemption. Study
            // transformations never inherit a certificate from an earlier graph.
        }
        if (periodicSupport)
        {
            // A cut annulus is derived from BOTH rings; neither source ring is
            // an outer-bound marker. Preserve all origins and keep the planar
            // one-bound classification report separate from this result.
            RepresentationNormalization classification;
            classification.kind = cylinder                          ? "NativeCylindricalCurveIntersectionContainment" :
                                  cone                              ? "NativeConicalCurveIntersectionContainment" :
                                  properties.m_lPoles != SM_SS_NONE ? "NativeRevolutionPoleCurveIntersectionContainment" :
                                                                      "NativeRevolutionCurveIntersectionContainment";
            classification.sourceFaceId = Id(f.record);
            for (const auto& loop : f.loops)
            {
                classification.sourceBoundIds.push_back(Id(loop.bound));
            }
            m_report.normalizations.push_back(std::move(classification));
        }
        else
        {
            m_report.classifiedOuterBounds.push_back({ Id(f.record), Id(f.loops[index].bound) });
            if (spline)
            {
                // Derived target region only. Preserve all source FACE_BOUND
                // types, UV parameters and uses; no source marker is invented.
                RepresentationNormalization classification;
                classification.kind = "NativeSplineCurveIntersectionContainment";
                classification.sourceFaceId = Id(f.record);
                for (const auto& loop : f.loops)
                {
                    classification.sourceBoundIds.push_back(Id(loop.bound));
                }
                m_report.normalizations.push_back(std::move(classification));
            }
        }
        if (it != native.loops.begin())
        {
            // Kernel constructor uses first loop as outer. Reorder intact loops
            // with complete ownership APIs; all source uses/directions survive.
            std::rotate(native.loops.begin(), it, it + 1);
            for (auto loop : native.loops)
            {
                if (auto operation = Step(f.record); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (auto nativeStatusCheck = Good(native.face->RemoveLoop(loop)); !nativeStatusCheck)
                {
                    return tl::unexpected(std::move(nativeStatusCheck.error()));
                }
            }
            for (auto loop : native.loops)
            {
                if (auto operation = Step(f.record); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (auto nativeStatusCheck = Good(native.face->InsertLoop(loop)); !nativeStatusCheck)
                {
                    return tl::unexpected(std::move(nativeStatusCheck.error()));
                }
            }
        }
        return {};
    }
    [[nodiscard]] ConstructionResult<void> Validate()
    {
        SmAssertArray pointers;
        if (auto operation = Step(shape); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const auto status = m_model->brep->ValidatePointers(&pointers);
        m_report.pointerReports = pointers.GetSize();
        if (status != SM_SUCCESS || pointers.GetSize() != 0)
        {
            return tl::unexpected(AssertFailure(pointers, "GeneralPointerValidation"));
        }
        SmAssertArray deep;
        if (auto operation = Step(shape); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const bool valid = m_model->brep->AssertValid(&deep, SM_LEVEL_2, SM_WALK) != FALSE;
        m_report.deepReports = deep.GetSize();
        if (!valid || deep.GetSize() != 0)
        {
            // A complete small-radius finding set on DERIVED revolution UV may
            // admit one exact target-chart conditioning trial. Defining source
            // curves, mixed failures, topology normalization and repair refuse.
            // Capture the original refusal before replacing trims: its native
            // owner pointers will be stale afterwards, even on later failure.
            auto originalFailure = AssertFailure(deep, "GeneralLevel2Validation");
            const auto candidates = RevolutionChartFaces(deep);
            if (!candidates || m_model->topologyNormalization || m_options.faceOrientationRepair || m_options.cylindricalBandRepair)
            {
                return tl::unexpected(std::move(originalFailure));
            }
            std::map<const SmFace*, std::size_t> originalCounts;
            for (ULONG report = 0; report < deep.GetSize(); ++report)
            {
                const auto* uv = static_cast<const SmBSplineCurve*>(deep[report]->GetOwner());
                ++originalCounts[static_cast<SmEdgeuse*>(uv->GetOwner())->GetFace()];
            }
            for (auto* candidate : *candidates)
            {
                const auto it = std::find_if(
                    m_model->faces.begin(),
                    m_model->faces.end(),
                    [&](const NativeFaceData& f)
                    {
                        return f.face == candidate;
                    }
                );
                if (it == m_model->faces.end())
                {
                    return tl::unexpected(std::move(originalFailure));
                }
                const auto index = static_cast<Index>(it - m_model->faces.begin());
                const auto& source = faces[SourceFaceIndex(index)];
                auto changed = ConditionRevolutionChart(
                    *candidate,
                    m_options.sourceLimits.values,
                    [&]
                    {
                        return Step(source.record);
                    }
                );
                if (!changed)
                {
                    return tl::unexpected(std::move(changed.error()));
                }
                if (!*changed)
                {
                    return tl::unexpected(std::move(originalFailure));
                }
                auto& change = **changed;
                RevolutionChartInfo info;
                info.sourceFaceId = Id(source.record);
                info.sourceSurfaceId = Id(source.surface);
                info.targetFaceIndex = index;
                info.uBinaryExponent = change.exponent;
                info.originalNativeDomain = change.before;
                info.normalizedNativeDomain = change.after;
                info.nativeStepDomain = change.step;
                info.centerConditionBefore = change.conditionBefore;
                info.centerConditionAfter = change.conditionAfter;
                info.exactStoredValues = change.exactValues;
                info.distinctDerivedTrims = change.trims;
                // The original findings may no longer be dereferenced. The
                // complete admitted set was all test 13; retain its count.
                info.originalSmallRadiusReports = originalCounts.at(candidate);
                m_model->retainedSupportDefinitions.push_back(std::move(change.original));
                m_revolutionChartChecks.emplace_back(source.record, std::move(change.verifyStoredIdentity));
                m_model->revolutionCharts.push_back(info);
                m_report.revolutionCharts.push_back(std::move(info));
                m_rebuiltUv = true;
                if (auto classified = ClassifyRegion(source, *it); !classified)
                {
                    return tl::unexpected(std::move(classified.error()));
                }
            }
            // No repeated tuning or selective error suppression. Re-run both
            // validators over the entire private graph after the one trial,
            // then retain all original final UV/role/tolerance/publication gates.
            SmAssertArray finalPointers, finalDeep;
            if (auto operation = Step(shape); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto pointerStatus = m_model->brep->ValidatePointers(&finalPointers);
            m_report.pointerReports = finalPointers.GetSize();
            if (pointerStatus != SM_SUCCESS || finalPointers.GetSize())
            {
                return tl::unexpected(AssertFailure(finalPointers, "GeneralPointerValidation"));
            }
            if (auto operation = Step(shape); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const bool finalValid = m_model->brep->AssertValid(&finalDeep, SM_LEVEL_2, SM_WALK) != FALSE;
            m_report.deepReports = finalDeep.GetSize();
            if (!finalValid || finalDeep.GetSize())
            {
                return tl::unexpected(AssertFailure(finalDeep, "GeneralLevel2Validation"));
            }
        }
        if (auto operation = VerifyPoleInventory(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        for (Index i = 0; i < vertices.size(); ++i)
        {
            if (auto businessCheck = Need(Norm(Sub(P(m_model->vertices[i]->GetPoint()), points[i])) == 0, "GeneralSourceVertexMoved", vertices[i]);
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        if (auto role = VerifyGeneralRole(
                *m_model,
                m_bodyRole,
                m_roleFaces,
                m_roleEdges,
                derivedEdgeFaces,
                m_report,
                [&](Index record)
                {
                    return Step(record);
                }
            );
            !role)
        {
            return tl::unexpected(std::move(role.error()));
        }
        return {};
    }
    [[nodiscard]] ConstructionFailure AssertFailure(const SmAssertArray& reports, const char* key) const
    {
        // Capture a bounded description before discarding native objects. IDs
        // and messages remain useful after the private context is destroyed.
        std::string message;
        for (ULONG i = 0; i < std::min<ULONG>(reports.GetSize(), 128); ++i)
        {
            const auto problem = reports[i];
            if (std::string_view(key) == "GeneralLevel2Validation" && m_report.pointerReports == 0)
            {
                // Only follow the known construction graph after pointer
                // validation. The same 2D curve predicate can concern an exact
                // source-pcurve composition or optional derived face UV; its
                // test index alone cannot distinguish them or authorize repair.
                for (const auto edge : TargetEdges())
                {
                    const auto* curve = edge->GetCurve();
                    const auto* composed = dynamic_cast<const SmCrvOnSurf*>(curve);
                    const char* role = problem->GetOwner() == curve                              ? "DefiningEdgeCurve" :
                                       composed && problem->GetOwner() == composed->GetUVCurve() ? "DefiningPcurve" :
                                                                                                   nullptr;
                    if (role)
                    {
                        message += std::string("geometry_role=") + role + " ";
                        if (const auto source = SourceCurveOf(edge))
                        {
                            message += "edge=" + Id(curves[*source].record) + " geometry=" + Id(curves[*source].geometry) + " ";
                        }
                    }
                }
                for (const auto& face : m_model->faces)
                {
                    for (const auto loop : face.loops)
                    {
                        SmTArray<SmEdgeuse*> uses;
                        loop->GetEdgeuses(uses);
                        for (const auto use : uses)
                        {
                            // Read existing UV only: never materialize a trim
                            // or update a tolerance while explaining a failure.
                            if (problem->GetOwner() == use->GetUVTrimCurvePointer())
                            {
                                message += "geometry_role=DerivedFaceUV face=" + face.faceId + " ";
                                if (const auto source = SourceCurveOf(use->GetEdge()))
                                {
                                    message += "edge=" + Id(curves[*source].record) + " ";
                                }
                            }
                        }
                    }
                }
            }
            for (const auto& face : m_model->faces)
            {
                if (problem->GetOwner() == face.face)
                {
                    message += "face=" + face.faceId + " ";
                }
            }
            for (const auto& face : m_model->faces)
            {
                for (const auto loop : face.loops)
                {
                    if (problem->GetOwner() == loop)
                    {
                        message += "face=" + face.faceId + " ";
                    }
                }
            }
            message += "[ownerType=" + std::to_string(problem->GetOwnerType()) + ",test=" + std::to_string(problem->GetTestIndex()) + "] ";
            TCHAR buffer[1024]{};
            problem->FormatLogMessage(buffer, 1024);
            for (const auto ch : buffer)
            {
                if (!ch)
                {
                    break;
                }
                message += ch >= 0 && ch < 128 ? static_cast<char>(ch) : '?';
            }
            message += "\n";
        }
        Failure failure{ Status::PostcheckFailure, key, message, shape, "GeneralBrep" };
        // ValidatePointers also invokes SmTrimmingTools::CheckFace. Its
        // sTrimmingCheckFace_list/SM_LIST_2 predicate 3 is geometric loop
        // orientation, not a broken pointer. Match reporter, list, owner and
        // predicate through the native identity API, never message parsing or
        // a bare test index. Mixed/unrecorded findings remain unclassified;
        // orientation evidence alone cannot authorize repair or blame source.
        bool orientationOnly = reports.GetSize() > 0 && reports.GetSize() <= 128;
        for (ULONG i = 0; orientationOnly && i < reports.GetSize(); ++i)
        {
            const auto problem = reports[i];
            orientationOnly = problem->GetOwnerType() == SmFace_TYPE && problem->GetListIndex() == SM_LIST_2 &&
                              problem->IsThisReport(problem->GetOwner(), 3, SmTrimmingTools_TYPE, problem->GetOwner(), nullptr);
        }
        if (orientationOnly)
        {
            failure.nativeKind = NativeFailureKind::FaceLoopOrientation;
            if (m_captureRepairFailure)
            {
                OrientationFailureCandidate witness{ failure, m_model, {} };
                for (ULONG i = 0; i < reports.GetSize(); ++i)
                {
                    const auto found = std::find_if(
                        m_model->faces.begin(),
                        m_model->faces.end(),
                        [&](const auto& face)
                        {
                            return face.face == reports[i]->GetOwner();
                        }
                    );
                    if (found == m_model->faces.end())
                    {
                        return failure;
                    }
                    witness.targetFaces.push_back(static_cast<std::size_t>(found - m_model->faces.begin()));
                }
                std::sort(witness.targetFaces.begin(), witness.targetFaces.end());
                witness.targetFaces.erase(std::unique(witness.targetFaces.begin(), witness.targetFaces.end()), witness.targetFaces.end());
                RememberRepairSources();
                return witness;
            }
        }
        return failure;
    }
    [[nodiscard]] Result<void> ObserveTolerances()
    {
        const auto observe = [&](const char* kind, const std::string& id, const SmObject* object) -> Result<void>
        {
            const double tolerance = SmTol::GetZoneTol3d(object);
            // Retain the failing object's finite observation before discarding
            // the candidate. This is millimetres of native working tolerance,
            // not permission to enlarge the configured acceptance/repair budget.
            // Nonfinite values remain a failure without invalid JSON numbers.
            if (std::isfinite(tolerance))
            {
                m_report.tolerances.push_back({ "Postchecked", kind, id, tolerance });
            }
            if (auto businessCheck = Need(
                    std::isfinite(tolerance) && tolerance > 0 && tolerance <= Q(m_options.quality.maximumWorkToleranceMm) &&
                        tolerance <= Tol() + Q(m_options.quality.toleranceGrowthMm),
                    "GeneralUnexpectedToleranceGrowth",
                    shape
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            return {};
        };
        if (auto businessCheck = observe("BrepWorkTolerance", Id(shape), m_model->brep.get()); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        for (const auto& f : m_model->faces)
        {
            if (auto businessCheck = observe("FaceWorkTolerance", f.faceId, f.face); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        for (Index i = 0; i < TargetEdges().size(); ++i)
        {
            if (auto businessCheck = observe(
                    "EdgeWorkTolerance",
                    m_model->topologyNormalization ? (m_model->topologyNormalization->edges[i].sourceEdgeId.value_or("derived-periodic-cut")) :
                                                     (i < curves.size() ? Id(curves[i].record) : "derived-seam"),
                    TargetEdges()[i]
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        for (Index i = 0; i < TargetVertices().size(); ++i)
        {
            if (auto businessCheck = observe(
                    "VertexWorkTolerance",
                    Id(m_model->topologyNormalization ? m_model->targetVertexOwners.at(i) : vertices[i]),
                    TargetVertices()[i]
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        return {};
    }
    [[nodiscard]] Result<void> Measure()
    {
        Statistics info;
        info.faces = m_model->faces.size();
        info.edges = TargetEdges().size();
        info.vertices = TargetVertices().size();
        info.shapeRole = GeneralRoleName(m_bodyRole);
        // This path has already checked the complete all-face boundary:
        // there are no free curves/vertices outside these faces. Each required
        // tight face box includes its vertices, edges AND trimmed interior
        // surface extrema. Their union therefore bounds the whole model without
        // repeating the expensive whole-BRep tight-bound query. Keep the same
        // boxes for FaceView::Info; edge-only or ordinary boxes are insufficient.
        bool firstFace = true;
        for (Index i = 0; i < m_model->faces.size(); ++i)
        {
            auto& f = m_model->faces[i];
            SmPoint3d faceMin, faceMax;
            if (auto operation = Step(faces[SourceFaceIndex(i)].record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(SmApiFaceBoundingBox(f.face, TRUE, faceMin, faceMax)); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            f.info.boundsMm = { P(faceMin), P(faceMax) };
            for (Index a = 0; a < 3; ++a)
            {
                if (auto businessCheck = Need(
                        std::isfinite(f.info.boundsMm[0][a]) && std::isfinite(f.info.boundsMm[1][a]) &&
                            f.info.boundsMm[1][a] >= f.info.boundsMm[0][a],
                        "GeneralFiniteBounds",
                        faces[SourceFaceIndex(i)].record
                    );
                    !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                info.boundsMm[0][a] = firstFace ? f.info.boundsMm[0][a] : std::min(info.boundsMm[0][a], f.info.boundsMm[0][a]);
                info.boundsMm[1][a] = firstFace ? f.info.boundsMm[1][a] : std::max(info.boundsMm[1][a], f.info.boundsMm[1][a]);
            }
            firstFace = false;
        }
        if (auto businessCheck = Need(!firstFace, "GeneralFiniteBounds", shape); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        for (Index a = 0; a < 3; ++a)
        {
            if (auto businessCheck = Need(
                    std::isfinite(info.boundsMm[0][a]) && std::isfinite(info.boundsMm[1][a]) &&
                        (IsMaterial(m_bodyRole) ? info.boundsMm[1][a] > info.boundsMm[0][a] : info.boundsMm[1][a] >= info.boundsMm[0][a]),
                    "GeneralFiniteBounds",
                    shape
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        if (auto operation = Step(shape); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        // Finite-accuracy mass integration also accumulates moment terms.
        // Center their reference frame on the bounds to reduce cancellation;
        // a corner reference failed the rotated analytic-torus control.
        // This changes no geometry or requested accuracy. midpoint avoids
        // overflow when finite bounds have large coordinates of the same sign.
        Point origin;
        for (Index axis = 0; axis < 3; ++axis)
        {
            origin[axis] = std::midpoint(info.boundsMm[0][axis], info.boundsMm[1][axis]);
        }
        GeneralSolidIntegrals integrals;
        const auto accuracy = m_options.numericalIntegration ? m_options.numericalIntegration->relativeAccuracy : 1e-4;
        const auto beforeFace = [&](Index face) -> Result<void>
        {
            if (auto operation = Step(faces[SourceFaceIndex(face)].record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (m_report.numericalIntegration)
            {
                m_report.numericalIntegration->appliedRelativeAccuracy = accuracy;
            }
            return {};
        };
        if (IsMaterial(m_bodyRole))
        {
            auto integrated = IntegrateGeneralSolid(*m_model, origin, beforeFace, integrals, accuracy);
            if (!integrated)
            {
                return tl::unexpected(std::move(integrated.error()));
            }
            if (auto checked = Good(*integrated); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            const double volume = integrals.volumeMm3;
            if (auto checked = Need(
                    std::isfinite(volume) && volume > 0 && std::isfinite(integrals.areaMm2) && integrals.areaMm2 > 0,
                    "GeneralSolidMeasures",
                    shape
                );
                !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            info.areaMm2 = integrals.areaMm2;
            info.materialVolumeMm3 = volume;
            info.centroidMm = integrals.centroidMm;
        }
        else
        {
            GeneralSurfaceIntegrals surface;
            auto integrated = IntegrateGeneralSurface(*m_model, origin, beforeFace, surface, accuracy);
            if (!integrated)
            {
                return tl::unexpected(std::move(integrated.error()));
            }
            if (auto checked = Good(*integrated); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            info.areaMm2 = surface.areaMm2;
            info.centroidMm = surface.centroidMm;
            integrals.faces = std::move(surface.faces);
        }
        // The face API honors this control directly. SmApiBrepComputeMassProperties
        // clamps tighter requests to 1e-4 and must not substitute for this path.
        // Completion records a numerical observation, never a certified volume
        // bound or permission to modify source/geometry/working tolerances.
        m_model->numericalIntegration = NumericalIntegrationObservation{ accuracy, accuracy, true };
        if (m_report.numericalIntegration)
        {
            m_report.numericalIntegration = m_model->numericalIntegration;
        }
        for (const auto value : info.centroidMm)
        {
            if (auto businessCheck = Need(std::isfinite(value), "GeneralFiniteCentroid", shape); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        std::map<SmEdge*, std::pair<double, bool>> edgeMeasures;
        for (Index i = 0; i < TargetEdges().size(); ++i)
        {
            double length = 0;
            if (auto operation = Step(EdgeOwner(i)); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto nativeStatusCheck = Good(SmApiEdgeComputeLength(TargetEdges()[i], m_model->queryTolerance, length)); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (auto businessCheck = Need(std::isfinite(length) && length > 0, "GeneralEdgeLength", EdgeOwner(i)); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            info.boundaryMm += length;
            const bool free = TargetEdges()[i]->IsLamina() != FALSE;
            info.freeBoundaryEdges += free;
            edgeMeasures.emplace(TargetEdges()[i], std::pair{ length, free });
        }
        for (Index i = 0; i < m_model->faces.size(); ++i)
        {
            auto& f = m_model->faces[i];
            f.info.faces = 1;
            f.info.shapeRole = IsMaterial(m_bodyRole) ? "MaterialSolidBoundary" : "FaceSurface";
            // The face's charged/cancellable query ran in the fused traversal.
            // Finish its cached result here, retaining the original report order.
            const auto& integral = integrals.faces[i];
            if (auto nativeStatusCheck = Good(std::abs(integral.areaMm2) >= SM_EFF_ZERO ? SM_SUCCESS : SM_ERR_INVALID_INPUT); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            f.info.areaMm2 = std::abs(integral.areaMm2);
            for (Index axis = 0; axis < 3; ++axis)
            {
                f.info.centroidMm[axis] = integral.areaOriginMm[axis] + integral.areaFirstMomentMm3[axis] / integral.areaMm2;
            }
            std::set<Index> faceEdges, faceVertices;
            if (m_model->topologyNormalization)
            {
                SmTArray<SmEdge*> edges;
                SmTArray<SmVertex*> faceNativeVertices;
                f.face->GetEdges(edges);
                f.face->GetVertices(faceNativeVertices);
                f.info.edges = edges.GetSize();
                f.info.vertices = faceNativeVertices.GetSize();
            }
            else
            {
                for (const auto& loop : faces[i].loops)
                {
                    for (const auto& use : loop.uses)
                    {
                        faceEdges.insert(use.edge);
                        faceVertices.insert(curves[use.edge].v0);
                        faceVertices.insert(curves[use.edge].v1);
                    }
                }
            }
            if (!m_model->topologyNormalization)
            {
                auto missing = MissingSeam(faces[i]);
                if (!missing)
                {
                    return tl::unexpected(std::move(missing.error()));
                }
                f.info.edges = faceEdges.size() + (missing->has_value() ? 1 : 0);
                f.info.vertices = faceVertices.size();
            }
            SmTArray<SmEdge*> nativeEdges;
            f.face->GetEdges(nativeEdges);
            for (const auto edge : nativeEdges)
            {
                const auto measured = edgeMeasures.find(edge);
                if (auto checked = Need(measured != edgeMeasures.end(), "GeneralFaceEdgeInventoryMismatch", shape); !checked)
                {
                    return tl::unexpected(std::move(checked.error()));
                }
                f.info.boundaryMm += measured->second.first;
                f.info.freeBoundaryEdges += measured->second.second;
            }
            const auto& sourceFace = faces[SourceFaceIndex(i)];
            m_report.faces.push_back({ f.faceId, Id(sourceFace.surface), B(sourceFace.record, "same_sense"), f.info });
        }
        m_model->info = info;
        m_report.native = info;
        if (IsMaterial(m_bodyRole))
        {
            m_report.solidRegions = m_report.nativeRole->materialRegions;
            m_report.voidCavities = m_report.nativeRole->boundedVoidRegions;
        }
        // Measurements are intentionally not written into *_bound_mm fields,
        // which are reserved for the existing planar conservative proof.
        m_report.tolerances.push_back({ "Postcheck", "MeasuredEdgeVertexGap", "", consistency });
        m_report.tolerances.push_back({ "Postcheck", "MeasuredDerivedUvGap", "", uvGap });
        return {};
    }
};
} // namespace
[[nodiscard]] Result<std::shared_ptr<ModelData>> BuildGeneralBody(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const step_semantics::Part::Shape& binding,
    const Options& options,
    Report& report,
    Execution& execution
)
{
    std::lock_guard lock(NativeMutex());
    const auto settle = [&](Result<NativeCandidate> candidate)
    {
        // Preserve the legacy leaf counter rule on normal error returns too;
        // originalAttempt remains an overlapping repair witness, not a child sum.
        if (!candidate)
        {
            report.candidateDiscarded = report.nativeCalls > 0;
        }
        return candidate;
    };
    try
    {
        if (options.conicalApexRepair)
        {
            return settle(BuildWithConicalApexRepair(
                source,
                binding,
                options,
                report,
                execution,
                [&](Report& checks, bool capture, const VertexPointOverrides& overrides)
                {
                    return GeneralBuilder(source, options, checks, execution, binding, capture, {}, overrides).Run();
                }
            ));
        }
        if (!options.faceOrientationRepair)
        {
            return settle(FinishConstructionAttempt(GeneralBuilder(std::move(source), options, report, execution, binding).Run()));
        }
        return settle(BuildWithOrientationRepair(
            source,
            binding,
            options,
            report,
            execution,
            [&](Report& checks, bool capture, const FaceSenseOverrides& overrides)
            {
                return GeneralBuilder(source, options, checks, execution, binding, capture, overrides).Run();
            }
        ));
    }
    catch (...)
    {
        report.candidateDiscarded = report.nativeCalls > 0;
        throw;
    }
}
[[nodiscard]] Result<std::shared_ptr<ModelData>> BuildGeneral(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const Options& options,
    Report& report,
    Execution& execution
)
{
    std::lock_guard lock(NativeMutex());
    const auto settle = [&](Result<NativeCandidate> candidate)
    {
        // Preserve the legacy leaf counter rule on normal error returns too;
        // originalAttempt remains an overlapping repair witness, not a child sum.
        if (!candidate)
        {
            report.candidateDiscarded = report.nativeCalls > 0;
        }
        return candidate;
    };
    try
    {
        if (options.conicalApexRepair)
        {
            return settle(BuildWithConicalApexRepair(
                source,
                {},
                options,
                report,
                execution,
                [&](Report& checks, bool capture, const VertexPointOverrides& overrides)
                {
                    return GeneralBuilder(source, options, checks, execution, {}, capture, {}, overrides).Run();
                }
            ));
        }
        if (!options.faceOrientationRepair)
        {
            return settle(FinishConstructionAttempt(GeneralBuilder(std::move(source), options, report, execution).Run()));
        }
        return settle(BuildWithOrientationRepair(
            source,
            {},
            options,
            report,
            execution,
            [&](Report& checks, bool capture, const FaceSenseOverrides& overrides)
            {
                return GeneralBuilder(source, options, checks, execution, {}, capture, overrides).Run();
            }
        ));
    }
    catch (...)
    {
        report.candidateDiscarded = report.nativeCalls > 0;
        throw;
    }
}
} // namespace step_import::detail
