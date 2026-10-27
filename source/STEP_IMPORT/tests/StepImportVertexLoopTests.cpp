// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportConicalVertexLoop.h"

#include <SmAssertArray.h>
#include <SmBrep.h>
#include <SmEdge.h>
#include <SmFace.h>
#include <SmVertex.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace
{
using namespace step_import;
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
std::string Replace(std::string text, std::string_view from, std::string_view to)
{
    const auto at = text.find(from);
    Require(at != std::string::npos, "public mutation target");
    text.replace(at, from.size(), to);
    return text;
}
Options Settings()
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    return options;
}
void Refused(const std::string& text, std::string_view key, Status status)
{
    const auto result = ImportBytes(text, Settings());
    Require(!result.model && result.status == status, "invalid/unqualified source cannot publish a model");
    if (std::none_of(
            result.report.diagnostics.begin(),
            result.report.diagnostics.end(),
            [&](const auto& d)
            {
                return d.source.key == key;
            }
        ))
    {
        throw std::runtime_error(
            "expected " + std::string(key) + ", got " +
            (result.report.diagnostics.empty() ? "no diagnostic" : result.report.diagnostics.front().source.key)
        );
    }
    Require(result.report.source && result.report.source->Bytes() == text, "failed source remains immutable");
}
void CheckBrep(SmBrep& body)
{
    SmAssertArray pointers, deep;
    Require(body.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "complete native pointer graph");
    Require(body.AssertValid(&deep, SM_LEVEL_2, SM_WALK) && deep.GetSize() == 0, "all native validity checks remain active");
    SmTArray<SmVertex*> vertices;
    SmTArray<SmEdge*> edges;
    body.GetVertices(vertices);
    body.GetEdges(edges);
    Require(vertices.GetSize() == 2 && edges.GetSize() == 2, "one original ring and one derived seam, original two vertices");
    bool apex = false, ring = false;
    for (const auto* vertex : vertices)
    {
        const auto p = vertex->GetPoint();
        apex |= p.x == 0 && p.y == 0 && p.z == 3;
        ring |= p.x == 3 && p.y == 0 && p.z == 0;
    }
    Require(apex && ring, "source vertex coordinates remain bit-identical");
}
void PlannerControls()
{
    using namespace step_import::detail;
    const ConicalVertexLoopInput input{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 0 }, { 0, 0, 1 }, { 3, 0, 0 },
                                        { 0, 0, 3 }, { 3, 0, 0 }, { 3, 0, 0 }, 3,           -1,          3,           true };
    Require(bool(PlanConicalVertexLoop(input, 1e-8, 1e-8)), "independent exact unit-slope cone");
    auto outside = input;
    outside.sourcePoleVertex[0] = 1e-4;
    Require(!PlanConicalVertexLoop(outside, 1e-8, 1e-8), "off-axis source apex is not snapped");
    const auto connected = PlanConicalVertexLoop(outside, 1e-8, 1e-3);
    Require(
        connected && connected->vertexConnectionMm == 1e-4 && connected->definitionResidualMm == 0 && outside.sourcePoleVertex[0] == 1e-4,
        "connection and conversion budgets are distinct; a plan does not move its input or qualify a native region"
    );
    auto reverse = input;
    reverse.forwardInCircle = false;
    Require(!PlanConicalVertexLoop(reverse, 1e-8, 1e-8), "directed region cannot be silently reversed");
    auto end = input;
    end.ringEnd[1] = 1e-4;
    Require(!PlanConicalVertexLoop(end, 1e-8, 1e-8), "source ring endpoint cannot move to a preferred meridian");
}
void SourceControls(const std::string& text)
{
    const auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
    const auto check = [&](step_semantics::Limits limits)
    {
        return step_semantics::Check(raw, limits, step_semantics::SourcePolicy::Strict, step_semantics::GeometryScope::GeneralBrep);
    };
    const auto complete = check({});
    if (complete.status != step_semantics::Status::Deferred || !complete.snapshot || complete.geometryDependencies != "Passed")
    {
        throw std::runtime_error(
            std::string("source control: ") + step_semantics::StatusName(complete.status) + " " +
            (complete.diagnostics.empty() ? "no diagnostic" : complete.diagnostics.front().source.key)
        );
    }
    step_semantics::Limits limits;
    limits.dependencyVisits = complete.dependencyVisits;
    Require(bool(check(limits).snapshot), "source exact visit budget completes");
    --limits.dependencyVisits;
    const auto exhausted = check(limits);
    Require(
        exhausted.status == step_semantics::Status::ResourceLimit && !exhausted.snapshot,
        "source one-fewer-visit failure cannot publish partial bindings"
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public cone and shared-ring fixtures required");
        std::ifstream stream(argv[1], std::ios::binary);
        Require(bool(stream), "fixture exists");
        const std::string text{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        PlannerControls();
        SourceControls(text);
        auto result = ImportBytes(text, Settings());
        Require(result.status == Status::Accepted && result.model, "complete ordinary conical point-boundary import");
        const auto nativeCalls = result.report.nativeCalls;
        const auto model = result.model;
        const auto info = model->Info();
        Require(info.faces == 2 && info.edges == 2 && info.vertices == 2, "analytic topology inventory");
        // Independent equations for the synthetic R=3,H=3 cone. This numerical
        // integration screen is separate from source/UV/definition evidence.
        Require(std::abs(info.areaMm2 - 9 * std::numbers::pi * (1 + std::sqrt(2.))) < 1e-3, "independent cone area");
        Require(model->MaterialVolumeMm3() && std::abs(*model->MaterialVolumeMm3() - 9 * std::numbers::pi) < 1e-3, "independent cone volume");
        Require(result.report.pointerReports == 0 && result.report.deepReports == 0, "unfiltered native checks passed");
        auto mapping = model->TopologyNormalization();
        Require(
            mapping && mapping->completed && mapping->sourceVerticesPreserved && mapping->sourceCurveIntervalsCovered &&
                mapping->vertexLoopCuts.size() == 1,
            "complete owning topology correspondence"
        );
        const auto cut = mapping->vertexLoopCuts[0];
        Require(
            cut.sourceFaceId == "182" && cut.sourceBoundId == "174" && cut.sourceLoopId == "164" && cut.sourceVertexId == "122" &&
                cut.sourceOrientation,
            "point use retains original source identities and orientation"
        );
        Require(
            mapping->vertices.at(cut.targetVertex).sourceVertexId == cut.sourceVertexId &&
                mapping->edges.at(cut.targetSeamEdge).derivedFromFaceId == cut.sourceFaceId,
            "original point and derived edge have distinct origins"
        );
        const auto source = model->Source();
        const auto* loops = source->Instances("VERTEX_LOOP");
        Require(loops && loops->size() == 1 && !source->IsA(loops->front(), "EDGE_LOOP"), "point loop never becomes a source PATH");
        Require(!source->IsA(loops->front(), "PATH"), "zero-dimensional source boundary has no edge-list ancestry");
        const auto vertex = source->Field(loops->front(), "loop_vertex").ReferenceTarget();
        Require(source->Source().Records().at(vertex).id == "122" && source->Source().Bytes() == text, "owning typed source keeps original point");
        mapping->vertexLoopCuts[0].sourceLoopId.clear();
        result.report = {};
        result = {};
        Require(model->TopologyNormalization()->vertexLoopCuts[0].sourceLoopId == "164", "returned report/copy cannot mutate owning correspondence");
        auto editable = model->CopyBrepForModeling();
        Require(editable.status == Status::Accepted && editable.model, "independent modeling copy");
        editable.model->Edit(CheckBrep);

        Require(
            cut.observedPoleConnectionMm && *cut.observedPoleConnectionMm <= .001 && cut.maximumPoleConnectionMm == Settings().quality.consistencyMm,
            "final actual native pole connection keeps its original budget"
        );
        // Passing the incidence precheck grants no native seam exemption. This
        // public gapped point still fails the complete retained seam checks.
        Refused(Replace(text, "(0.,0.,3.)", "(0.00001,0.,3.)"), "GeneralRegionSeamUnestablished", Status::PostcheckFailure);

        const auto reversePoint = ImportBytes(Replace(text, "#174=FACE_BOUND('',#164,.T.)", "#174=FACE_BOUND('',#164,.F.)"), Settings());
        Require(
            reversePoint.status == Status::Accepted && reversePoint.model &&
                !reversePoint.model->TopologyNormalization()->vertexLoopCuts[0].sourceOrientation,
            "zero-dimensional source orientation remains explicit without inventing curve direction"
        );
        auto surface = Replace(text, "ADVANCED_BREP_SHAPE_REPRESENTATION", "MANIFOLD_SURFACE_SHAPE_REPRESENTATION");
        surface = Replace(surface, "MANIFOLD_SOLID_BREP('',#22)", "SHELL_BASED_SURFACE_MODEL('',(#22))");
        const auto closed = ImportBytes(surface, Settings());
        Require(
            closed.status == Status::Accepted && closed.model && closed.model->Info().shapeRole == "ClosedSurfaceModel" &&
                !closed.model->MaterialVolumeMm3() && !closed.model->ClassifyMaterial({ 0, 0, 1 }),
            "same closed conical boundary keeps its surface-model role"
        );
        auto rotated = Replace(text, "#100=CARTESIAN_POINT('',(0.,0.,0.))", "#100=CARTESIAN_POINT('',(10.,20.,30.))");
        rotated = Replace(rotated, "#102=CARTESIAN_POINT('',(3.,0.,0.))", "#102=CARTESIAN_POINT('',(10.,23.,30.))");
        rotated = Replace(rotated, "#103=CARTESIAN_POINT('',(0.,0.,3.))", "#103=CARTESIAN_POINT('',(13.,20.,30.))");
        rotated = Replace(rotated, "#104=DIRECTION('',(0.,0.,1.))", "#104=DIRECTION('',(1.,0.,0.))");
        rotated = Replace(rotated, "#105=DIRECTION('',(1.,0.,0.))", "#105=DIRECTION('',(0.,1.,0.))");
        auto placedOptions = Settings();
        // The analytic oracle is tighter than the legacy 1e-4 quadrature
        // stopping control, especially after translated face contributions
        // cancel. Select a tighter QUERY computation; geometry budgets stay
        // unchanged and no default integration accuracy is redefined here.
        placedOptions.numericalIntegration = NumericalIntegrationSettings{ 1e-7 };
        const auto placed = ImportBytes(rotated, placedOptions);
        Require(
            placed.status == Status::Accepted && placed.model && std::abs(*placed.model->MaterialVolumeMm3() - 9 * std::numbers::pi) < 1e-3,
            "rotated translated source placements retain the analytic cone"
        );
        std::ifstream sharedStream(argv[2], std::ios::binary);
        Require(bool(sharedStream), "public shared-ring fixture exists");
        const std::string sharedText{ std::istreambuf_iterator<char>(sharedStream), std::istreambuf_iterator<char>() };
        auto sharedOptions = Settings();
        sharedOptions.numericalIntegration = NumericalIntegrationSettings{ 1e-7 };
        const auto shared = ImportBytes(sharedText, sharedOptions);
        Require(shared.status == Status::Accepted && shared.model, "point boundary composes with a globally shared circular subdivision");
        const auto sharedInfo = shared.model->Info();
        const auto sharedOrigins = shared.model->TopologyNormalization();
        Require(
            sharedInfo.faces == 3 && sharedInfo.edges == 5 && sharedInfo.vertices == 4 && sharedOrigins && sharedOrigins->completed &&
                sharedOrigins->sourceUseSegments.size() == 4 && sharedOrigins->vertexLoopCuts.size() == 1,
            "one inventory retains both the split circle uses and the point-boundary cut"
        );
        const auto& sharedCut = sharedOrigins->vertexLoopCuts[0];
        Require(
            sharedCut.targetSeamEdge == 4 && sharedOrigins->edges[4].derivedFromFaceId == "182" &&
                sharedOrigins->vertices[sharedCut.targetVertex].sourceVertexId == "122",
            "cut index is rebound after the extra circular child, not left at its old representative index"
        );
        Require(std::abs(*shared.model->MaterialVolumeMm3() - 27 * std::numbers::pi) < 1e-3, "independent stem-plus-cone volume");

        Refused(Replace(text, "(0.,0.,3.)", "(0.01,0.,3.)"), "GeneralConicalVertexLoopConnectionUnestablished", Status::Unsupported);
        Refused(
            Replace(text, "#182=ADVANCED_FACE('',(#172,#174),#142,.T.)", "#182=ADVANCED_FACE('',(#172,#174),#142,.F.)"),
            "GeneralConicalVertexLoopOrientation",
            Status::PostcheckFailure
        );
        Refused(Replace(text, "#174=FACE_BOUND", "#174=FACE_OUTER_BOUND"), "GeneralVertexLoopNormalizationUnestablished", Status::Unsupported);
        auto malformed = ImportBytes(Replace(text, "VERTEX_LOOP('',#122)", "VERTEX_LOOP('',#105)"), Settings());
        Require(
            malformed.status == Status::Rejected && !malformed.model && malformed.report.nativeCalls == 0,
            "wrong nominal vertex target fails before native allocation"
        );
        auto bounded = Settings();
        bounded.nativeLimits.nativeCalls = nativeCalls;
        Require(ImportBytes(text, bounded).status == Status::Accepted, "exact native-call budget completes");
        --bounded.nativeLimits.nativeCalls;
        const auto exhausted = ImportBytes(text, bounded);
        Require(
            exhausted.status == Status::ResourceLimit && !exhausted.model && exhausted.report.candidateDiscarded,
            "one fewer native call releases the entire private candidate"
        );
        bounded = Settings();
        bounded.nativeLimits.reportBytes = 1;
        const auto reportLimited = ImportBytes(text, bounded);
        Require(reportLimited.status == Status::ResourceLimit && !reportLimited.model, "mandatory report refusal prevents publication");
        for (const auto phase : { Phase::NativeReady, Phase::BeforeCommit })
        {
            auto cancelled = Settings();
            cancelled.execution.control = std::make_shared<ImportControl>();
            cancelled.execution.progress = [&, phase](Progress progress)
            {
                if (progress.phase == phase)
                {
                    cancelled.execution.control->RequestCancel();
                }
            };
            const auto stopped = ImportBytes(text, cancelled);
            Require(
                stopped.status == Status::Cancelled && !stopped.model && stopped.report.candidateDiscarded,
                "late cancellation withholds the complete normalized model"
            );
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
