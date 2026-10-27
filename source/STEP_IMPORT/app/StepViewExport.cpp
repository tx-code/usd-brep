// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
// Isolated display worker: no native handle crosses the process boundary.
#include "StepAssemblyArguments.h"
#include "StepComplexMappingArguments.h"
#include "StepConicalApexArguments.h"
#include "StepCylindricalBandArguments.h"
#include "StepExecutionArguments.h"
#include "StepImport.h"
#include "StepIntegrationArguments.h"
#include "StepRepairArguments.h"
#include "StepSelectionArguments.h"
#include "StepSourceIdentityArguments.h"
#include "StepSourceReport.h"
#include "StepSurfaceCurveArguments.h"
#include "StepViewAuxiliary.h"
#include "StepViewMesh.h"
#include "StepViewTopology.h"

#include <SmApiBrep.h>
#include <SmBrep.h>
#include <SmEdge.h>
#include <SmPoly.h>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace
{
using Writer = step_source::report::Writer;
void Progress(const char* phase, std::size_t completed = 0, std::size_t total = 0)
{
    // Counts describe only the named operation. Native call budgets are not a
    // denominator for import work; meshing has no measurable total here.
    std::cout << "@brep-view:" << phase << ',' << completed << ',' << total << '\n' << std::flush;
}
void Good(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
template <class T>
void Network(std::ostream& out, T value)
{
    // Legacy VTK binary scalars are big endian; coordinates retain all 64 bits.
    std::array<char, sizeof(T)> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        bytes[bytes.size() - 1 - i] = static_cast<char>((value >> (8 * i)) & 255);
    }
    out.write(bytes.data(), bytes.size());
}
void Point(std::ostream& out, const SmPoint3d& p)
{
    for (double v : { p.x, p.y, p.z })
    {
        Good(std::isfinite(v), "NonfiniteMeshCoordinate");
        Network(out, std::bit_cast<std::uint64_t>(v));
    }
}
void Mesh(SmBrep& body, const std::filesystem::path& output, std::size_t& triangles, std::size_t& points, step_view::DisplayBudget* budget = nullptr)
{
    SmTessellationParams params;
    params.dChordHeightTol = .01; // Display mm, independent of import acceptance/work tolerances.
    params.dCurveAngleTolDeg = params.dSurfaceAngleTolDeg = 15.;
    SmPolyBrep* raw = nullptr;
    Progress("mesh");
    const auto status = SmApiTessellate(&body, raw, params, FALSE);
    std::unique_ptr<SmPolyBrep> mesh(raw);
    Good(status == SM_SUCCESS && mesh, "TessellationFailed"); // Never request a partial-face fallback.
    SmTArray<SmPolyVertex*> vertices;
    SmTArray<SmPolyFace*> faces;
    mesh->GetPolyVertices(vertices);
    Good(!budget || budget->meshCells <= 2000000, "DisplayMeshLimit");
    Good(!budget || (budget->meshPoints <= 6000000 && vertices.GetSize() <= 6000000 - budget->meshPoints), "DisplayMeshLimit");
    const auto converted = step_view::TriangulateDisplayMesh(*mesh, budget ? 2000000 - budget->meshCells : 2000000);
    Good(bool(converted), converted ? "" : converted.error().data());
    mesh->GetPolyFaces(faces);
    points = vertices.GetSize();
    triangles = faces.GetSize();
    step_view::DisplayTopology topology(body, budget);
    Good(points && triangles && triangles <= 2000000 && points <= std::size_t(INT32_MAX), "DisplayMeshLimit");
    if (budget)
    {
        budget->meshPoints += points;
        budget->meshCells += triangles;
        Good(budget->meshPoints <= 6000000 && budget->meshCells <= 2000000, "DisplayMeshLimit");
    }
    std::unordered_map<SmPolyVertex*, std::uint32_t> indices;
    indices.reserve(points);
    Progress("mesh-points", 0, points);
    std::ofstream out(output / "mesh.vtk", std::ios::binary);
    out << "# vtk DataFile Version 3.0\nBRep Viewer display mesh\nBINARY\nDATASET POLYDATA\nPOINTS " << points << " double\n";
    for (ULONG i = 0; i < vertices.GetSize(); ++i)
    {
        indices.emplace(vertices[i], i);
        Point(out, vertices[i]->GetPoint());
        if ((i + 1) % 4096 == 0 || i + 1 == points)
        {
            Progress("mesh-points", i + 1, points);
        }
    }
    out << "\nPOLYGONS " << triangles << ' ' << triangles * 4 << '\n';
    Progress("mesh-faces", 0, triangles);
    for (ULONG i = 0; i < faces.GetSize(); ++i)
    {
        auto loop = faces[i]->GetOuterPolyLoop();
        Good(loop != nullptr, "MissingMeshLoop");
        SmTArray<SmPolyEdge*> edges;
        loop->GetPolyEdges(edges);
        Good(edges.GetSize() == 3, "UnexpectedNontriangleMesh");
        Network(out, std::uint32_t(3));
        for (ULONG j = 0; j < 3; ++j)
        {
            Network(out, indices.at(edges[j]->GetStartPolyVertex()));
        }
        if ((i + 1) % 4096 == 0 || i + 1 == triangles)
        {
            Progress("mesh-faces", i + 1, triangles);
        }
    }
    out << "\nCELL_DATA " << triangles << "\nSCALARS TopoFace int 1\nLOOKUP_TABLE default\n";
    for (ULONG i = 0; i < faces.GetSize(); ++i)
    {
        Network(out, static_cast<std::uint32_t>(topology.Face(*faces[i])));
    }
    out << '\n';
    out.close();
    Good(bool(out), "MeshWriteFailed");
    SmTArray<SmPoint3d> samples;
    SmTArray<ULONG> counts;
    SmTArray<SmEdge*> edges;
    Progress("boundary");
    Good(SmApiTessellateBoundaries(&body, 5., samples, counts, edges) == SM_SUCCESS, "BoundaryTessellationFailed");
    Good(counts.GetSize() == edges.GetSize(), "DisplayEdgeProvenanceCount");
    std::size_t lines = 0, entries = 0;
    for (ULONG i = 0; i < counts.GetSize(); ++i)
    {
        if (counts[i] > 1)
        {
            ++lines;
            entries += counts[i] + 1;
        }
    }
    Good(samples.GetSize() <= std::size_t(INT32_MAX) && entries <= std::size_t(INT32_MAX), "DisplayBoundaryLimit");
    if (budget)
    {
        budget->boundaryPoints += samples.GetSize();
        budget->boundaryCells += lines;
        Good(budget->boundaryPoints <= 6000000 && budget->boundaryCells <= 2000000, "DisplayBoundaryLimit");
    }
    std::ofstream wire(output / "edges.vtk", std::ios::binary);
    Progress("boundary-points", 0, samples.GetSize());
    wire << "# vtk DataFile Version 3.0\nBRep boundary display samples\nBINARY\nDATASET POLYDATA\nPOINTS " << samples.GetSize() << " double\n";
    for (ULONG i = 0; i < samples.GetSize(); ++i)
    {
        Point(wire, samples[i]);
        if ((i + 1) % 4096 == 0 || i + 1 == samples.GetSize())
        {
            Progress("boundary-points", i + 1, samples.GetSize());
        }
    }
    wire << "\nLINES " << lines << ' ' << entries << '\n';
    Progress("boundary-lines", 0, counts.GetSize());
    std::uint32_t offset = 0;
    for (ULONG i = 0; i < counts.GetSize(); ++i)
    {
        if (counts[i] > 1)
        {
            Network(wire, std::uint32_t(counts[i]));
            for (ULONG j = 0; j < counts[i]; ++j)
            {
                Network(wire, offset + j);
            }
        }
        offset += counts[i];
        if ((i + 1) % 4096 == 0 || i + 1 == counts.GetSize())
        {
            Progress("boundary-lines", i + 1, counts.GetSize());
        }
    }
    wire << "\nCELL_DATA " << lines << "\nSCALARS TopoEdge int 1\nLOOKUP_TABLE default\n";
    for (ULONG i = 0; i < counts.GetSize(); ++i)
    {
        if (counts[i] > 1)
        {
            Network(wire, topology.Edge(edges[i]));
        }
    }
    wire << '\n';
    wire.close();
    Good(bool(wire), "BoundaryWriteFailed");
    Progress("measure");
    topology.Write(output, budget);
}
struct BodyScene
{
    std::string directory, sourceShapeId;
    step_import::Statistics info;
    std::size_t triangles = 0, points = 0;
    std::size_t definition = 0;
    std::string sourceDefinitionId;
    std::size_t instance = 0, definitionBody = 0;
};
std::pair<std::size_t, bool> RepairSummary(const step_import::Report& report)
{
    std::size_t count = report.repairs ? report.repairs->actions.size() : 0;
    bool interpreted = report.repairs && report.repairs->sourceInterpretationApplied;
    count += report.cylindricalBandRepairs ? report.cylindricalBandRepairs->actions.size() : 0;
    interpreted = interpreted || (report.cylindricalBandRepairs && report.cylindricalBandRepairs->sourceInterpretationApplied);
    count += report.conicalApexRepairs ? report.conicalApexRepairs->actions.size() : 0;
    interpreted = interpreted || (report.conicalApexRepairs && report.conicalApexRepairs->sourceInterpretationApplied);
    const auto include = [&](const auto& entries)
    {
        for (const auto& entry : entries)
        {
            if (!entry.checks)
            {
                continue;
            }
            const auto child = RepairSummary(*entry.checks);
            count += child.first;
            interpreted = interpreted || child.second;
        }
    };
    include(report.bodies);
    include(report.definitions);
    return { count, interpreted };
}
void SurfaceCurve3dMarker(Writer& writer, const step_import::Report& report)
{
    if (!report.surfaceCurve3dInterpretation)
    {
        return;
    }
    const auto& receipt = *report.surfaceCurve3dInterpretation;
    // Keep the scene within its independent 64 KiB display bound. The complete
    // native plans, original conflict diagnostics and exact budgets already
    // belong to import-report.json; Python must bind/validate that report before
    // decoding meshes or publishing any scene. No STEP interpretation is rebuilt.
    writer.Add(
        ",\"surface_curve_3d_interpretation\":{\"interpretation\":\"ExplicitCurve3dPriority\","
        "\"report\":\"import-report.json\",\"source_sha256\":"
    );
    writer.String(receipt.sourceSha256);
    writer.Add(",\"quality_id\":");
    writer.String(receipt.qualityId);
    writer.Add(",\"request_complete\":");
    writer.Bool(receipt.requestComplete);
    writer.Add(",\"maximum_uses\":");
    writer.Number(receipt.maximumUses);
    writer.Add(",\"plan_count\":");
    writer.Number(receipt.plans.size());
    writer.Add(",\"action_count\":");
    writer.Number(receipt.actions.size());
    writer.Add("}");
}
void ExecutionReceipt(const std::filesystem::path& output, const step_import_app::ExecutionArguments& request, const step_import::Report& report)
{
    if (!request.Explicit())
    {
        return;
    }
    // Bound the fixed application receipt separately from the full import
    // report. Applied values come from the captured request; the observations
    // below come from the ACTUAL core Report, never from rendered meshes.
    std::string json;
    Good(
        Writer::Publish(
            4096,
            json,
            [&](Writer& writer)
            {
                writer.Add("{\"format\":\"brep-view-execution.v1\",\"scope\":\"ApplicationResourcesOnly\",\"source_sha256\":");
                writer.String(report.sourceSha256);
                writer.Add(",\"max_exact_work_units\":");
                writer.Number(request.Work());
                writer.Add(",\"deadline_ms\":");
                writer.Number(request.DeadlineMs());
                writer.Add(",\"deadline_configured\":");
                writer.Bool(report.deadlineConfigured);
                writer.Add(",\"exact_geometry_work\":");
                if (report.exactGeometryWork)
                {
                    writer.Add("{\"maximum_units\":");
                    writer.Number(report.exactGeometryWork->maximumUnits);
                    writer.Add(",\"used_units\":");
                    writer.Number(report.exactGeometryWork->usedUnits);
                    writer.Add("}");
                }
                else
                {
                    writer.Add("null");
                }
                writer.Add("}");
            }
        ),
        "ExecutionResourceReceiptLimit"
    );
    std::ofstream stream(output / "execution-resources.json", std::ios::binary);
    stream << json;
    stream.close();
    Good(bool(stream), "ExecutionResourceReceiptWriteFailed");
}
void Publish(
    std::string_view profile,
    const std::filesystem::path& output,
    const std::string& status,
    const std::string& key,
    const step_import::Report& report,
    const step_import::Statistics* info,
    std::size_t triangles,
    std::size_t points,
    bool auxiliary = false,
    std::string_view nativeStatus = {},
    std::string_view schema = "DeclaredOnly",
    std::string_view carrier = "DeclaredOnly",
    std::size_t reportMiB = 16,
    bool fileNameText = false,
    bool asciiInvariant = false,
    bool asciiApplied = false,
    const std::vector<BodyScene>* bodyScenes = nullptr,
    std::string_view unitInterpretation = "DeclaredOnly",
    std::size_t definitionCount = 0,
    const step_import::AssemblySnapshot* assembly = nullptr,
    const step_import_app::AssemblyArguments& assemblyRequest = {},
    std::size_t wireframeInstances = 0
)
{
    std::string json;
    const auto emitScene = [&](Writer& w)
    {
        w.Add(
            wireframeInstances      ? "{\"format\":\"brep-view-scene.v7\",\"status\":" :
            assemblyRequest.enabled ? "{\"format\":\"brep-view-scene.v6\",\"status\":" :
            definitionCount         ? "{\"format\":\"brep-view-scene.v5\",\"status\":" :
            bodyScenes              ? "{\"format\":\"brep-view-scene.v4\",\"status\":" :
            info                    ? "{\"format\":\"brep-view-scene.v3\",\"topology\":\"topology.json\",\"status\":" :
                                      "{\"format\":\"brep-view-scene.v1\",\"status\":"
        );
        w.String(status);
        SurfaceCurve3dMarker(w, report);
        if (info && profile == "fine-work-tolerance-study")
        {
            // A ready scene binds this explicit study to the profile
            // actually selected for the core call. Default payloads
            // retain their existing bytes and need no new receipt.
            w.Add(",\"quality_id\":\"Q-AP214-fine-work-tolerance-study-mm-01\"");
        }
        if (assemblyRequest.enabled)
        {
            if (wireframeInstances)
            {
                w.Add(",\"wireframe_required\":true,\"wireframe\":\"wireframes.json\",\"wireframe_instance_count\":");
                w.Number(wireframeInstances);
            }
            w.Add(",\"requested_unreferenced_placement_policy\":");
            w.String(assemblyRequest.placements == step_semantics::UnreferencedPlacementPolicy::RetainDisconnected ? "RetainDisconnected" : "Reject");
            if (assembly)
            {
                w.Add(",\"coordinate_scope\":\"AssemblyRoot\",\"root_count\":1,\"instance_count\":");
                w.Number(assembly->InstanceCount());
                w.Add(",\"definition_count\":");
                w.Number(assembly->DefinitionCount());
                w.Add(",\"retained_source_record_count\":");
                w.Number(assembly->Source()->RetainedPlacements() ? assembly->Source()->RetainedPlacements()->records.size() : 0);
                w.Add(",\"instances\":[");
                for (std::size_t i = 0; i < assembly->InstanceCount(); ++i)
                {
                    if (i)
                    {
                        w.Add(",");
                    }
                    const auto node = assembly->Instance(i);
                    w.Add("{\"index\":");
                    w.Number(i);
                    w.Add(",\"definition\":");
                    w.Number(node.definitionIndex);
                    w.Add(",\"source_definition_id\":");
                    w.String(assembly->SourceDefinitionId(node.definitionIndex));
                    w.Add(",\"body_count\":");
                    w.Number(assembly->BodyCount(node.definitionIndex));
                    w.Add(",\"role\":");
                    w.String(assembly->DefinitionRole(node.definitionIndex));
                    w.Add(",\"parent\":");
                    if (node.parentIndex)
                    {
                        w.Number(*node.parentIndex);
                    }
                    else
                    {
                        w.Add("null");
                    }
                    w.Add(",\"usage\":");
                    if (node.sourceUsageId)
                    {
                        w.String(*node.sourceUsageId);
                    }
                    else
                    {
                        w.Add("null");
                    }
                    w.Add("}");
                }
                w.Add("]");
            }
        }

        w.Add(",\"native_import_status\":");
        w.String(nativeStatus);
        if (report.definitionSelection && (info || nativeStatus == "AcceptedWithSourceIsolation"))
        {
            const auto& selection = *report.definitionSelection;
            w.Add(",\"definition_selection\":{\"configuration\":");
            w.String(selection.configuration);
            w.Add(",\"source_sha256\":");
            w.String(selection.sourceSha256);
            w.Add(",\"source_definition_count\":");
            w.Number(selection.sourceDefinitionIds.size());
            w.Add(",\"selected_definitions\":[");
            for (std::size_t i = 0; i < selection.selectedDefinitionIds.size(); ++i)
            {
                if (i)
                {
                    w.Add(",");
                }
                w.String(selection.selectedDefinitionIds[i]);
            }
            w.Add("],\"native_scope\":\"ExplicitSelectedDefinitions\"");
            if (report.sourceErrorIsolationRequest)
            {
                w.Add(",\"source_scope\":");
                w.String(selection.sourceScope);
                w.Add(",\"isolated_source_failure_count\":");
                w.Number(selection.isolatedSourceFailureCount);
                w.Add(",\"maximum_isolated_failures\":");
                w.Number(report.sourceErrorIsolationRequest->maximumFailures);
                w.Add(",\"maximum_influence_visits\":");
                w.Number(report.sourceErrorIsolationRequest->maximumInfluenceVisits);
                w.Add(",\"influence_visits\":");
                w.Number(report.generalSource->sourceInfluenceVisits);
            }
            w.Add("}");
        }
        if (info && nativeStatus == "AcceptedWithRepairs")
        {
            const auto repair = RepairSummary(report);
            w.Add(",\"repair_applied\":true,\"repaired_face_count\":");
            w.Number(report.conicalApexRepairRequest ? 0 : repair.first);
            w.Add(",\"repair_kind\":");
            w.String(
                report.conicalApexRepairRequest                                                            ? "ConicalApexVertices" :
                report.cylindricalBandRepairRequest && report.cylindricalBandRepairRequest->circleVertices ? "CylindricalBandWithCircleVertices" :
                report.cylindricalBandRepairRequest                                                        ? "CylindricalBandRoles" :
                                                                                                             "FaceOrientation"
            );
            if (report.conicalApexBudget)
            {
                const auto& r = *report.conicalApexRepairRequest;
                const auto& b = *report.conicalApexBudget;
                w.Add(",\"repaired_vertex_count\":");
                w.Number(b.correctedVertices);
                w.Add(",\"conical_apex_repair\":{\"source_sha256\":");
                w.String(r.sourceSha256);
                w.Add(",\"maximum_vertices\":");
                w.Number(r.maximumVertices);
                w.Add(",\"maximum_proof_controls\":");
                w.Number(r.maximumProofControls);
                w.Add(",\"maximum_proof_work\":");
                w.Number(r.maximumProofWork);
                w.Add(",\"maximum_displacement_mm\":");
                w.String(std::to_string(r.maximumDisplacementMm.Numerator()) + "/" + std::to_string(r.maximumDisplacementMm.Denominator()));
                w.Add(",\"maximum_total_displacement_mm\":");
                w.String(std::to_string(r.maximumTotalDisplacementMm.Numerator()) + "/" + std::to_string(r.maximumTotalDisplacementMm.Denominator()));
                w.Add(",\"request_complete\":");
                w.Bool(b.requestComplete);
                w.Add(",\"uses\":[");
                for (std::size_t i = 0; i < r.uses.size(); ++i)
                {
                    if (i)
                    {
                        w.Add(",");
                    }
                    const auto& u = r.uses[i];
                    w.String(
                        u.definitionId + ":" + u.shapeId + ":" + u.faceId + ":" + u.boundId + ":" + u.loopId + ":" + u.vertexId + ":" + u.surfaceId
                    );
                }
                w.Add("]}");
            }
            if (report.circleVertexBudget)
            {
                w.Add(",\"repaired_vertex_count\":");
                w.Number(report.circleVertexBudget->correctedVertices);
                const auto& request = *report.cylindricalBandRepairRequest->circleVertices;
                w.Add(",\"circle_vertex_repair\":{\"source_sha256\":");
                w.String(report.cylindricalBandRepairRequest->sourceSha256);
                w.Add(",\"maximum_vertices\":");
                w.Number(request.maximumVertices);
                const auto fraction = [&](step_import::Fraction value)
                {
                    w.String(std::to_string(value.Numerator()) + "/" + std::to_string(value.Denominator()));
                };
                w.Add(",\"maximum_displacement_mm\":");
                fraction(request.maximumDisplacementMm);
                w.Add(",\"maximum_total_displacement_mm\":");
                fraction(request.maximumTotalDisplacementMm);
                w.Add(",\"request_complete\":");
                w.Bool(report.circleVertexBudget->requestComplete);
                w.Add(",\"vertices\":[");
                for (std::size_t i = 0; i < request.vertices.size(); ++i)
                {
                    if (i)
                    {
                        w.Add(",");
                    }
                    const auto& edit = request.vertices[i];
                    w.String(edit.definitionId + ":" + edit.shapeId + ":" + edit.vertexId + ":" + edit.circleId);
                }
                w.Add("]}");
            }
            w.Add(",\"source_interpretation_applied\":");
            w.Bool(repair.second);
        }
        w.Add(",\"requested_unit_interpretation\":");
        w.String(unitInterpretation);
        const auto sourceView = assembly             ? assembly->Source()->Definition(0) :
                                report.generalSource ? (report.generalSource->snapshot ? report.generalSource->snapshot :
                                                        report.generalSource->document ? report.generalSource->document->Part(0) :
                                                                                         nullptr) :
                                                       nullptr;
        const auto appliedUnit = sourceView ? sourceView->AppliedUnitInterpretation() : step_semantics::UnitInterpretation::DeclaredOnly;
        if (info && nativeStatus == "AcceptedWithSourceIsolation")
        {
            w.Add(",\"source_interpretation_applied\":");
            w.Bool(
                sourceView && (sourceView->AppliedInterpretation() != step_semantics::SchemaInterpretation::DeclaredOnly ||
                               sourceView->Source().AppliedCarrierAssumption() || sourceView->Source().HasAsciiInvariantDataAssumption() ||
                               sourceView->AppliedUnitInterpretation() != step_semantics::UnitInterpretation::DeclaredOnly ||
                               !sourceView->ComplexMappingAssumptions().empty())
            );
        }
        const auto requestedMapping = report.generalSource ? report.generalSource->requestedComplexMappingInterpretation :
                                                             step_semantics::ComplexMappingInterpretation::DeclaredOnly;
        if (requestedMapping != step_semantics::ComplexMappingInterpretation::DeclaredOnly)
        {
            const auto count = sourceView ? sourceView->ComplexMappingAssumptions().size() : 0;
            w.Add(",\"complex_mapping_interpretation\":{\"requested\":");
            w.String(step_semantics::ComplexMappingInterpretationName(requestedMapping));
            w.Add(",\"applied\":");
            w.String(count ? "RegisteredFacetNames" : "DeclaredOnly");
            w.Add(",\"records_interpreted\":");
            w.Number(count);
            w.Add("}");
        }
        w.Add(",\"applied_unit_interpretation\":");
        w.String(step_semantics::UnitInterpretationName(appliedUnit));
        w.Add(",\"import_report_encoding\":");
        w.String(report.encoding == step_import::ReportEncoding::CompactV2 ? "CompactV2" : "ExpandedV1");
        w.Add(",\"requested_schema_interpretation\":");
        w.String(schema);
        if (schema == "SourceBoundAp214As2010")
        {
            w.Add(",\"applied_schema_interpretation\":");
            w.String(
                step_semantics::SchemaInterpretationName(
                    sourceView          ? sourceView->AppliedInterpretation() :
                    report.sourceSchema ? report.sourceSchema->appliedInterpretation.policy :
                                          step_semantics::SchemaInterpretation::DeclaredOnly
                )
            );
        }
        w.Add(",\"requested_carrier_interpretation\":");
        w.String(carrier);
        w.Add(",\"report_budget_mib\":");
        w.Number(reportMiB);
        if (report.numericalIntegration)
        {
            // Copy actual core observations, including NotRun on
            // source failure. Never infer execution from the request.
            const auto& integration = *report.numericalIntegration;
            w.Add(",\"numerical_integration\":{\"requested_relative_accuracy\":");
            step_view::Scalar(w, integration.requestedRelativeAccuracy);
            w.Add(",\"applied_relative_accuracy\":");
            if (integration.appliedRelativeAccuracy)
            {
                step_view::Scalar(w, *integration.appliedRelativeAccuracy);
            }
            else
            {
                w.Add("null");
            }
            w.Add(",\"completed\":");
            w.Bool(integration.completed);
            w.Add(",\"continuous_error_bound_established\":false,\"control_scope\":\"DimensionlessNumericalStoppingOnly\"}");
        }
        w.Add(",\"preserve_opaque_file_name_text\":");
        w.Bool(fileNameText);
        w.Add(",\"assume_ascii_invariant_data\":");
        w.Bool(asciiInvariant);
        w.Add(",\"ascii_invariant_data_applied\":");
        w.Bool(asciiApplied);
        w.Add(",\"key\":");
        w.String(key);
        w.Add(",\"source_sha256\":");
        w.String(report.sourceSha256);
        w.Add(",\"triangles\":");
        w.Number(triangles);
        w.Add(",\"points\":");
        w.Number(points);
        if (info)
        {
            // v3 always declares whether the associated payload is
            // required. Losing its path cannot become a body-only scene.
            w.Add(",\"auxiliary_required\":");
            w.Bool(auxiliary);
            if (auxiliary)
            {
                w.Add(",\"auxiliary\":\"auxiliary.json\"");
            }
            w.Add(",\"role\":");
            w.String(info->shapeRole);
            w.Add(",\"faces\":");
            w.Number(info->faces);
            w.Add(",\"edges\":");
            w.Number(info->edges);
            w.Add(",\"vertices\":");
            w.Number(info->vertices);
            w.Add(",\"units\":\"mm\"");
            if (!bodyScenes)
            {
                w.Add(",\"mesh\":\"mesh.vtk\",\"boundary\":\"edges.vtk\"");
            }
            else
            {
                if (definitionCount && !assemblyRequest.enabled)
                {
                    // These are inventory totals, not assembled-space
                    // metrics. Source definitions have separate frames.
                    w.Add(",\"coordinate_scope\":\"DefinitionLocal\",\"definition_count\":");
                    w.Number(definitionCount);
                }
                w.Add(",\"body_count\":");
                w.Number(bodyScenes->size());
                w.Add(",\"body_scenes\":[");
                for (std::size_t i = 0; i < bodyScenes->size(); ++i)
                {
                    if (i)
                    {
                        w.Add(",");
                    }
                    const auto& body = (*bodyScenes)[i];
                    w.Add("{\"ordinal\":");
                    w.Number(i);
                    w.Add(",\"source_shape_id\":");
                    w.String(body.sourceShapeId);
                    if (definitionCount)
                    {
                        w.Add(",\"definition\":");
                        w.Number(body.definition);
                        w.Add(",\"source_definition_id\":");
                        w.String(body.sourceDefinitionId);
                    }
                    if (assemblyRequest.enabled)
                    {
                        w.Add(",\"instance\":");
                        w.Number(body.instance);
                        w.Add(",\"definition_body\":");
                        w.Number(body.definitionBody);
                    }
                    w.Add(",\"role\":");
                    w.String(body.info.shapeRole);
                    w.Add(",\"format\":\"brep-view-scene.v3\",\"units\":\"mm\",\"auxiliary_required\":false,\"mesh\":");
                    w.String(body.directory + "/mesh.vtk");
                    w.Add(",\"boundary\":");
                    w.String(body.directory + "/edges.vtk");
                    w.Add(",\"topology\":");
                    w.String(body.directory + "/topology.json");
                    w.Add(",\"faces\":");
                    w.Number(body.info.faces);
                    w.Add(",\"edges\":");
                    w.Number(body.info.edges);
                    w.Add(",\"vertices\":");
                    w.Number(body.info.vertices);
                    w.Add(",\"triangles\":");
                    w.Number(body.triangles);
                    w.Add(",\"points\":");
                    w.Number(body.points);
                    w.Add("}");
                }
                w.Add("]");
            }
        }
        w.Add(",\"diagnostics\":[");
        for (std::size_t i = 0; i < report.diagnostics.size() && i < 5; ++i)
        {
            if (i)
            {
                w.Add(",");
            }
            w.Add("{\"key\":");
            w.String(report.diagnostics[i].source.key);
            if (report.diagnostics[i].classification == "NativeFaceLoopOrientationFailure" ||
                report.diagnostics[i].classification == "NativePeriodicSourceVertexNearMiss")
            {
                w.Add(",\"classification\":");
                w.String(report.diagnostics[i].classification);
            }
            w.Add(",\"message\":");
            w.String(report.diagnostics[i].source.message);
            w.Add("}");
        }
        w.Add("]}");
    };
    if (!Writer::Publish(65536, json, emitScene))
    {
        // This is descriptor encoding, not a repeated import or a larger root
        // manifest. Every instance/body row remains in one bounded descriptor
        // and must pass the existing complete-scene checks before adoption.
        Good(assemblyRequest.enabled, "SceneReportLimit");
        Good(Writer::Publish(32 * 1024 * 1024, json, emitScene), "SceneReportLimit");
        const auto metadataBytes = json.size();
        std::ofstream metadata(output / "scene-metadata.json", std::ios::binary);
        metadata << json;
        metadata.close();
        Good(bool(metadata), "SceneMetadataWriteFailed");
        Good(
            Writer::Publish(
                65536,
                json,
                [&](Writer& w)
                {
                    w.Add("{\"format\":\"brep-view-scene.v8\",\"status\":");
                    w.String(status);
                    w.Add(",\"native_import_status\":");
                    w.String(nativeStatus);
                    w.Add(",\"source_sha256\":");
                    w.String(report.sourceSha256);
                    w.Add(",\"metadata\":\"scene-metadata.json\",\"metadata_format\":");
                    w.String(wireframeInstances ? "brep-view-scene.v7" : "brep-view-scene.v6");
                    w.Add(",\"metadata_bytes\":");
                    w.Number(metadataBytes);
                    w.Add("}");
                }
            ),
            "SceneReportLimit"
        );
    }
    std::ofstream file(output / "scene.json.tmp", std::ios::binary);
    file << json;
    file.close();
    Good(bool(file), "SceneReportWriteFailed");
    // Only the complete manifest publishes. Mesh fragments remain private on
    // errors/hard cancellation; the application owns the whole request directory.
    std::filesystem::rename(output / "scene.json.tmp", output / "scene.json");
}
int Run(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    const std::string& profile,
    bool normalize,
    bool restrictDomain,
    bool strict,
    const std::string& schema,
    const std::string& carrier,
    const std::string& reportBudget,
    bool fileNameText,
    bool asciiInvariant,
    const std::string& unitInterpretation,
    const std::optional<step_import::FaceOrientationRepairRequest>& repair = {},
    const std::optional<step_import::DefinitionSelectionRequest>& selection = {},
    step_semantics::ComplexMappingInterpretation complexMappingInterpretation = step_semantics::ComplexMappingInterpretation::DeclaredOnly,
    const std::optional<step_import::CylindricalBandRepairRequest>& bandRepair = {},
    const std::optional<step_import::SourceErrorIsolationLimits>& sourceIsolation = {},
    const step_import_app::AssemblyArguments& assemblyRequest = {},
    const std::optional<step_import::NumericalIntegrationSettings>& numericalIntegration = {},
    const std::string& schemaSourceIdentity = {},
    const std::shared_ptr<const step_import::SurfaceCurve3dInterpretationRequest>& surfaceCurve3d = {},
    const step_import_app::ExecutionArguments& executionRequest = {},
    const std::optional<step_import::ConicalApexRepairRequest>& apexRepair = {}
)
{
    Good(std::filesystem::is_directory(output) && std::filesystem::is_empty(output), "FreshOutputDirectoryRequired");
    Good(
        reportBudget == "16" || reportBudget == "32" || reportBudget == "64" || reportBudget == "128" || reportBudget == "256",
        "InvalidReportBudget"
    );
    const std::size_t reportMiB = reportBudget == "16"  ? 16 :
                                  reportBudget == "32"  ? 32 :
                                  reportBudget == "64"  ? 64 :
                                  reportBudget == "128" ? 128 :
                                                          256;
    Good(!strict || !fileNameText, "FileNameTextRequiresInteroperability");
    Good(!strict || !asciiInvariant, "AsciiInvariantDataRequiresInteroperability");
    Good((schema == "SourceBoundAp214As2010") == !schemaSourceIdentity.empty(), "SourceBoundSchemaRequiresExactOriginalIdentity");
    Good(
        assemblyRequest.enabled || assemblyRequest.placements == step_semantics::UnreferencedPlacementPolicy::Reject,
        "PlacementRetentionRequiresAssembly"
    );
    Good(!assemblyRequest.enabled || (!repair && !selection && !bandRepair && !sourceIsolation), "AssemblyPlanCombinationUnsupported");
    bool asciiApplied = false;
    step_import::FileOutcome result;
    std::string nativeStatus; // Empty until the core actually returns an outcome.
    try
    {
        auto options = step_import::Options::PlanarFixture();
        options.generalBrep = true;
        options.surfaceCurve3dInterpretation = surfaceCurve3d;
        options.numericalIntegration = numericalIntegration;
        options.faceOrientationRepair = repair;
        options.definitionSelection = selection;
        options.sourceErrorIsolation = sourceIsolation;
        options.complexMappingInterpretation = complexMappingInterpretation;
        options.cylindricalBandRepair = bandRepair;
        options.conicalApexRepair = apexRepair;
        options.quality = profile == "development"               ? step_import::Quality::GeneralDevelopment() :
                          profile == "fine-work-tolerance-study" ? step_import::Quality::GeneralFineWorkToleranceStudy() :
                          profile == "tolerance-study"           ? step_import::Quality::GeneralToleranceStudy() :
                          profile == "work-tolerance-study"      ? step_import::Quality::GeneralWorkToleranceStudy() :
                                                                   throw std::invalid_argument("UnknownViewerProfile");
        // Application-selected development preview policy, not a production
        // SDK default. Source assumptions require explicit arguments; no retry.
        options.sourceLimits = { 64 * 1024 * 1024, 1000000, 2000000, 2000000, 128, 1024 * 1024, 16 * 1024 * 1024, 16 * 1024 * 1024, 64 };
        options.semanticLimits.dependencyVisits = 10000000;
        options.nativeLimits.nativeCalls = 1000000;
        // Explicit capacity only: never retry with a larger budget, and never
        // alter source/geometry/working-tolerance or repair policies here.
        options.sourceLimits.reportBytes = reportMiB * 1024 * 1024;
        options.nativeLimits.reportBytes = std::max<std::size_t>(32, reportMiB) * 1024 * 1024;
        options.nativeLimits.processPrivateBytes = 2ULL * 1024 * 1024 * 1024;
        executionRequest.Apply(options);
        const char* lastPhase = "";
        const char* repairPhase = nullptr;
        options.execution.progress = [&](step_import::Progress p)
        {
            const char* phase = nullptr;
            switch (p.phase)
            {
                case step_import::Phase::BeforeCapture:
                    phase = "capture";
                    break;
                case step_import::Phase::Captured:
                    phase = "check";
                    break;
                case step_import::Phase::BeforeNativeCall:
                    phase = repairPhase ? repairPhase : "build";
                    break;
                case step_import::Phase::BeforeRepairProof:
                    repairPhase = phase = "repair-proof";
                    break;
                case step_import::Phase::BeforeDefinitionSelection:
                    phase = "select";
                    break;
                case step_import::Phase::DefinitionSelectionReady:
                    phase = "selection-ready";
                    break;
                case step_import::Phase::BeforeRepair:
                    repairPhase = phase = "repair";
                    break;
                case step_import::Phase::RepairReady:
                    phase = "repair-ready";
                    repairPhase = nullptr;
                    break;
                case step_import::Phase::NativeReady:
                case step_import::Phase::BeforeReport:
                    phase = "report";
                    break;
                default:
                    break;
            }
            if (phase && std::string_view(lastPhase) != phase)
            {
                Progress(phase);
                lastPhase = phase;
            }
        };
        if (!strict)
        {
            options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
            options.headerNamePolicy = fileNameText ? step_source::HeaderNamePolicy::PreserveFileNameText :
                                                      step_source::HeaderNamePolicy::PreserveOpaqueBytes;
            // Transport only an explicit application choice. Never infer this
            // interpretation from raw bytes or retry after an opaque-name refusal.
            options.dataStringPolicy = asciiInvariant ? step_source::DataStringPolicy::PreserveAsciiInvariantBytes :
                                                        step_source::DataStringPolicy::PreserveOpaqueBytes;
        }
        // Match the core's closed policy enums, never infer an interpretation
        // from file content or a failed default import. Strict mode cannot gain
        // a carrier/schema exception through this transport entry point.
        Good(
            !strict || (schema == "DeclaredOnly" && carrier == "DeclaredOnly" && unitInterpretation == "DeclaredOnly"),
            "InterpretationRequiresInteroperability"
        );
        if (unitInterpretation == "VerifiedExplicitUnitDimensions")
        {
            options.unitInterpretation = step_semantics::UnitInterpretation::VerifiedExplicitUnitDimensions;
        }
        else
        {
            Good(unitInterpretation == "DeclaredOnly", "UnknownUnitInterpretation");
        }
        if (schema == "SourceBoundAp214As2010")
        {
            options.schemaInterpretation = { step_semantics::SchemaInterpretation::SourceBoundAp214As2010, schemaSourceIdentity };
        }
        else if (schema == "Ap214NameOnlyAs2010")
        {
            options.schemaInterpretation = step_semantics::SchemaInterpretation::Ap214NameOnlyAs2010;
        }
        else if (schema == "UnregisteredAp214As2010")
        {
            options.schemaInterpretation = step_semantics::SchemaInterpretation::UnregisteredAp214As2010;
        }
        else
        {
            Good(schema == "DeclaredOnly", "UnknownSchemaInterpretation");
        }
        if (carrier == "BlankLevelAsCorrectedEncoding")
        {
            options.carrierInterpretation = step_source::CarrierInterpretation::BlankLevelAsCorrectedEncoding;
        }
        else if (carrier == "Level1AsCorrectedEncoding")
        {
            options.carrierInterpretation = step_source::CarrierInterpretation::Level1AsCorrectedEncoding;
        }
        else if (carrier == "Level1AsCorrectedEncodingWithLowercaseHex")
        {
            options.carrierInterpretation = step_source::CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex;
        }
        else
        {
            Good(carrier == "DeclaredOnly", "UnknownCarrierInterpretation");
        }
        options.normalizePeriodicFaces = normalize;
        options.restrictUnusedSupportDomains = restrictDomain;
        // The application selects compact evidence; model acceptance and report
        // capacity stay identical. Dispatch is based on the one checked source,
        // not a second read or a retry after single-body failure.
        options.reportEncoding = step_import::ReportEncoding::CompactV2;
        if (assemblyRequest.enabled)
        {
            // Application-selected preview resources, separate from geometric
            // quality. The explicit assembly API never retries a failed import.
            auto loaded = step_import::ImportAssemblyFile(input, options, { 100000, 10000, 128 }, assemblyRequest.placements);
            result.status = loaded.status;
            result.report = std::move(loaded.report);
            nativeStatus = step_import::StatusName(result.status);
            if (loaded.assembly && !schemaSourceIdentity.empty())
            {
                Good(result.report.sourceSha256 == schemaSourceIdentity, "SourceBoundSchemaResultIdentityMismatch");
            }
            std::ofstream report(output / "import-report.json", std::ios::binary);
            report << result.report.json;
            report.close();
            Good(bool(report), "ImportReportWriteFailed");
            ExecutionReceipt(output, executionRequest, result.report);
            if (!loaded.assembly)
            {
                Publish(
                    profile,
                    output,
                    nativeStatus,
                    "ImportFailed",
                    result.report,
                    nullptr,
                    0,
                    0,
                    false,
                    nativeStatus,
                    schema,
                    carrier,
                    reportMiB,
                    fileNameText,
                    asciiInvariant,
                    false,
                    nullptr,
                    unitInterpretation,
                    0,
                    nullptr,
                    assemblyRequest
                );
                return 1;
            }
            auto model = loaded.assembly;
            Good(model->Source()->Inputs().Bindings().structure->Roots().size() == 1, "AssemblyDisplayRequiresSingleRoot");
            asciiApplied = model->Source()->Inputs().SourceFields().Source().HasAsciiInvariantDataAssumption();
            step_view::DisplayBudget budget;
            std::vector<BodyScene> scenes;
            step_import::Statistics total;
            total.shapeRole = "Assembly";
            std::size_t bodyInstances = 0;
            for (std::size_t i = 0; i < model->InstanceCount(); ++i)
            {
                const auto count = model->BodyCount(model->Instance(i).definitionIndex);
                Good(count <= 100000 - bodyInstances, "AssemblyDisplayBodyLimit");
                bodyInstances += count;
            }
            for (std::size_t i = 0; i < model->InstanceCount(); ++i)
            {
                const auto node = model->Instance(i);
                for (std::size_t j = 0; j < model->BodyCount(node.definitionIndex); ++j)
                {
                    Progress("bodies", scenes.size(), bodyInstances);
                    auto body = model->Body(node.definitionIndex, j);
                    BodyScene scene;
                    scene.directory = "body-" + std::to_string(scenes.size());
                    scene.info = body->Info();
                    scene.definition = node.definitionIndex;
                    scene.sourceDefinitionId = model->SourceDefinitionId(node.definitionIndex);
                    scene.instance = i;
                    scene.definitionBody = j;
                    const auto part = model->Source()->Definition(node.definitionIndex);
                    scene.sourceShapeId = part->Source().Records().at(part->PartDefinition().shapes.at(j).shape).id;
                    const auto directory = output / scene.directory;
                    Good(std::filesystem::create_directory(directory), "DisplayBodyDirectoryFailed");
                    auto copy = body->CopyBrepForModeling();
                    Good(copy.status == step_import::Status::Accepted && bool(copy.model), "DisplayCopyFailed");
                    copy.model->Edit(
                        [&](SmBrep& native)
                        {
                            const auto& r = node.rotationToRoot;
                            const auto& t = node.translationToRootMm;
                            // SmAxis2Placement stores X/Y basis columns; the public
                            // assembly matrix is row-major. Transform only this
                            // disposable display copy, keeping stored tolerances.
                            const SmAxis2Placement transform(
                                SmPoint3d(t[0], t[1], t[2]),
                                SmVector3d(r[0][0], r[1][0], r[2][0]),
                                SmVector3d(r[0][1], r[1][1], r[2][1])
                            );
                            Good(native.Transform(transform, nullptr, FALSE) == SM_SUCCESS, "AssemblyDisplayTransformFailed");
                            Mesh(native, directory, scene.triangles, scene.points, &budget);
                        }
                    );
                    total.faces += scene.info.faces;
                    total.edges += scene.info.edges;
                    total.vertices += scene.info.vertices;
                    scenes.push_back(std::move(scene));
                }
            }
            step_view::AuxiliaryExport wireExporter(
                [](std::size_t done, std::size_t total)
                {
                    Progress("auxiliary", done, total);
                }
            );
            const auto wireInstances = wireExporter.WriteAssembly(*model, output);
            Good(!scenes.empty() || wireInstances, "AssemblyDisplayGeometryMissing");
            Progress("bodies", bodyInstances, bodyInstances);
            Publish(
                profile,
                output,
                "Ready",
                "",
                result.report,
                &total,
                budget.meshCells,
                budget.meshPoints,
                false,
                nativeStatus,
                schema,
                carrier,
                reportMiB,
                fileNameText,
                asciiInvariant,
                asciiApplied,
                &scenes,
                unitInterpretation,
                model->DefinitionCount(),
                model.get(),
                assemblyRequest,
                wireInstances
            );
            return 0;
        }
        result = step_import::ImportModelFile(input, options);
        const auto singleAlternative = std::get_if<std::shared_ptr<const step_import::ModelSnapshot>>(&result.model);
        const auto collectionAlternative = std::get_if<std::shared_ptr<const step_import::MaterialCollectionSnapshot>>(&result.model);
        const auto documentAlternative = std::get_if<std::shared_ptr<const step_import::DocumentSnapshot>>(&result.model);
        const auto model = singleAlternative ? *singleAlternative : nullptr;
        const auto collection = collectionAlternative ? *collectionAlternative : nullptr;
        const auto document = documentAlternative ? *documentAlternative : nullptr;
        asciiApplied = (model && model->Source()->Source().HasAsciiInvariantDataAssumption()) ||
                       (collection && collection->Source()->Source().HasAsciiInvariantDataAssumption()) ||
                       (document && document->Source()->Source().HasAsciiInvariantDataAssumption());
        nativeStatus = step_import::StatusName(result.status);
        if ((model || collection || document) && !schemaSourceIdentity.empty())
        {
            Good(result.report.sourceSha256 == schemaSourceIdentity, "SourceBoundSchemaResultIdentityMismatch");
        }
        std::ofstream report(output / "import-report.json", std::ios::binary);
        report << result.report.json;
        report.close();
        Good(bool(report), "ImportReportWriteFailed");
        ExecutionReceipt(output, executionRequest, result.report);
        if (!model && !collection && !document)
        {
            Publish(
                profile,
                output,
                step_import::StatusName(result.status),
                "ImportFailed",
                result.report,
                nullptr,
                0,
                0,
                false,
                nativeStatus,
                schema,
                carrier,
                reportMiB,
                fileNameText,
                asciiInvariant,
                asciiApplied,
                nullptr,
                unitInterpretation
            );
            return 1;
        }
        if (collection || document)
        {
            step_view::DisplayBudget budget;
            std::vector<BodyScene> scenes;
            step_import::Statistics total;
            total.shapeRole = document ? "IndependentProductDefinitions" : "MaterialSolidCollection";
            const auto count = document ? document->BodyCount() : collection->BodyCount();
            for (std::size_t i = 0; i < count; ++i)
            {
                Progress("bodies", i, count);
                auto body = document ? document->Body(i) : collection->Body(i);
                BodyScene scene{ "body-" + std::to_string(i),
                                 document ? document->SourceShapeId(i) : collection->SourceShapeId(i),
                                 body->Info(),
                                 0,
                                 0 };
                if (document)
                {
                    scene.definition = document->BodyPartIndex(i);
                    scene.sourceDefinitionId = document->SourceDefinitionId(i);
                }
                const auto directory = output / scene.directory;
                Good(std::filesystem::create_directory(directory), "DisplayBodyDirectoryFailed");
                auto copy = body->CopyBrepForModeling();
                Good(copy.status == step_import::Status::Accepted && bool(copy.model), "DisplayCopyFailed");
                copy.model->Edit(
                    [&](SmBrep& native)
                    {
                        Mesh(native, directory, scene.triangles, scene.points, &budget);
                    }
                );
                total.faces += scene.info.faces;
                total.edges += scene.info.edges;
                total.vertices += scene.info.vertices;
                scenes.push_back(std::move(scene));
            }
            Progress("bodies", count, count);
            Publish(
                profile,
                output,
                "Ready",
                "",
                result.report,
                &total,
                budget.meshCells,
                budget.meshPoints,
                false,
                nativeStatus,
                schema,
                carrier,
                reportMiB,
                fileNameText,
                asciiInvariant,
                asciiApplied,
                &scenes,
                unitInterpretation,
                document ? document->PartCount() : 0
            );
            return 0;
        }
        // CopyBrepForModeling explicitly selects the body component. Complete
        // CopyForModeling still refuses mixed requests; we retain the owning
        // source snapshot and export EVERY associated role before publication.
        step_view::AuxiliaryExport auxiliaryExport(
            [](auto completed, auto total)
            {
                Progress("auxiliary", completed, total);
            }
        );
        const bool auxiliary = auxiliaryExport.Write(*model, output);
        const auto info = model->Info();
        Progress("copy");
        auto copy = model->CopyBrepForModeling();
        Good(copy.status == step_import::Status::Accepted && bool(copy.model), "DisplayCopyFailed");
        std::size_t triangles = 0, points = 0;
        copy.model->Edit(
            [&](SmBrep& body)
            {
                Mesh(body, output, triangles, points);
            }
        );
        Progress("publish");
        Publish(
            profile,
            output,
            "Ready",
            "",
            result.report,
            &info,
            triangles,
            points,
            auxiliary,
            nativeStatus,
            schema,
            carrier,
            reportMiB,
            fileNameText,
            asciiInvariant,
            asciiApplied,
            nullptr,
            unitInterpretation
        );
        return 0;
    }
    catch (const std::exception& error)
    {
        if (!std::filesystem::exists(output / "scene.json"))
        {
            Publish(
                profile,
                output,
                "DisplayFailure",
                error.what(),
                result.report,
                nullptr,
                0,
                0,
                false,
                nativeStatus,
                schema,
                carrier,
                reportMiB,
                fileNameText,
                asciiInvariant,
                asciiApplied,
                nullptr,
                unitInterpretation,
                0,
                nullptr,
                assemblyRequest
            );
        }
        return 1;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    if (argc != 7 && argc != 9 && argc != 10 && argc != 11 && argc != 12 && !(argc >= 13 && (argc - 13) % 2 == 0))
    {
        return 2;
    }
    try
    {
        // The parent assigns the process to its resource/cleanup job before
        // permitting input capture or native allocations. EOF never starts work.
        std::string start;
        Good(bool(std::getline(std::cin, start)) && start == "start", "StartHandshakeRequired");
        for (int i = 4; i < 7; ++i)
        {
            Good(std::wstring_view(argv[i]) == L"0" || std::wstring_view(argv[i]) == L"1", "InvalidFlag");
        }
        if (argc >= 11)
        {
            Good(std::wstring_view(argv[10]) == L"0" || std::wstring_view(argv[10]) == L"1", "InvalidHeaderFlag");
        }
        if (argc >= 12)
        {
            Good(std::wstring_view(argv[11]) == L"0" || std::wstring_view(argv[11]) == L"1", "InvalidDataFlag");
        }
        step_import::Options request;
        step_import_app::AssemblyArguments assemblyRequest;
        step_import_app::SelectionArgumentState selectionArguments;
        step_import_app::SurfaceCurve3dArguments surfaceCurveArguments;
        step_import_app::ExecutionArguments executionArguments;
        for (int i = 13; i < argc; i += 2)
        {
            auto resource = executionArguments.Parse(std::filesystem::path(argv[i]).string(), std::filesystem::path(argv[i + 1]).string());
            Good(bool(resource), "InvalidOrDuplicateExecutionResourceArgument");
            Good(
                *resource || surfaceCurveArguments.Parse(std::filesystem::path(argv[i]).string(), std::filesystem::path(argv[i + 1]).string()) ||
                    step_import_app::SourceIdentityArgument(
                        std::filesystem::path(argv[i]).string(),
                        std::filesystem::path(argv[i + 1]).string(),
                        request
                    ) ||
                    step_import_app::IntegrationArgument(
                        std::filesystem::path(argv[i]).string(),
                        std::filesystem::path(argv[i + 1]).string(),
                        request
                    ) ||
                    step_import_app::AssemblyArgument(
                        std::filesystem::path(argv[i]).string(),
                        std::filesystem::path(argv[i + 1]).string(),
                        assemblyRequest
                    ) ||
                    step_import_app::ConicalApexArgument(
                        std::filesystem::path(argv[i]).string(),
                        std::filesystem::path(argv[i + 1]).string(),
                        request
                    ) ||
                    step_import_app::CylindricalBandArgument(
                        std::filesystem::path(argv[i]).string(),
                        std::filesystem::path(argv[i + 1]).string(),
                        request
                    ) ||
                    step_import_app::RepairArgument(std::filesystem::path(argv[i]).string(), std::filesystem::path(argv[i + 1]).string(), request) ||
                    step_import_app::SelectionArgument(
                        std::filesystem::path(argv[i]).string(),
                        std::filesystem::path(argv[i + 1]).string(),
                        request,
                        selectionArguments
                    ) ||
                    step_import_app::ComplexMappingArgument(
                        std::filesystem::path(argv[i]).string(),
                        std::filesystem::path(argv[i + 1]).string(),
                        request
                    ),
                "UnknownRequestArgument"
            );
        }
        step_import_app::ValidateSelectionArguments(request, selectionArguments);
        return Run(
            argv[1],
            argv[2],
            std::filesystem::path(argv[3]).string(),
            argv[4][0] == L'1',
            argv[5][0] == L'1',
            argv[6][0] == L'1',
            argc >= 9 ? std::filesystem::path(argv[7]).string() : "DeclaredOnly",
            argc >= 9 ? std::filesystem::path(argv[8]).string() : "DeclaredOnly",
            argc >= 10 ? std::filesystem::path(argv[9]).string() : "16",
            argc >= 11 && argv[10][0] == L'1',
            argc >= 12 && argv[11][0] == L'1',
            argc >= 13 ? std::filesystem::path(argv[12]).string() : "DeclaredOnly",
            request.faceOrientationRepair,
            request.definitionSelection,
            request.complexMappingInterpretation,
            request.cylindricalBandRepair,
            request.sourceErrorIsolation,
            assemblyRequest,
            request.numericalIntegration,
            request.schemaInterpretation.sourceSha256,
            surfaceCurveArguments.Request(),
            executionArguments,
            request.conicalApexRepair
        );
    }
    catch (...)
    {
        return 2;
    }
}
#else
int main(int argc, char** argv)
{
    if (argc != 7 && argc != 9 && argc != 10 && argc != 11 && argc != 12 && !(argc >= 13 && (argc - 13) % 2 == 0))
    {
        return 2;
    }
    try
    {
        std::string start;
        Good(bool(std::getline(std::cin, start)) && start == "start", "StartHandshakeRequired");
        for (int i = 4; i < 7; ++i)
        {
            Good(std::string_view(argv[i]) == "0" || std::string_view(argv[i]) == "1", "InvalidFlag");
        }
        if (argc >= 11)
        {
            Good(std::string_view(argv[10]) == "0" || std::string_view(argv[10]) == "1", "InvalidHeaderFlag");
        }
        if (argc >= 12)
        {
            Good(std::string_view(argv[11]) == "0" || std::string_view(argv[11]) == "1", "InvalidDataFlag");
        }
        step_import::Options request;
        step_import_app::AssemblyArguments assemblyRequest;
        step_import_app::SelectionArgumentState selectionArguments;
        step_import_app::SurfaceCurve3dArguments surfaceCurveArguments;
        step_import_app::ExecutionArguments executionArguments;
        for (int i = 13; i < argc; i += 2)
        {
            auto resource = executionArguments.Parse(argv[i], argv[i + 1]);
            Good(bool(resource), "InvalidOrDuplicateExecutionResourceArgument");
            Good(
                *resource || surfaceCurveArguments.Parse(argv[i], argv[i + 1]) ||
                    step_import_app::SourceIdentityArgument(argv[i], argv[i + 1], request) ||
                    step_import_app::IntegrationArgument(argv[i], argv[i + 1], request) ||
                    step_import_app::AssemblyArgument(
                        std::filesystem::path(argv[i]).string(),
                        std::filesystem::path(argv[i + 1]).string(),
                        assemblyRequest
                    ) ||
                    step_import_app::ConicalApexArgument(argv[i], argv[i + 1], request) ||
                    step_import_app::CylindricalBandArgument(argv[i], argv[i + 1], request) ||
                    step_import_app::RepairArgument(argv[i], argv[i + 1], request) ||
                    step_import_app::SelectionArgument(argv[i], argv[i + 1], request, selectionArguments) ||
                    step_import_app::ComplexMappingArgument(argv[i], argv[i + 1], request),
                "UnknownRequestArgument"
            );
        }
        step_import_app::ValidateSelectionArguments(request, selectionArguments);
        return Run(
            argv[1],
            argv[2],
            argv[3],
            argv[4][0] == '1',
            argv[5][0] == '1',
            argv[6][0] == '1',
            argc >= 9 ? argv[7] : "DeclaredOnly",
            argc >= 9 ? argv[8] : "DeclaredOnly",
            argc >= 10 ? argv[9] : "16",
            argc >= 11 && argv[10][0] == '1',
            argc >= 12 && argv[11][0] == '1',
            argc >= 13 ? argv[12] : "DeclaredOnly",
            request.faceOrientationRepair,
            request.definitionSelection,
            request.complexMappingInterpretation,
            request.cylindricalBandRepair,
            request.sourceErrorIsolation,
            assemblyRequest,
            request.numericalIntegration,
            request.schemaInterpretation.sourceSha256,
            surfaceCurveArguments.Request(),
            executionArguments,
            request.conicalApexRepair
        );
    }
    catch (...)
    {
        return 2;
    }
}
#endif
