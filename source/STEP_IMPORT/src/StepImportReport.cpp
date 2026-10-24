// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportReport.h"

#include "StepImportAssemblyEvidence.h"
#include "StepImportSurfaceCurveInterpretation.h"
#include "StepSemanticReport.h"
#include "StepSourceReport.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace step_import::detail
{
namespace
{
using step_source::report::Writer;
bool IsAcceptedReportStatus(Status status)
{
    // Scope and source-error provenance differ, but every accepted status has
    // passed the same selected-model publication gate. Nested reports must retain
    // its completeness and native observations, including isolated-source results.
    return status == Status::Accepted || status == Status::AcceptedUnderInterpretation || status == Status::AcceptedWithRepairs ||
           status == Status::AcceptedWithSourceIsolation;
}
void Number(Writer& writer, double value)
{
    if (!std::isfinite(value))
    {
        // Failed native observations must still permit a complete diagnostic report.
        // Null cannot qualify as a measured finite value in an Accepted report.
        writer.Add("null");
        return;
    }
    std::array<char, 64> buffer{};
    const auto end = std::to_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        value,
        std::chars_format::general,
        std::numeric_limits<double>::max_digits10
    );
    if (end.ec != std::errc{})
    {
        throw std::length_error("Native report number capacity");
    }
    writer.Add(std::string_view(buffer.data(), static_cast<std::size_t>(end.ptr - buffer.data())));
}
void SurfaceCurve3dJson(Writer& w, const Report& report)
{
    if (!report.surfaceCurve3dInterpretation)
    {
        return;
    }
    const auto& receipt = *report.surfaceCurve3dInterpretation;
    w.Add(",\"surface_curve_3d_interpretation\":{\"source_sha256\":");
    w.String(receipt.sourceSha256);
    if (!report.surfaceCurve3dReceiptRoot)
    {
        w.Add(",\"receipt_scope\":\"RequestRoot\"}");
        return;
    }
    w.Add(
        ",\"interpretation\":\"ExplicitCurve3dPriority\",\"strict_original_geometry_qualified\":false,\"source_priority_declaration_rewritten\":false,\"source_definitions_rewritten\":false,\"source_pcurve_consistency_qualified\":false,\"request_complete\":"
    );
    w.Bool(receipt.requestComplete);
    w.Add(",\"quality_id\":");
    w.String(receipt.qualityId);
    w.Add(",\"maximum_uses\":");
    w.Number(receipt.maximumUses);
    const auto fraction = [&](std::string_view key, const auto& value)
    {
        w.Add(",");
        w.String(key);
        w.Add(":[");
        w.Number(value[0]);
        w.Add(",");
        w.Number(value[1]);
        w.Add("]");
    };
    fraction("consistency_budget_mm", receipt.consistencyBudgetMm);
    fraction("derived_uv_budget_mm", receipt.derivedUvBudgetMm);
    fraction("maximum_work_tolerance_mm", receipt.maximumWorkToleranceMm);
    w.Add(",\"plans\":[");
    for (std::size_t i = 0; i < receipt.plans.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& plan = receipt.plans[i];
        w.Add("{\"source_definition\":");
        w.String(plan.definitionId);
        w.Add(",\"source_shape\":");
        w.String(plan.shapeId);
        w.Add(",\"source_edge\":");
        w.String(plan.edgeId);
        w.Add(",\"source_surface_curve\":");
        w.String(plan.surfaceCurveId);
        w.Add(",\"source_curve_3d\":");
        w.String(plan.curve3dId);
        w.Add(",\"original_master\":");
        w.String(plan.expectedMaster == SurfaceCurveDeclaredMaster::PcurveS1 ? "PCURVE_S1" : "PCURVE_S2");
        w.Add(",\"source_pcurves\":[");
        for (std::size_t j = 0; j < plan.pcurveIds.size(); ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            w.String(plan.pcurveIds[j]);
        }
        w.Add("]}");
    }
    w.Add("],\"actions\":[");
    for (std::size_t i = 0; i < receipt.actions.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& action = receipt.actions[i];
        w.Add("{\"plan_index\":");
        w.Number(action.planIndex);
        w.Add(",\"native_edge_interval\":[");
        Number(w, action.nativeEdgeInterval[0]);
        w.Add(",");
        Number(w, action.nativeEdgeInterval[1]);
        w.Add("]");
        w.Add(",\"native_checks_passed\":");
        w.Bool(action.nativeChecksPassed);
        w.Add(",\"original_conflicts\":[");
        for (std::size_t j = 0; j < action.originalConflicts.size(); ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            const auto& conflict = action.originalConflicts[j];
            w.Add("{\"source_pcurve\":");
            w.String(conflict.pcurveId);
            w.Add(",\"association_occurrence\":");
            w.Number(conflict.associationOccurrence);
            w.Add(",\"status\":");
            w.String(conflict.status);
            w.Add(",\"key\":");
            w.String(conflict.key);
            w.Add(",\"record_id\":");
            w.String(conflict.recordId);
            w.Add(",\"stage\":");
            w.String(conflict.stage);
            w.Add(",\"message\":");
            w.String(conflict.message);
            w.Add("}");
        }
        w.Add("]}");
    }
    w.Add("]}");
}
void TerminalDomainJson(Writer& w, const Report& report)
{
    if (!report.terminalDomainInterpretation)
    {
        return;
    }
    const auto& receipt = *report.terminalDomainInterpretation;
    w.Add(",\"source_domain_interpretation\":{\"source_sha256\":");
    w.String(receipt.sourceSha256);
    if (!report.terminalDomainReceiptRoot)
    {
        w.Add(",\"receipt_scope\":\"RequestRoot\"}");
        return;
    }
    w.Add(
        ",\"interpretation\":\"ExactTerminalNurbsContinuation\",\"strict_original_geometry_qualified\":false,\"source_definitions_rewritten\":false,\"source_uv_rewritten\":false,\"request_complete\":"
    );
    w.Bool(receipt.requestComplete);
    w.Add(",\"maximum_surfaces\":");
    w.Number(receipt.maximumSurfaces);
    w.Add(",\"maximum_proof_controls\":");
    w.Number(receipt.maximumProofControls);
    w.Add(",\"maximum_proof_work_units\":");
    w.Number(receipt.maximumProofWorkUnits);
    w.Add(",\"proof_work_units\":");
    w.Number(receipt.proofWorkUnits);
    const auto lexemes = [&](const auto& values)
    {
        w.Add("[");
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i)
            {
                w.Add(",");
            }
            w.String(values[i]);
        }
        w.Add("]");
    };
    w.Add(",\"plans\":[");
    for (std::size_t i = 0; i < receipt.plans.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& plan = receipt.plans[i];
        w.Add("{\"source_surface\":");
        w.String(plan.surfaceId);
        w.Add(",\"source_pcurves\":");
        lexemes(plan.pcurveIds);
        w.Add(",\"u_source_parameter_range\":");
        if (plan.u)
        {
            lexemes(*plan.u);
        }
        else
        {
            w.Add("null");
        }
        w.Add(",\"v_source_parameter_range\":");
        if (plan.v)
        {
            lexemes(*plan.v);
        }
        else
        {
            w.Add("null");
        }
        w.Add("}");
    }
    w.Add("],\"actions\":[");
    for (std::size_t i = 0; i < receipt.actions.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& action = receipt.actions[i];
        w.Add("{\"source_surface\":");
        w.String(action.sourceSurfaceId);
        w.Add(",\"source_generator\":");
        w.String(action.sourceGeneratorId);
        w.Add(",\"source_use\":");
        w.String(action.sourceUseId);
        w.Add(",\"source_definition\":");
        w.String(action.sourceDefinitionId);
        w.Add(",\"reference\":");
        w.String(action.reference);
        w.Add(",\"original_domain_diagnostic\":");
        if (action.originalDomainDiagnostic)
        {
            w.String(*action.originalDomainDiagnostic);
        }
        else
        {
            w.Add("null");
        }
        w.Add(",\"original_source_domain\":");
        lexemes(action.originalSourceDomain);
        w.Add(",\"target_native_domain\":[");
        for (std::size_t j = 0; j < 4; ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            Number(w, action.targetNativeDomain[j]);
        }
        w.Add("],\"whole_new_domain_error_upper_mm\":");
        Number(w, action.wholeNewDomainErrorUpperMm);
        w.Add(",\"outside_source_semantics_assumed\":");
        w.Bool(action.outsideSourceSemanticsAssumed);
        w.Add("}");
    }
    w.Add("]}");
}
void SourceUseSegmentsJson(Writer& w, const std::vector<SourceUseSegmentOrigin>& uses)
{
    w.Add("[");
    for (std::size_t i = 0; i < uses.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& use = uses[i];
        const auto& segment = use.target;
        w.Add("{\"source_face\":");
        w.String(use.sourceFaceId);
        w.Add(",\"source_bound\":");
        w.String(use.sourceBoundId);
        w.Add(",\"source_loop\":");
        w.String(use.sourceLoopId);
        w.Add(",\"source_oriented_edge\":");
        w.String(use.sourceOrientedEdgeId);
        w.Add(",\"source_edge\":");
        w.String(use.sourceEdgeId);
        w.Add(",\"forward_in_native_curve\":");
        w.Bool(use.forwardInNativeCurve);
        w.Add(",\"target_edge\":");
        w.Number(segment.edge);
        w.Add(",\"target_start_vertex\":");
        w.Number(segment.startVertex);
        w.Add(",\"target_end_vertex\":");
        w.Number(segment.endVertex);
        w.Add(",\"parameter_convention\":\"OriginalNativeBasis\",\"basis_parameter_interval\":[");
        Number(w, segment.basisParameterInterval[0]);
        w.Add(",");
        Number(w, segment.basisParameterInterval[1]);
        w.Add("]}");
    }
    w.Add("]");
}
void TopologyJson(Writer& w, const TopologyNormalizationInfo& info)
{
    w.Add("{\"completed\":");
    w.Bool(info.completed);
    w.Add(",\"source_curve_intervals_covered\":");
    w.Bool(info.sourceCurveIntervalsCovered);
    w.Add(",\"source_vertices_preserved\":");
    w.Bool(info.sourceVerticesPreserved);
    w.Add(",\"parameter_convention\":\"OriginalNativeBasis\",\"faces\":[");
    for (std::size_t i = 0; i < info.faces.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& f = info.faces[i];
        w.Add("{\"target\":");
        w.Number(f.target);
        w.Add(",\"source_face_id\":");
        w.String(f.sourceFaceId);
        w.Add(",\"source_surface_id\":");
        w.String(f.sourceSurfaceId);
        const auto domain = [&](const std::array<double, 4>& values)
        {
            w.Add("[");
            for (std::size_t j = 0; j < values.size(); ++j)
            {
                if (j)
                {
                    w.Add(",");
                }
                Number(w, values[j]);
            }
            w.Add("]");
        };
        w.Add(",\"native_domain\":");
        domain(f.nativeDomain);
        if (f.originalNativeDomain)
        {
            w.Add(",\"original_native_domain\":");
            domain(*f.originalNativeDomain);
        }
        if (f.nativeSurfaceDifferenceBoundMm)
        {
            w.Add(",\"native_surface_difference_bound_mm\":");
            Number(w, *f.nativeSurfaceDifferenceBoundMm);
        }
        w.Add("}");
    }
    w.Add("],\"edges\":[");
    for (std::size_t i = 0; i < info.edges.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& e = info.edges[i];
        w.Add("{\"target\":");
        w.Number(e.target);
        if (e.sourceEdgeId)
        {
            w.Add(",\"source_edge_id\":");
            w.String(*e.sourceEdgeId);
        }
        if (e.derivedFromFaceId)
        {
            w.Add(",\"derived_from_face_id\":");
            w.String(*e.derivedFromFaceId);
        }
        w.Add(",\"basis_parameter_interval\":[");
        Number(w, e.basisParameterInterval[0]);
        w.Add(",");
        Number(w, e.basisParameterInterval[1]);
        w.Add("]}");
    }
    w.Add("],\"vertices\":[");
    for (std::size_t i = 0; i < info.vertices.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& v = info.vertices[i];
        w.Add("{\"target\":");
        w.Number(v.target);
        if (v.sourceVertexId)
        {
            w.Add(",\"source_vertex_id\":");
            w.String(*v.sourceVertexId);
        }
        if (v.surfacePole)
        {
            w.Add(",\"surface_pole\":{\"source_face_id\":");
            w.String(v.surfacePole->sourceFaceId);
            w.Add(",\"source_surface_id\":");
            w.String(v.surfacePole->sourceSurfaceId);
            w.Add(",\"axis_sign\":");
            Number(w, static_cast<double>(v.surfacePole->axisSign));
            w.Add("}");
        }
        w.Add(",\"basis_edge_parameters\":[");
        for (std::size_t j = 0; j < v.basisEdgeParameters.size(); ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            w.Add("{\"source_edge_id\":");
            w.String(v.basisEdgeParameters[j].first);
            w.Add(",\"parameter\":");
            Number(w, v.basisEdgeParameters[j].second);
            w.Add("}");
        }
        w.Add("]}");
    }
    w.Add("]");
    if (!info.sourceUseSegments.empty())
    {
        w.Add(",\"source_use_segments\":");
        SourceUseSegmentsJson(w, info.sourceUseSegments);
    }
    w.Add("}");
}
void PointJson(Writer& writer, const Point& point)
{
    writer.Add("[");
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (i)
        {
            writer.Add(",");
        }
        Number(writer, point[i]);
    }
    writer.Add("]");
}
void FractionJson(Writer& writer, Fraction value)
{
    writer.Add("{\"kind\":\"rational\",\"num\":");
    writer.String(std::to_string(value.Numerator()));
    writer.Add(",\"den\":");
    writer.String(std::to_string(value.Denominator()));
    writer.Add("}");
}
void FractionJson(Writer& writer, const ExactUnitRatio& value)
{
    writer.Add("{\"kind\":\"rational\",\"num\":");
    writer.String(value.Numerator());
    writer.Add(",\"den\":");
    writer.String(value.Denominator());
    writer.Add("}");
}
void FrameJson(Writer& w, const CoordinateFrame& frame)
{
    w.Add("{\"origin_mm\":");
    PointJson(w, frame.originMm);
    w.Add(",\"x\":");
    PointJson(w, frame.x);
    w.Add(",\"y\":");
    PointJson(w, frame.y);
    w.Add(",\"z\":");
    PointJson(w, frame.z);
    w.Add("}");
}
void DetachedCurveJson(Writer& w, const WireframeCurveInfo& curve)
{
    w.Add("{\"source_id\":");
    w.String(curve.sourceId);
    w.Add(",\"source_basis\":");
    w.String(curve.basisId);
    w.Add(",\"native_curve_index\":");
    w.Number(curve.nativeIndex);
    w.Add(",\"native_parameter_domain\":[");
    Number(w, curve.parameterDomain[0]);
    w.Add(",");
    Number(w, curve.parameterDomain[1]);
    w.Add("],\"source_forward_in_native\":");
    w.Bool(curve.sourceForwardInNative);
    w.Add("}");
}

void PointMappingJson(Writer& w, const WireframePointInfo& point)
{
    w.Add("{\"source_id\":");
    w.String(point.sourceId);
    w.Add(",\"archive_point_brep_index\":");
    w.Number(point.nativeIndex + 1);
    w.Add(",\"point_mm\":");
    PointJson(w, point.pointMm);
    w.Add("}");
}
void SupportingJson(Writer& w, const std::vector<SupportingGeometryInfo>& groups)
{
    w.Add(",\"supporting_geometry\":[");
    for (std::size_t i = 0; i < groups.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& group = groups[i];
        w.Add("{\"source_representation\":");
        w.String(group.representationId);
        w.Add(",\"source_relationship\":");
        w.String(group.relationshipId);
        w.Add(",\"role\":\"ConstructiveSupportingGeometry\",\"material_boundary\":false,\"curves\":[");
        for (std::size_t j = 0; j < group.curves.size(); ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            DetachedCurveJson(w, group.curves[j]);
        }
        w.Add("],\"points\":[");
        for (std::size_t j = 0; j < group.points.size(); ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            PointMappingJson(w, group.points[j]);
        }
        w.Add("],\"frames\":[");
        for (std::size_t j = 0; j < group.frames.size(); ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            const auto& frame = group.frames[j];
            w.Add("{\"source_id\":");
            w.String(frame.sourceId);
            w.Add(",\"archive_frame_surface_adapter_index\":");
            w.Number(frame.nativeSurfaceIndex);
            w.Add(",\"archive_encoding\":\"CanonicalPlaneFrameAdapter\",\"is_source_surface\":false,\"frame\":");
            FrameJson(w, frame.frame);
            w.Add("}");
        }
        w.Add("],\"planes\":[");
        for (std::size_t j = 0; j < group.planes.size(); ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            const auto& plane = group.planes[j];
            w.Add("{\"source_id\":");
            w.String(plane.sourceId);
            w.Add(",\"source_position\":");
            w.String(plane.positionId);
            w.Add(",\"native_surface_index\":");
            w.Number(plane.nativeSurfaceIndex);
            w.Add(",\"unbounded_source_plane\":true,\"frame\":");
            FrameJson(w, plane.frame);
            w.Add(",\"native_chart_sentinels\":[");
            for (std::size_t k = 0; k < 4; ++k)
            {
                if (k)
                {
                    w.Add(",");
                }
                Number(w, plane.parameterDomain[k]);
            }
            w.Add("]}");
        }
        w.Add("]}");
    }
    w.Add("]");
}
void Optional(Writer& writer, const std::optional<double>& value)
{
    if (value)
    {
        Number(writer, *value);
    }
    else
    {
        writer.Add("null");
    }
}
void StatisticsJson(Writer& w, const Statistics& n, bool includeRole = false)
{
    w.Add("{");
    if (includeRole)
    {
        w.Add("\"shape_role\":");
        w.String(n.shapeRole);
        w.Add(",");
    }
    w.Add("\"faces\":");
    w.Number(n.faces);
    w.Add(",\"edges\":");
    w.Number(n.edges);
    w.Add(",\"vertices\":");
    w.Number(n.vertices);
    w.Add(",\"area_mm2\":");
    Number(w, n.areaMm2);
    w.Add(",\"boundary_mm\":");
    Number(w, n.boundaryMm);
    w.Add(",\"bounds_mm\":[");
    PointJson(w, n.boundsMm[0]);
    w.Add(",");
    PointJson(w, n.boundsMm[1]);
    w.Add("]");
    w.Add(",\"centroid_mm\":");
    PointJson(w, n.centroidMm);
    w.Add(",\"normal\":");
    if (n.normal)
    {
        PointJson(w, *n.normal);
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"window_mm\":");
    if (n.windowMm)
    {
        w.Add("[");
        for (std::size_t i = 0; i < 4; ++i)
        {
            if (i)
            {
                w.Add(",");
            }
            Number(w, (*n.windowMm)[i]);
        }
        w.Add("]");
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"material_volume\":");
    if (n.materialVolumeMm3)
    {
        w.Add("{\"applicability\":\"Applicable\",\"value_mm3\":");
        Number(w, *n.materialVolumeMm3);
        w.Add("}");
    }
    else
    {
        w.String("NotApplicable");
    }
    w.Add(",\"free_boundary_edges\":");
    w.Number(n.freeBoundaryEdges);
    w.Add("}");
}
// General reports return before the planar serializer. Keep their role and
// incidence observations explicit here, including nested definition reports.
void GeneralNativeEvidenceJson(Writer& w, const Report& report)
{
    w.Add(",\"source_shape\":");
    if (report.sourceShape)
    {
        const auto& source = *report.sourceShape;
        w.Add("{");
        const std::pair<const char*, const std::string*> fields[]{ { "role", &source.role },
                                                                   { "representation_id", &source.representationId },
                                                                   { "representation_type", &source.representationType },
                                                                   { "shape_id", &source.shapeId },
                                                                   { "shape_type", &source.shapeType },
                                                                   { "shell_id", &source.shellId },
                                                                   { "shell_type", &source.shellType } };
        bool comma = false;
        for (const auto& [name, value] : fields)
        {
            if (comma)
            {
                w.Add(",");
            }
            comma = true;
            w.String(name);
            w.Add(":");
            w.String(*value);
        }
        w.Add("}");
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"native_role\":");
    if (report.nativeRole)
    {
        const auto& role = *report.nativeRole;
        w.Add("{\"regions\":");
        w.Number(role.regions);
        w.Add(",\"shells\":");
        w.Number(role.shells);
        w.Add(",\"material_regions\":");
        w.Number(role.materialRegions);
        w.Add(",\"bounded_void_regions\":");
        w.Number(role.boundedVoidRegions);
        w.Add(",\"manifold_solid_predicate\":");
        w.Bool(role.manifoldSolidPredicate);
        w.Add("}");
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"edge_adjacency\":[");
    for (std::size_t i = 0; i < report.adjacency.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& edge = report.adjacency[i];
        w.Add("{\"target_edge_index\":");
        w.Number(i);
        w.Add(",\"source_edge\":");
        if (edge.edgeId.empty())
        {
            w.Add("null");
        }
        else
        {
            w.String(edge.edgeId);
        }
        w.Add(",\"native_edgeuses\":");
        w.Number(edge.nativeUses);
        w.Add(",\"source_faces\":[");
        for (std::size_t j = 0; j < edge.faceIds.size(); ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            // Repeated IDs retain distinct same-face seam uses.
            w.String(edge.faceIds[j]);
        }
        w.Add("]}");
    }
    w.Add("],\"face_mappings\":[");
    for (std::size_t i = 0; i < report.faces.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& face = report.faces[i];
        w.Add("{\"target_face_index\":");
        w.Number(i);
        w.Add(",\"source_face\":");
        w.String(face.faceId);
        w.Add(",\"source_surface\":");
        w.String(face.planeId);
        w.Add(",\"source_same_sense\":");
        w.Bool(face.sourceSameSense);
        w.Add(",\"measurements\":");
        StatisticsJson(w, face.native, true);
        w.Add("}");
    }
    w.Add("],\"material_census\":{\"applicability\":");
    const bool surface = report.sourceShape && report.sourceShape->role != "MaterialSolid";
    w.String(surface ? "NotApplicable" : report.solidRegions ? "Applicable" : "NotRun");
    w.Add(",\"solid_regions\":");
    if (report.solidRegions)
    {
        w.Number(*report.solidRegions);
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"void_cavities\":");
    if (report.voidCavities)
    {
        w.Number(*report.voidCavities);
    }
    else
    {
        w.Add("null");
    }
    w.Add("}");
}
void DeclarationsJson(Writer& w, const SourceDeclarations& declarations)
{
    const auto& source = *declarations.semantic;
    const auto& raw = source.Source();
    const auto id = [&](std::size_t r) -> const std::string&
    {
        return raw.Records().at(r).id;
    };
    const auto& units = source.UnitContext();
    w.Add("{\"uncertainty_policy\":\"RecordAndWarnOnly\",\"units\":{\"length_unit\":");
    w.String(id(units.lengthUnit));
    w.Add(",\"plane_angle_unit\":");
    w.String(id(units.planeAngleUnit));
    w.Add(",\"solid_angle_unit\":");
    if (units.solidAngleUnit)
    {
        w.String(id(*units.solidAngleUnit));
    }
    else
    {
        w.Add("null");
    }
    w.Add("},\"records\":[");
    bool comma = false;
    for (const auto r : declarations.records)
    {
        if (comma)
        {
            w.Add(",");
        }
        comma = true;
        const auto& record = raw.Records().at(r);
        w.Add("{\"id\":");
        w.String(record.id);
        w.Add(",\"span\":");
        w.Range(record.span);
        w.Add(",\"raw\":");
        w.String(raw.Raw(record.span));
        w.Add(",\"types\":[");
        bool typeComma = false;
        for (const auto& type : source.TypesOf(r))
        {
            if (typeComma)
            {
                w.Add(",");
            }
            typeComma = true;
            w.String(type);
        }
        w.Add("]}");
    }
    w.Add("],\"uncertainties\":[");
    comma = false;
    for (const auto& uncertainty : declarations.uncertainties)
    {
        if (comma)
        {
            w.Add(",");
        }
        comma = true;
        w.Add("{\"id\":");
        w.String(id(uncertainty.record));
        w.Add(",\"unit\":");
        w.String(id(uncertainty.unit));
        w.Add(",\"value_type\":");
        w.String(uncertainty.valueType);
        w.Add(",\"value\":");
        FractionJson(w, uncertainty.value);
        const auto optional = [&](const char* name, const std::optional<Fraction>& value)
        {
            w.Add(",");
            w.String(name);
            w.Add(":");
            if (value)
            {
                FractionJson(w, *value);
            }
            else
            {
                w.Add("null");
            }
        };
        optional("unit_scale_mm", uncertainty.unitScaleMm);
        optional("value_mm", uncertainty.valueMm);
        w.Add(",\"consistency_comparison\":");
        w.String(uncertainty.consistencyComparison);
        w.Add(",\"warning\":");
        if (uncertainty.consistencyComparison == "Within")
        {
            w.Add("null");
        }
        else
        {
            w.String(
                uncertainty.consistencyComparison == "Exceeds" ? "SourceUncertaintyExceedsConsistencyBudget" :
                                                                 "SourceUncertaintyComparisonUnestablished"
            );
        }
        w.Add("}");
    }
    w.Add("]}");
}
} // namespace
namespace
{
void RepairEditJson(Writer& w, const FaceOrientationEdit& edit)
{
    w.Add("{\"source_definition_id\":");
    w.String(edit.definitionId);
    w.Add(",\"source_shape_id\":");
    w.String(edit.shapeId);
    w.Add(",\"source_face_id\":");
    w.String(edit.faceId);
    w.Add(",\"original_same_sense\":");
    w.Bool(edit.expectedSourceSameSense);
    w.Add(",\"effective_same_sense\":");
    w.Bool(edit.targetSameSense);
    w.Add("}");
}
void AssemblyJson(Writer& w, const AssemblyEvidence& evidence)
{
    const auto& tree = evidence.instances;
    const auto& source = tree.placements.source->SourceFields().Source();
    const auto id = [&](std::size_t record)
    {
        w.String(source.Records().at(record).id);
    };
    // A retained disconnected frame remains source-only: no assigned physical
    // unit, representation membership or target object is manufactured for it.
    w.Add(
        ",\"assembly\":{\"coordinate_system\":\"PerRootMillimetres\",\"matrix_layout\":\"RowMajor\",\"definition_geometry\":\"LocalUnmodified\",\"source_rewritten\":false,\"continuous_transform_error_certificate\":false,\"roots\":["
    );
    for (std::size_t i = 0; i < tree.roots.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        id(tree.roots[i]);
    }
    w.Add("],\"retained_unreferenced_placements\":");
    if (evidence.retainedPlacements)
    {
        const auto& retained = *evidence.retainedPlacements;
        w.Add(
            "{\"policy\":\"RetainDisconnected\",\"incoming_from_outside\":0,\"outgoing_to_outside\":0,\"target_geometry_created\":false,\"units_assigned\":false,\"placements\":["
        );
        for (std::size_t i = 0; i < retained.placements.size(); ++i)
        {
            if (i)
            {
                w.Add(",");
            }
            id(retained.placements[i]);
        }
        w.Add("],\"records\":[");
        for (std::size_t i = 0; i < retained.records.size(); ++i)
        {
            if (i)
            {
                w.Add(",");
            }
            id(retained.records[i]);
        }
        const auto& d = retained.originalDiagnostic;
        w.Add("],\"original_diagnostic\":{\"key\":");
        w.String(d.source.key);
        w.Add(",\"stage\":");
        w.String(d.source.stage);
        w.Add(",\"classification\":");
        w.String(d.classification);
        w.Add(",\"message\":");
        w.String(d.source.message);
        w.Add(",\"span\":");
        w.Range(d.source.primary);
        w.Add(",\"record_id\":");
        w.String(d.source.recordId);
        w.Add(",\"component\":");
        w.String(d.source.component);
        w.Add(",\"parameter_path\":");
        w.Path(d.source.parameterPath);
        w.Add(",\"target_id\":");
        w.String(d.source.targetId);
        w.Add(",\"field\":");
        w.String(d.field);
        w.Add(",\"declared_in\":");
        w.String(d.declaredIn);
        w.Add(",\"declared_type\":");
        w.String(d.declaredType);
        w.Add(",\"expected\":");
        w.String(d.expected);
        w.Add(",\"actual\":");
        w.String(d.actual);
        w.Add(",\"responsibility\":");
        w.String(d.responsibility);
        w.Add(",\"related_spans\":[");
        for (std::size_t i = 0; i < d.source.related.size(); ++i)
        {
            if (i)
            {
                w.Add(",");
            }
            w.Range(d.source.related[i]);
        }
        w.Add("]}}");
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"definitions\":[");
    for (std::size_t i = 0; i < evidence.definitions.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& d = evidence.definitions[i];
        w.Add("{\"source_definition_id\":");
        id(d.definition);
        w.Add(",\"role\":");
        w.String(d.role);
        w.Add(",\"source_shapes\":[");
        for (std::size_t j = 0; j < d.shapes.size(); ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            id(d.shapes[j]);
        }
        w.Add("]}");
    }
    w.Add("],\"contexts\":[");
    for (std::size_t i = 0; i < tree.placements.contexts.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& context = tree.placements.contexts[i];
        w.Add("{\"source_context\":");
        id(context.context);
        w.Add(",\"source_length_unit\":");
        id(tree.placements.source->FindContext(context.context)->units.lengthUnit);
        w.Add(",\"length_to_mm\":");
        FractionJson(w, context.length.scaleMm);
        w.Add(",\"conversion_chain\":[");
        for (std::size_t j = 0; j < context.length.definitions.size(); ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            const auto& d = context.length.definitions[j];
            w.Add("{\"source_unit\":");
            id(d.unit);
            w.Add(",\"source_factor\":");
            id(d.factor);
            w.Add(",\"source_base_unit\":");
            id(d.baseUnit);
            w.Add(",\"value_type\":");
            w.String(d.valueType);
            w.Add(",\"factor_value\":");
            FractionJson(w, d.factorValue);
            w.Add(",\"scale_mm\":");
            FractionJson(w, d.scaleMm);
            w.Add("}");
        }
        w.Add("]}");
    }
    w.Add("],\"local_placements\":[");
    const auto& bindings = tree.placements.source->Bindings().occurrences;
    for (std::size_t i = 0; i < tree.placements.occurrences.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& p = tree.placements.occurrences[i];
        const auto& b = bindings.at(i);
        w.Add("{\"source_usage\":");
        id(p.usage);
        w.Add(",\"source_relationship\":");
        id(b.representationRelationship);
        w.Add(",\"source_transform\":");
        id(b.transformation);
        w.Add(",\"source_context_dependent_shape\":");
        id(b.dependentShape);
        w.Add(",\"child_is_first_representation\":");
        w.Bool(b.childIsFirstRepresentation);
        w.Add(",\"child_representation\":");
        id(b.childRepresentation);
        w.Add(",\"parent_representation\":");
        id(b.parentRepresentation);
        w.Add(",\"child_definition\":");
        id(b.childDefinition);
        w.Add(",\"parent_definition\":");
        id(b.parentDefinition);
        w.Add(",\"child_context\":");
        id(b.childContext);
        w.Add(",\"parent_context\":");
        id(b.parentContext);
        w.Add(",\"child_item\":");
        id(b.childItem);
        w.Add(",\"parent_item\":");
        id(b.parentItem);
        w.Add(",\"rotation_child_to_parent\":[");
        for (std::size_t r = 0; r < 3; ++r)
        {
            if (r)
            {
                w.Add(",");
            }
            PointJson(w, p.rotation[r]);
        }
        w.Add("],\"translation_mm\":");
        PointJson(w, p.translationMm);
        w.Add(",\"orthonormal_residual\":");
        Number(w, p.orthonormalResidual);
        w.Add("}");
    }
    w.Add("],\"instances\":[");
    for (std::size_t i = 0; i < tree.instances.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& node = tree.instances[i];
        w.Add("{\"index\":");
        w.Number(i);
        w.Add(",\"source_definition_id\":");
        id(node.definition);
        w.Add(",\"root_index\":");
        w.Number(node.rootIndex);
        w.Add(",\"parent_instance\":");
        if (node.parentIndex)
        {
            w.Number(*node.parentIndex);
        }
        else
        {
            w.Add("null");
        }
        w.Add(",\"source_usage\":");
        if (node.usage)
        {
            id(*node.usage);
        }
        else
        {
            w.Add("null");
        }
        w.Add(",\"depth\":");
        w.Number(node.depth);
        w.Add(",\"rotation_to_root\":[");
        for (std::size_t r = 0; r < 3; ++r)
        {
            if (r)
            {
                w.Add(",");
            }
            PointJson(w, node.rotationToRoot[r]);
        }
        w.Add("],\"translation_to_root_mm\":");
        PointJson(w, node.translationToRootMm);
        w.Add("}");
    }
    w.Add("]}");
}
void WriteReportJson(
    Writer& w,
    Status status,
    const Report& report,
    const Quality& quality,
    const step_source::InspectOutcome& source,
    bool nestedBody = false,
    const AssemblyEvidence* assembly = nullptr,
    bool assemblyRequest = false
)
{
    if (report.generalBrep)
    {
        // Separate scope: do not serialize unexecuted planar proofs as
        // qualification for analytic/NURBS models or complete AP214 rules.
        w.Add(
            assembly || assemblyRequest ?
                "{\"format_version\":\"step-assembly-development.v1\",\"scope\":\"AssemblyDevelopment\",\"status\":" :
            nestedBody ?
                (report.sourceEvidenceScope == "RepairParent" ?
                     "{\"format_version\":\"step-repair-original-attempt.v1\",\"scope\":\"WithinRepairRequest\",\"status\":" :
                 report.sourceEvidenceScope == "AssemblyDefinitionRoot" || report.sourceEvidenceScope == "AssemblyPlacementWrapper" ?
                     "{\"format_version\":\"step-assembly-definition-checks.v1\",\"scope\":\"WithinAssembly\",\"status\":" :
                 report.sourceEvidenceScope == "ProductDocumentRoot" ?
                     "{\"format_version\":\"step-product-definition-checks.v1\",\"scope\":\"WithinProductDocument\",\"status\":" :
                     "{\"format_version\":\"step-material-body-checks.v1\",\"scope\":\"WithinMaterialCollection\",\"status\":") :
            report.productDocument ?
                (report.definitionSelectionRequest ?
                     "{\"format_version\":\"step-product-document-development.v1\",\"scope\":\"ExplicitSelectedProductDefinitions\",\"status\":" :
                     "{\"format_version\":\"step-product-document-development.v1\",\"scope\":\"IndependentProductDefinitions\",\"status\":") :
            report.materialCollection ?
                "{\"format_version\":\"step-material-collection-development.v1\",\"scope\":\"MaterialCollectionDevelopment\",\"status\":" :
                "{\"format_version\":\"step-general-native-development.v1\",\"scope\":\"GeneralBrepDevelopment\",\"status\":"
        );
        w.String(StatusName(status));
        if (assembly)
        {
            AssemblyJson(w, *assembly);
        }
        else if (assemblyRequest)
        {
            w.Add(",\"assembly\":null");
        }
        if (report.cylindricalBandRepairRequest || report.cylindricalBandRepairs)
        {
            const auto vertexRequest = report.cylindricalBandRepairRequest && report.cylindricalBandRepairRequest->circleVertices ?
                                           &*report.cylindricalBandRepairRequest->circleVertices :
                                           nullptr;
            w.Add(",\"cylindrical_band_repair\":{\"configuration\":");
            w.String(vertexRequest ? "ExactOrientedCylindricalBandWithCircleVertices-0.1" : "ExactOrientedCylindricalBand-0.1");
            w.Add(
                ",\"source_region_proof\":\"ExactCoaxialEqualRadiusOrientedWholeCircles\",\"effective_outer_markers\":\"None\",\"geometry_displacement_allowance_mm\":"
            );
            if (vertexRequest)
            {
                FractionJson(w, vertexRequest->maximumDisplacementMm);
            }
            else
            {
                w.Add("0");
            }
            w.Add(",\"tolerance_growth_allowance_mm\":0");
            if (report.cylindricalBandRepairRequest)
            {
                const auto& request = *report.cylindricalBandRepairRequest;
                w.Add(",\"requested_source_sha256\":");
                w.String(request.sourceSha256);
                w.Add(",\"maximum_faces\":");
                w.Number(request.maximumFaces);
                w.Add(",\"maximum_bound_edits\":");
                w.Number(request.maximumBoundEdits);
                w.Add(",\"maximum_proof_operations\":");
                w.Number(request.maximumProofOperations);
                w.Add(",\"proof_operations_spent\":");
                w.Number(report.cylindricalBandProofOperations);
                if (vertexRequest)
                {
                    w.Add(",\"circle_vertex_request\":{\"maximum_vertices\":");
                    w.Number(vertexRequest->maximumVertices);
                    w.Add(",\"maximum_displacement_mm\":");
                    FractionJson(w, vertexRequest->maximumDisplacementMm);
                    w.Add(",\"maximum_total_displacement_mm\":");
                    FractionJson(w, vertexRequest->maximumTotalDisplacementMm);
                    w.Add(",\"local_protection_ratio\":\"1/100\",\"vertices\":[");
                    for (std::size_t i = 0; i < vertexRequest->vertices.size(); ++i)
                    {
                        if (i)
                        {
                            w.Add(",");
                        }
                        const auto& e = vertexRequest->vertices[i];
                        w.Add("{\"definition_id\":");
                        w.String(e.definitionId);
                        w.Add(",\"shape_id\":");
                        w.String(e.shapeId);
                        w.Add(",\"vertex_id\":");
                        w.String(e.vertexId);
                        w.Add(",\"circle_id\":");
                        w.String(e.circleId);
                        w.Add("}");
                    }
                    w.Add("]}");
                }
            }
            const auto receipt = report.circleVertexBudget     ? report.circleVertexBudget :
                                 report.cylindricalBandRepairs ? report.cylindricalBandRepairs->circleVertexBudget :
                                                                 nullptr;
            if (receipt)
            {
                w.Add(",\"circle_vertex_budget\":{\"maximum_vertices\":");
                w.Number(receipt->maximumVertices);
                w.Add(",\"corrected_vertices\":");
                w.Number(receipt->correctedVertices);
                w.Add(",\"maximum_displacement_mm\":");
                FractionJson(w, receipt->maximumDisplacementMm);
                w.Add(",\"maximum_total_displacement_mm\":");
                FractionJson(w, receipt->maximumTotalDisplacementMm);
                w.Add(",\"cumulative_displacement_upper_mm\":");
                Number(w, receipt->cumulativeDisplacementUpperMm);
                w.Add(",\"minimum_protection_lower_mm\":");
                Number(w, receipt->minimumProtectionLowerMm);
                w.Add(",\"request_complete\":");
                w.Bool(receipt->requestComplete);
                w.Add("}");
            }
            w.Add(",\"evidence\":");
            if (report.cylindricalBandRepairs)
            {
                const auto& history = *report.cylindricalBandRepairs;
                w.Add("{\"source_sha256\":");
                w.String(history.sourceSha256);
                w.Add(",\"native_definitions_identical\":");
                w.Bool(history.definitionsIdentical);
                w.Add(",\"working_tolerances_identical\":");
                w.Bool(history.workingTolerancesIdentical);
                w.Add(",\"source_interpretation_applied\":");
                w.Bool(history.sourceInterpretationApplied);
                if (!history.circleVertices.empty())
                {
                    w.Add(",\"native_curves_and_surfaces_identical\":");
                    w.Bool(history.curvesAndSurfacesIdentical);
                    w.Add(",\"unrequested_vertices_identical\":");
                    w.Bool(history.unrequestedVerticesIdentical);
                    w.Add(",\"circle_vertices\":[");
                    for (std::size_t i = 0; i < history.circleVertices.size(); ++i)
                    {
                        if (i)
                        {
                            w.Add(",");
                        }
                        const auto& e = history.circleVertices[i];
                        w.Add("{\"definition_id\":");
                        w.String(e.edit.definitionId);
                        w.Add(",\"shape_id\":");
                        w.String(e.edit.shapeId);
                        w.Add(",\"vertex_id\":");
                        w.String(e.edit.vertexId);
                        w.Add(",\"circle_id\":");
                        w.String(e.edit.circleId);
                        w.Add(",\"source_point_id\":");
                        w.String(e.sourcePointId);
                        w.Add(",\"original_native_mm\":[");
                        for (std::size_t axis = 0; axis < 3; ++axis)
                        {
                            if (axis)
                            {
                                w.Add(",");
                            }
                            Number(w, e.originalMm[axis]);
                        }
                        w.Add("],\"corrected_mm\":[");
                        for (std::size_t axis = 0; axis < 3; ++axis)
                        {
                            if (axis)
                            {
                                w.Add(",");
                            }
                            Number(w, e.correctedMm[axis]);
                        }
                        w.Add("],\"displacement_upper_mm\":");
                        Number(w, e.displacementUpperMm);
                        w.Add(",\"projection_error_upper_mm\":");
                        Number(w, e.projectionErrorUpperMm);
                        w.Add(",\"original_gap_upper_mm\":");
                        Number(w, e.originalGapUpperMm);
                        w.Add(",\"local_protection_lower_mm\":");
                        Number(w, e.localProtectionLowerMm);
                        w.Add("}");
                    }
                    w.Add("]");
                }
                w.Add(",\"vertices_compared\":");
                w.Number(history.verticesCompared);
                w.Add(",\"edges_compared\":");
                w.Number(history.edgesCompared);
                w.Add(",\"surfaces_compared\":");
                w.Number(history.surfacesCompared);
                w.Add(",\"actions\":[");
                for (std::size_t i = 0; i < history.actions.size(); ++i)
                {
                    if (i)
                    {
                        w.Add(",");
                    }
                    const auto& a = history.actions[i];
                    w.Add("{\"definition_id\":");
                    w.String(a.edit.definitionId);
                    w.Add(",\"shape_id\":");
                    w.String(a.edit.shapeId);
                    w.Add(",\"face_id\":");
                    w.String(a.edit.faceId);
                    w.Add(",\"surface_id\":");
                    w.String(a.sourceSurfaceId);
                    w.Add(",\"height_ordered_bounds\":[");
                    w.String(a.heightOrderedBoundIds[0]);
                    w.Add(",");
                    w.String(a.heightOrderedBoundIds[1]);
                    w.Add("],\"height_ordered_circles\":[");
                    w.String(a.sourceCircleIds[0]);
                    w.Add(",");
                    w.String(a.sourceCircleIds[1]);
                    w.Add("]}");
                }
                w.Add("]}");
            }
            else
            {
                w.Add("null");
            }
            w.Add("}");
        }
        if (report.definitionSelectionRequest)
        {
            const auto strings = [&](const std::vector<std::string>& values)
            {
                w.Add("[");
                for (std::size_t i = 0; i < values.size(); ++i)
                {
                    if (i)
                    {
                        w.Add(",");
                    }
                    w.String(values[i]);
                }
                w.Add("]");
            };
            w.Add(",\"definition_selection\":{\"configuration\":");
            w.String(report.sourceErrorIsolationRequest ? step_semantics::SourceFaceClauseSelectionProfile : "SourceCompleteNativeSelection-0.1");
            if (report.sourceErrorIsolationRequest)
            {
                w.Add(",\"maximum_isolated_source_failures\":");
                w.Number(report.sourceErrorIsolationRequest->maximumFailures);
                w.Add(",\"maximum_source_influence_visits\":");
                w.Number(report.sourceErrorIsolationRequest->maximumInfluenceVisits);
            }
            w.Add(",\"requested_source_sha256\":");
            w.String(report.definitionSelectionRequest->sourceSha256);
            w.Add(",\"requested_definitions\":");
            strings(report.definitionSelectionRequest->definitionIds);
            w.Add(",\"qualified\":");
            if (report.definitionSelection)
            {
                const auto& receipt = *report.definitionSelection;
                w.Add("{\"source_sha256\":");
                w.String(receipt.sourceSha256);
                w.Add(",\"source_scope\":");
                w.String(receipt.sourceScope);
                if (report.sourceErrorIsolationRequest)
                {
                    w.Add(",\"isolated_source_failure_count\":");
                    w.Number(receipt.isolatedSourceFailureCount);
                }
                w.Add(",\"native_scope\":\"ExplicitSelectedDefinitions\",\"source_definitions\":");
                strings(receipt.sourceDefinitionIds);
                w.Add(",\"selected_definitions\":");
                strings(receipt.selectedDefinitionIds);
                w.Add(",\"not_requested_definitions\":");
                strings(receipt.excludedDefinitionIds);
                w.Add("}");
            }
            else
            {
                w.Add("null");
            }
            w.Add("}");
        }
        if (report.repairRequest)
        {
            const auto& request = *report.repairRequest;
            w.Add(",\"repair_request\":{\"configuration\":\"FaceOrientationZeroGeometry-0.1\",\"source_sha256\":");
            w.String(request.sourceSha256);
            w.Add(",\"maximum_face_edits\":");
            w.Number(request.maximumFaceEdits);
            w.Add(",\"maximum_proof_controls\":");
            w.Number(request.maximumProofControls);
            w.Add(",\"maximum_proof_products\":");
            w.Number(request.maximumProofProducts);
            w.Add(",\"proof_products_spent\":");
            w.Number(report.repairProofProducts);
            w.Add(",\"proof_accounting_scope\":\"CompletedProofOperations\"");
            w.Add(",\"geometry_displacement_allowance_mm\":0,\"tolerance_growth_allowance_mm\":0,\"edits\":[");
            for (std::size_t i = 0; i < request.edits.size(); ++i)
            {
                if (i)
                {
                    w.Add(",");
                }
                RepairEditJson(w, request.edits[i]);
            }
            w.Add("]}");
        }
        if (report.repairs)
        {
            const auto& repair = *report.repairs;
            w.Add(",\"repair_evidence\":{\"scope\":\"NativeBinary64Definitions\",\"source_sha256\":");
            w.String(repair.sourceSha256);
            w.Add(",\"source_interpretation_applied\":");
            w.Bool(repair.sourceInterpretationApplied);
            w.Add(",\"definitions_identical\":");
            w.Bool(repair.definitionsIdentical);
            w.Add(",\"working_tolerances_identical\":");
            w.Bool(repair.workingTolerancesIdentical);
            w.Add(",\"vertices_compared\":");
            w.Number(repair.verticesCompared);
            w.Add(",\"edges_compared\":");
            w.Number(repair.edgesCompared);
            w.Add(",\"surfaces_compared\":");
            w.Number(repair.surfacesCompared);
            w.Add(
                ",\"defining_data_displacement_bound_mm\":0,\"material_orientation_changed\":true,\"original_material_equivalence\":\"NotEstablished_InvalidOriginal\",\"tolerance_growth_mm\":0,\"actions\":["
            );
            for (std::size_t i = 0; i < repair.actions.size(); ++i)
            {
                if (i)
                {
                    w.Add(",");
                }
                const auto& action = repair.actions[i];
                w.Add("{\"edit\":");
                RepairEditJson(w, action.edit);
                w.Add(",\"target_face\":");
                w.Number(action.targetFace);
                w.Add(",\"injectivity_method\":\"ProjectedStrongMonotonicity\",\"projection\":[");
                for (std::size_t row = 0; row < 2; ++row)
                {
                    if (row)
                    {
                        w.Add(",");
                    }
                    w.Add("[");
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        if (k)
                        {
                            w.Add(",");
                        }
                        Number(w, action.projection[row][k]);
                    }
                    w.Add("]");
                }
                w.Add("],\"support_domain\":[");
                for (std::size_t k = 0; k < 4; ++k)
                {
                    if (k)
                    {
                        w.Add(",");
                    }
                    Number(w, action.supportDomain[k]);
                }
                w.Add("],\"first_diagonal_lower\":");
                Number(w, action.firstDiagonalLower);
                w.Add(",\"second_diagonal_lower\":");
                Number(w, action.secondDiagonalLower);
                w.Add(",\"symmetric_determinant_lower\":");
                Number(w, action.symmetricDeterminantLower);
                w.Add(",\"continuous_lift_bound_mm\":");
                Number(w, action.continuousLiftBoundMm);
                w.Add(",\"lift_cells\":");
                w.Number(action.liftCells);
                w.Add("}");
            }
            w.Add("]}");
        }
        if (report.originalAttempt)
        {
            w.Add(",\"original_attempt\":");
            WriteReportJson(w, report.originalAttemptStatus.value_or(Status::PostcheckFailure), *report.originalAttempt, quality, source, true);
        }
        if (!report.repairStage.empty())
        {
            w.Add(",\"repair_progress\":{\"stage\":");
            w.String(report.repairStage);
            w.Add(",\"edits_attempted\":");
            w.Number(report.repairEditsAttempted);
            w.Add(",\"model_published\":");
            w.Bool(status == Status::AcceptedWithRepairs);
            w.Add("}");
        }
        if (report.periodicNormalizationRequested || report.topologyNormalization)
        {
            w.Add(",\"periodic_normalization_requested\":");
            w.Add(report.periodicNormalizationRequested ? "true" : "false");
            w.Add(",\"topology_normalization\":");
            if (report.topologyNormalization)
            {
                TopologyJson(w, *report.topologyNormalization);
            }
            else
            {
                w.Add("null");
            }
        }
        TerminalDomainJson(w, report);
        SurfaceCurve3dJson(w, report);
        if (report.exactGeometryWork)
        {
            w.Add(",\"exact_geometry_work\":{\"maximum_units\":");
            w.Number(report.exactGeometryWork->maximumUnits);
            w.Add(",\"used_units\":");
            w.Number(report.exactGeometryWork->usedUnits);
            w.Add("}");
        }
        if (!report.revolutionPoleVisits.empty())
        {
            w.Add(",\"revolution_pole_visits\":[");
            for (std::size_t i = 0; i < report.revolutionPoleVisits.size(); ++i)
            {
                if (i)
                {
                    w.Add(",");
                }
                const auto& proof = report.revolutionPoleVisits[i];
                w.Add("{\"source_face\":");
                w.String(proof.sourceFaceId);
                w.Add(",\"source_surface\":");
                w.String(proof.sourceSurfaceId);
                w.Add(",\"target_face\":");
                w.Number(proof.targetFaceIndex);
                w.Add(",\"native_pole_mask\":");
                w.Number(proof.nativePoleMask);
                w.Add(",\"checked_edge_pole_pairs\":");
                w.Number(proof.checkedEdges);
                w.Add(",\"bezier_patches\":");
                w.Number(proof.patches);
                w.Add(",\"exact_work_units\":");
                w.Number(proof.exactWorkUnits);
                w.Add(",\"native_pole_radius_mm\":");
                Number(w, proof.nativePoleRadiusMm);
                w.Add(",\"evidence\":\"EndpointConnectedNativePoleZoneVisits\",\"source_exact_poles_qualified\":false}");
            }
            w.Add("]");
        }
        if (report.supportDomainRestrictionRequested)
        {
            w.Add(",\"support_domain_restriction_requested\":true");
        }
        if (report.sourceSchema && report.sourceSchema->appliedInterpretation != step_semantics::SchemaInterpretation::DeclaredOnly)
        {
            w.Add(",\"source_binding_verified\":false,\"source_interpretation\":");
            w.String(step_semantics::SchemaInterpretationName(report.sourceSchema->appliedInterpretation));
        }
        const auto carrier = source.source ? source.source->AppliedCarrierAssumption() : report.carrierAssumption.get();
        if (source.requestedCarrierInterpretation != step_source::CarrierInterpretation::DeclaredOnly || carrier)
        {
            w.Add(",\"requested_carrier_interpretation\":");
            w.String(step_source::CarrierInterpretationName(source.requestedCarrierInterpretation));
            w.Add(",\"source_carrier_verified\":");
            w.Bool(source.source && source.status == step_source::Status::Inspected && !carrier);
            if (carrier)
            {
                w.Add(",\"source_carrier_interpretation\":");
                w.String(step_source::CarrierInterpretationName(carrier->interpretation));
            }
        }
        if (source.source && source.source->HasAsciiInvariantDataAssumption())
        {
            w.Add(",\"data_string_interpretation\":\"PreserveAsciiInvariantBytes\",\"data_encoding_verified\":false");
        }
        const auto sourceView = report.generalSource ? (report.generalSource->snapshot ? report.generalSource->snapshot :
                                                        report.generalSource->document ? report.generalSource->document->Part(0) :
                                                                                         std::shared_ptr<const step_semantics::Snapshot>{}) :
                                                       std::shared_ptr<const step_semantics::Snapshot>{};
        if (sourceView && sourceView->AppliedUnitInterpretation() != step_semantics::UnitInterpretation::DeclaredOnly)
        {
            w.Add(",\"source_unit_interpretation\":");
            w.String(step_semantics::UnitInterpretationName(sourceView->AppliedUnitInterpretation()));
            w.Add(",\"source_unit_declaration_conforming\":false");
        }
        w.Add(",\"repair\":");
        w.String(
            report.circleVertexBudget && report.circleVertexBudget->requestComplete && IsAcceptedReportStatus(status) ?
                "ExactOrientedCylindricalBandWithCircleVertices-0.1" :
            report.cylindricalBandRepairs ?
                (!report.cylindricalBandRepairs->circleVertices.empty() ? "ExactOrientedCylindricalBandWithCircleVertices-0.1" :
                                                                          "ExactOrientedCylindricalBand-0.1") :
            report.repairs ? "FaceOrientationZeroGeometry-0.1" :
                             "None"
        );
        w.Add(",\"quality_id\":");
        w.String(quality.id);
        if (report.numericalIntegration)
        {
            const auto& integration = *report.numericalIntegration;
            w.Add(",\"numerical_integration\":{\"method\":");
            const bool grouped = report.materialCollection || report.productDocument || assembly || assemblyRequest;
            const bool surface = report.sourceShape && report.sourceShape->role != "MaterialSolid";
            w.String(grouped ? "PerDefinitionFaceProperties" : surface ? "SMLibAreaFaceProperties" : "SMLibFusedFaceProperties");
            w.Add(",\"control_scope\":\"DimensionlessNumericalStoppingOnly\",\"requested_relative_accuracy\":");
            Number(w, integration.requestedRelativeAccuracy);
            w.Add(",\"applied_relative_accuracy\":");
            if (integration.appliedRelativeAccuracy)
            {
                Number(w, *integration.appliedRelativeAccuracy);
            }
            else
            {
                w.Add("null");
            }
            w.Add(",\"completed\":");
            w.Bool(integration.completed);
            w.Add(",\"continuous_error_bound_established\":false}");
        }
        w.Add(",\"quality\":{");
        const std::pair<const char*, Fraction> budgets[] = { { "definition_mm", quality.definitionMm },
                                                             { "consistency_mm", quality.consistencyMm },
                                                             { "derived_uv_mm", quality.derivedUvMm },
                                                             { "initial_work_tolerance_mm", quality.initialWorkToleranceMm },
                                                             { "maximum_work_tolerance_mm", quality.maximumWorkToleranceMm },
                                                             { "tolerance_growth_mm", quality.toleranceGrowthMm },
                                                             { "query_mm", quality.queryMm } };
        bool budgetComma = false;
        for (const auto& [name, value] : budgets)
        {
            if (budgetComma)
            {
                w.Add(",");
            }
            budgetComma = true;
            w.String(name);
            w.Add(":");
            FractionJson(w, value);
        }
        w.Add("}");
        if (report.units && !report.units->generalDefinitions.empty())
        {
            const auto& units = *report.units;
            const auto id = [&](std::size_t record) -> const std::string&
            {
                return report.source->Records().at(record).id;
            };
            w.Add(",\"unit_normalization\":{\"source_length_unit\":");
            w.String(units.sourceLengthUnitId);
            w.Add(",\"length_to_mm\":");
            FractionJson(w, units.lengthToMm);
            w.Add(",\"line_parameter\":\"DimensionlessUnchanged\",\"direction_ratios\":\"Dimensionless\",\"definitions\":[");
            bool comma = false;
            for (const auto& definition : units.generalDefinitions)
            {
                if (comma)
                {
                    w.Add(",");
                }
                comma = true;
                w.Add("{\"source_unit\":");
                w.String(id(definition.unit));
                w.Add(",\"source_factor\":");
                w.String(id(definition.factor));
                w.Add(",\"source_base_unit\":");
                w.String(id(definition.baseUnit));
                w.Add(",\"value_type\":");
                w.String(definition.valueType);
                w.Add(",\"factor_value\":");
                FractionJson(w, definition.factorValue);
                w.Add(",\"scale_mm\":");
                FractionJson(w, definition.scaleMm);
                w.Add("}");
            }
            w.Add("]}");
        }
        w.Add(",\"measurement_policy\":{\"native_requested_relative_accuracy\":");
        if (report.numericalIntegration)
        {
            Number(w, report.numericalIntegration->requestedRelativeAccuracy);
        }
        else
        {
            w.Add("0.0001"); // Retain the exact legacy spelling for unconfigured requests.
        }
        if (report.materialCollection || report.productDocument || assembly || assemblyRequest)
        {
            w.Add(",\"integration_scope\":\"PerDefinitionOnly\",\"absolute_area_volume_error_bound\":\"NotEstablished\"");
        }
        else if (report.sourceShape && report.sourceShape->role != "MaterialSolid")
        {
            w.Add(
                ",\"area_centroid_accumulation_origin\":\"AxisAlignedBoundsMidpoint\",\"face_area_origin\":\"FaceBoundsMidpoint\",\"absolute_area_error_bound\":\"NotEstablished\",\"material_volume\":\"NotApplicable\""
            );
        }
        else
        {
            w.Add(",\"mass_integration_origin\":\"AxisAlignedBoundsMidpoint\",\"absolute_area_volume_error_bound\":\"NotEstablished\"");
        }
        w.Add(
            ",\"global_definition_error_bound\":\"NotEstablished\",\"global_curve_surface_error_bound\":\"NotEstablished\",\"final_trim_measurement\":\"PairedParameter8SubintervalsPerUnionKnotSpan\"}"
        );
        w.Add(
            ",\"qualification\":\"SelectedSourceChecksAndNativeMeasuredPostchecks\",\"full_schema_conformance\":false,\"planar_proofs\":\"NotApplicable\",\"source_sha256\":"
        );
        w.String(report.sourceSha256);
        w.Add(",\"native_calls\":");
        w.Number(report.nativeCalls);
        w.Add(",\"candidate_discarded\":");
        w.Bool(report.candidateDiscarded);
        w.Add(",\"last_boundary\":");
        w.String(PhaseName(report.lastBoundary));
        if (report.encoding == ReportEncoding::CompactV2)
        {
            w.Add(",\"source_report_encoding\":\"CompactV2\"");
        }
        w.Add(",\"source_inspection\":");
        if (report.generalSource)
        {
            step_semantics::report::WriteJson(
                w,
                source,
                *report.generalSource,
                report.encoding == ReportEncoding::CompactV2 ? step_semantics::report::Encoding::AttributeDictionaryV2 :
                                                               step_semantics::report::Encoding::ExpandedV1
            );
        }
        else
        {
            w.Add("null");
        }
        if (nestedBody)
        {
            w.Add(",\"source_evidence\":");
            w.String(report.sourceEvidenceScope.empty() ? "CollectionRoot" : report.sourceEvidenceScope);
            w.Add(",\"execution_evidence\":");
            w.String(report.sourceEvidenceScope.empty() ? "CollectionRoot" : report.sourceEvidenceScope);
        }
        if (report.materialCollection)
        {
            w.Add(",\"material_collection\":{\"complete\":");
            w.Bool(IsAcceptedReportStatus(status));
            w.Add(",\"selected_body_count\":");
            w.Number(
                report.generalSource && report.generalSource->snapshot ? report.generalSource->snapshot->PartDefinition().shapes.size() :
                                                                         report.selectedBodyCount
            );
            w.Add(",\"bodies\":[");
            for (std::size_t i = 0; i < report.bodies.size(); ++i)
            {
                if (i)
                {
                    w.Add(",");
                }
                const auto& body = report.bodies[i];
                w.Add("{\"ordinal\":");
                w.Number(i);
                w.Add(",\"source_shape_id\":");
                w.String(source.source->Records().at(body.sourceShapeRecord).id);
                w.Add(",\"source_shell_id\":");
                w.String(source.source->Records().at(body.sourceShellRecord).id);
                w.Add(",\"checks_passed\":");
                w.Bool(body.checksPassed);
                w.Add(",\"checks\":");
                if (body.checks)
                {
                    WriteReportJson(w, status, *body.checks, quality, source, true);
                }
                else
                {
                    w.Add("null");
                }
                w.Add("}");
            }
            w.Add("]}");
        }
        if (report.productDocument || assembly || assemblyRequest)
        {
            w.Add(",\"product_document\":{\"complete\":");
            w.Bool(IsAcceptedReportStatus(status));
            w.Add(",\"selected_definition_count\":");
            w.Number(
                assembly                          ? assembly->definitions.size() :
                report.definitionSelectionRequest ? (report.definitionSelection ? report.definitionSelection->selectedDefinitionIds.size() : 0) :
                report.generalSource && report.generalSource->document ? report.generalSource->document->PartCount() :
                                                                         0
            );
            w.Add(",\"definitions\":[");
            for (std::size_t i = 0; i < report.definitions.size(); ++i)
            {
                if (i)
                {
                    w.Add(",");
                }
                const auto& definition = report.definitions[i];
                w.Add("{\"source_definition_id\":");
                w.String(source.source->Records().at(definition.sourceDefinitionRecord).id);
                w.Add(",\"checks_passed\":");
                w.Bool(definition.checksPassed);
                w.Add(",\"checks\":");
                if (definition.checks)
                {
                    WriteReportJson(w, status, *definition.checks, quality, source, true);
                }
                else
                {
                    w.Add("null");
                }
                w.Add("}");
            }
            w.Add("]}");
        }
        w.Add(",\"diagnostics\":[");
        for (std::size_t i = 0; i < report.diagnostics.size(); ++i)
        {
            if (i)
            {
                w.Add(",");
            }
            const auto& d = report.diagnostics[i];
            w.Add("{\"key\":");
            w.String(d.source.key);
            w.Add(",\"record_id\":");
            w.String(d.source.recordId);
            w.Add(",\"stage\":");
            w.String(d.source.stage);
            if (d.classification == "NativeFaceLoopOrientationFailure" || d.classification == "NativePeriodicSourceVertexNearMiss")
            {
                w.Add(",\"classification\":");
                w.String(d.classification);
            }
            w.Add(",\"message\":");
            w.String(d.source.message);
            w.Add("}");
        }
        w.Add("],\"model\":");
        if (report.native && IsAcceptedReportStatus(status))
        {
            StatisticsJson(w, *report.native, true);
        }
        else
        {
            w.Add("null");
        }
        GeneralNativeEvidenceJson(w, report);
        w.Add(",\"validations\":{\"pointer_reports\":");
        if (report.pointerReports)
        {
            w.Number(*report.pointerReports);
        }
        else
        {
            w.Add("null");
        }
        w.Add(",\"level2_reports\":");
        if (report.deepReports)
        {
            w.Number(*report.deepReports);
        }
        else
        {
            w.Add("null");
        }
        w.Add("},\"measurements\":[");
        for (std::size_t i = 0; i < report.tolerances.size(); ++i)
        {
            if (i)
            {
                w.Add(",");
            }
            const auto& t = report.tolerances[i];
            w.Add("{\"stage\":");
            w.String(t.stage);
            w.Add(",\"kind\":");
            w.String(t.kind);
            w.Add(",\"source_id\":");
            w.String(t.sourceId);
            w.Add(",\"value_mm\":");
            Number(w, t.valueMm);
            w.Add("}");
        }
        w.Add("]");
        if (!report.surfaceNormalizations.empty())
        {
            w.Add(",\"surface_normalizations\":[");
            for (std::size_t i = 0; i < report.surfaceNormalizations.size(); ++i)
            {
                if (i)
                {
                    w.Add(",");
                }
                const auto& info = report.surfaceNormalizations[i];
                w.Add("{\"kind\":\"ToroidalOriginalBasisRestriction\",\"source_face_id\":");
                w.String(info.sourceFaceId);
                w.Add(",\"source_surface_id\":");
                w.String(info.sourceSurfaceId);
                w.Add(",\"target_face_index\":");
                w.Number(info.targetFaceIndex);
                const auto array = [&](std::string_view name, const auto& values)
                {
                    w.Add(",");
                    w.String(name);
                    w.Add(":[");
                    for (std::size_t j = 0; j < values.size(); ++j)
                    {
                        if (j)
                        {
                            w.Add(",");
                        }
                        Number(w, values[j]);
                    }
                    w.Add("]");
                };
                array("original_native_domain", info.originalNativeDomain);
                array("retained_native_domain", info.retainedNativeDomain);
                array("original_analytic_domain_degrees", info.originalAnalyticDomainDegrees);
                array("generator_radius_bounds_mm", info.generatorRadiusBoundsMm);
                w.Add(",\"minimum_absolute_generator_radius_mm\":");
                Number(w, info.minimumAbsoluteGeneratorRadiusMm);
                w.Add(",\"continuous_difference_upper_mm\":");
                Number(w, info.continuousDifferenceUpperMm);
                w.Add(",\"tensor_patches\":");
                w.Number(info.tensorPatches);
                w.Add(",\"source_rewritten\":false}");
            }
            w.Add("]");
        }
        if (!report.revolutionCharts.empty())
        {
            w.Add(",\"revolution_charts\":[");
            for (std::size_t i = 0; i < report.revolutionCharts.size(); ++i)
            {
                if (i)
                {
                    w.Add(",");
                }
                const auto& info = report.revolutionCharts[i];
                w.Add("{\"source_face_id\":");
                w.String(info.sourceFaceId);
                w.Add(",\"source_surface_id\":");
                w.String(info.sourceSurfaceId);
                w.Add(",\"target_face_index\":");
                w.Number(info.targetFaceIndex);
                w.Add(",\"u_binary_exponent\":");
                Number(w, double(info.uBinaryExponent));
                const auto domain = [&](std::string_view key, const auto& values)
                {
                    w.Add(",");
                    w.String(key);
                    w.Add(":[");
                    for (std::size_t j = 0; j < values.size(); ++j)
                    {
                        if (j)
                        {
                            w.Add(",");
                        }
                        Number(w, values[j]);
                    }
                    w.Add("]");
                };
                domain("original_native_domain", info.originalNativeDomain);
                domain("normalized_native_domain", info.normalizedNativeDomain);
                domain("native_step_domain_degrees_and_generator_t", info.nativeStepDomain);
                w.Add(",\"center_condition_before\":");
                Number(w, info.centerConditionBefore);
                w.Add(",\"center_condition_after\":");
                Number(w, info.centerConditionAfter);
                w.Add(",\"exact_stored_values\":");
                w.Number(info.exactStoredValues);
                w.Add(",\"distinct_derived_trims\":");
                w.Number(info.distinctDerivedTrims);
                w.Add(",\"original_small_radius_reports\":");
                w.Number(info.originalSmallRadiusReports);
                w.Add(",\"map\":\"new_u=ldexp(old_u,k);new_v=old_v\",\"source_rewritten\":false}");
            }
            w.Add("]");
        }
        w.Add(",\"representation_normalizations\":[");
        for (std::size_t i = 0; i < report.normalizations.size(); ++i)
        {
            if (i)
            {
                w.Add(",");
            }
            const auto& change = report.normalizations[i];
            w.Add("{\"kind\":");
            w.String(change.kind);
            w.Add(",\"source_face\":");
            w.String(change.sourceFaceId);
            const auto ids = [&](const char* name, const std::vector<std::string>& values)
            {
                w.Add(",");
                w.String(name);
                w.Add(":[");
                for (std::size_t k = 0; k < values.size(); ++k)
                {
                    if (k)
                    {
                        w.Add(",");
                    }
                    w.String(values[k]);
                }
                w.Add("]");
            };
            ids("source_bounds", change.sourceBoundIds);
            ids("source_vertices", change.sourceVertexIds);
            w.Add(",\"target_edge_index\":");
            if (change.targetEdgeIndex)
            {
                w.Number(*change.targetEdgeIndex);
            }
            else
            {
                w.Add("null");
            }
            w.Add(",\"source_rewritten\":false}");
        }
        w.Add("],\"parameter_mappings\":[");
        for (std::size_t i = 0; i < report.parameterMappings.size(); ++i)
        {
            if (i)
            {
                w.Add(",");
            }
            const auto& map = report.parameterMappings[i];
            w.Add("{\"kind\":");
            w.String(map.kind);
            w.Add(",\"source_geometry\":");
            w.String(map.sourceGeometryId);
            if (map.sourceBasisGeometryId)
            {
                w.Add(",\"source_basis_geometry\":");
                w.String(*map.sourceBasisGeometryId);
            }
            if (map.targetCurveIndex)
            {
                w.Add(",\"target_curve_index\":");
                w.Number(*map.targetCurveIndex);
            }
            w.Add(",\"source_use\":");
            w.String(map.sourceUseId);
            w.Add(",\"equation\":");
            w.String(map.equation);
            w.Add(",\"target_edge_index\":");
            if (map.targetEdgeIndex)
            {
                w.Number(*map.targetEdgeIndex);
            }
            else
            {
                w.Add("null");
            }
            w.Add(",\"target_face_index\":");
            if (map.targetFaceIndex)
            {
                w.Number(*map.targetFaceIndex);
            }
            else
            {
                w.Add("null");
            }
            w.Add(",\"parameters\":{");
            for (std::size_t j = 0; j < map.parameters.size(); ++j)
            {
                if (j)
                {
                    w.Add(",");
                }
                w.String(map.parameters[j].first);
                w.Add(":[");
                for (std::size_t k = 0; k < map.parameters[j].second.size(); ++k)
                {
                    if (k)
                    {
                        w.Add(",");
                    }
                    Number(w, map.parameters[j].second[k]);
                }
                w.Add("]");
            }
            w.Add("}}");
        }
        w.Add("]");
        w.Add(",\"classified_outer_bounds\":[");
        std::unordered_set<std::string_view> splineClassifiedFaces;
        for (const auto& normalization : report.normalizations)
        {
            if (normalization.kind == "NativeSplineCurveIntersectionContainment")
            {
                splineClassifiedFaces.insert(normalization.sourceFaceId);
            }
        }
        for (std::size_t i = 0; i < report.classifiedOuterBounds.size(); ++i)
        {
            if (i)
            {
                w.Add(",");
            }
            w.Add("{\"source_face\":");
            w.String(report.classifiedOuterBounds[i].first);
            w.Add(",\"source_bound\":");
            w.String(report.classifiedOuterBounds[i].second);
            w.Add(",\"method\":");
            w.String(
                splineClassifiedFaces.contains(report.classifiedOuterBounds[i].first) ? "NativeSplineCurveIntersectionContainment" :
                                                                                        "NativePlanarCurveIntersectionContainment"
            );
            w.Add(",\"source_rewritten\":false}");
        }
        w.Add("]");
        if (!report.wireframes.empty())
        {
            w.Add(",\"linked_wireframes\":[");
            for (std::size_t i = 0; i < report.wireframes.size(); ++i)
            {
                if (i)
                {
                    w.Add(",");
                }
                const auto& group = report.wireframes[i];
                w.Add("{\"source_representation\":");
                w.String(group.representationId);
                w.Add(",\"source_relationship\":");
                w.String(group.relationshipId);
                w.Add(",\"source_set\":");
                w.String(group.setId);
                w.Add(",\"role\":\"IndependentAssociatedWireframe\",\"material_boundary\":false,\"bounds_mm\":[");
                PointJson(w, group.boundsMm[0]);
                w.Add(",");
                PointJson(w, group.boundsMm[1]);
                w.Add("],\"curves\":[");
                for (std::size_t j = 0; j < group.curves.size(); ++j)
                {
                    if (j)
                    {
                        w.Add(",");
                    }
                    DetachedCurveJson(w, group.curves[j]);
                }
                w.Add("],\"points\":[");
                for (std::size_t j = 0; j < group.points.size(); ++j)
                {
                    if (j)
                    {
                        w.Add(",");
                    }
                    PointMappingJson(w, group.points[j]);
                }
                w.Add("]}");
            }
            w.Add("]");
        }
        if (!report.supportingGeometry.empty())
        {
            SupportingJson(w, report.supportingGeometry);
        }
        w.Add("}");
        return;
    }
    w.Add("{\"format_version\":\"step-native-import.v2\",\"scope\":\"PlanarNativeImport\",\"status\":");
    w.String(StatusName(status));
    w.Add(",\"repair\":\"None\",\"profile\":");
    w.String(step_semantics::Profile);
    w.Add(",\"schema_sha256\":");
    w.String(step_semantics::SchemaSha256);
    w.Add(",\"source\":{\"sha256\":");
    if (report.sourceSha256.empty())
    {
        w.Add("null");
    }
    else
    {
        w.String(report.sourceSha256);
    }
    w.Add(",\"captured\":");
    w.Bool(bool(report.captured));
    w.Add(",\"records\":");
    if (report.source)
    {
        w.Number(report.source->Records().size());
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"read\":");
    w.String(report.sourceStatus);
    w.Add(",\"semantics\":");
    w.String(report.semanticStatus);
    w.Add(",\"planar\":");
    w.String(report.planarStatus);
    w.Add("}");
    w.Add(",\"source_schema\":");
    step_semantics::report::WriteSchemaSelection(w, report.sourceSchema);
    w.Add(",\"execution\":{\"last_boundary\":");
    w.String(PhaseName(report.lastBoundary));
    w.Add(",\"checkpoints\":");
    w.Number(report.checkpoints);
    w.Add(",\"memory_observation_cutoff\":\"BeforeFinalReportSerialization\",\"maximum_observed_private_bytes\":");
    const auto memory = [&](const std::optional<std::size_t>& value)
    {
        if (value)
        {
            w.Number(*value);
        }
        else
        {
            w.Add("null");
        }
    };
    memory(report.maximumObservedPrivateBytes);
    w.Add(",\"process_lifetime_peak_commit_bytes\":");
    memory(report.processPeakCommitBytes);
    w.Add(",\"process_private_limit_bytes\":");
    memory(report.processPrivateLimitBytes);
    w.Add(",\"hard_memory_limit_bytes\":");
    memory(report.hardMemoryLimitBytes);
    w.Add(",\"deadline_configured\":");
    w.Bool(report.deadlineConfigured);
    w.Add("}");
    w.Add(",\"source_declarations\":");
    if (report.declarations)
    {
        DeclarationsJson(w, *report.declarations);
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"source_shape\":");
    if (report.sourceShape)
    {
        const auto& shape = *report.sourceShape;
        w.Add("{");
        const std::pair<const char*, const std::string*> fields[] = { { "role", &shape.role },
                                                                      { "representation_id", &shape.representationId },
                                                                      { "representation_type", &shape.representationType },
                                                                      { "shape_id", &shape.shapeId },
                                                                      { "shape_type", &shape.shapeType },
                                                                      { "shell_id", &shape.shellId },
                                                                      { "shell_type", &shape.shellType } };
        bool comma = false;
        for (const auto& [name, value] : fields)
        {
            if (comma)
            {
                w.Add(",");
            }
            comma = true;
            w.String(name);
            w.Add(":");
            w.String(*value);
        }
        w.Add("}");
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"unit_normalization\":");
    if (report.units)
    {
        const auto& units = *report.units;
        w.Add("{\"interpretation\":\"Checked\",\"source_length_unit\":");
        w.String(units.sourceLengthUnitId);
        w.Add(",\"length_to_mm\":");
        FractionJson(w, units.lengthToMm);
        w.Add(",\"source_plane_uv_to_native_scale\":");
        FractionJson(w, units.lengthToMm);
        w.Add(
            ",\"line_parameter\":\"DimensionlessUnchanged\",\"direction_ratios\":\"Dimensionless\","
            "\"canonical_length\":\"mm\",\"canonical_area\":\"mm^2\",\"canonical_volume\":\"mm^3\",\"definitions\":["
        );
        bool comma = false;
        for (const auto& definition : units.definitions)
        {
            if (comma)
            {
                w.Add(",");
            }
            comma = true;
            const auto id = [&](std::size_t r) -> const std::string&
            {
                return report.source->Records().at(r).id;
            };
            w.Add("{\"source_unit\":");
            w.String(id(definition.unit));
            w.Add(",\"source_dimensions\":");
            w.String(id(definition.dimensions));
            w.Add(",\"source_factor\":");
            w.String(id(definition.factor));
            w.Add(",\"source_base_unit\":");
            w.String(id(definition.baseUnit));
            w.Add(",\"value_type\":");
            w.String(definition.valueType);
            w.Add(",\"factor_value\":");
            FractionJson(w, definition.factorValue);
            w.Add(",\"scale_mm\":");
            FractionJson(w, definition.scaleMm);
            w.Add(",\"dimensions\":[");
            for (std::size_t i = 0; i < 7; ++i)
            {
                if (i)
                {
                    w.Add(",");
                }
                FractionJson(w, definition.exponents[i]);
            }
            w.Add("]}");
        }
        w.Add("]}");
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"native_role\":");
    if (report.nativeRole)
    {
        const auto& role = *report.nativeRole;
        w.Add("{\"regions\":");
        w.Number(role.regions);
        w.Add(",\"shells\":");
        w.Number(role.shells);
        w.Add(",\"material_regions\":");
        w.Number(role.materialRegions);
        w.Add(",\"bounded_void_regions\":");
        w.Number(role.boundedVoidRegions);
        w.Add(",\"manifold_solid_predicate\":");
        w.Bool(role.manifoldSolidPredicate);
        w.Add("}");
    }
    else
    {
        w.Add("null");
    }
    w.Add(
        ",\"normalization_policy\":{\"curve_parameter\":\"SourceIncreasingToNative01\","
        "\"loop_order\":\"OuterFirstIdentityPreserving\","
        "\"uv\":\"DerivedAfterTopologyOnSameOwner\",\"finite_window\":\"SourceTrimEnvelope\","
        "\"window_protection\":\"OutwardIntervalPlus64EpsilonTimes1PlusMaxAbsPerAxis\","
        "\"definition_fitting\":false}"
    );
    w.Add(",\"quality\":{\"id\":");
    w.String(quality.id);
    const std::pair<const char*, Fraction> fields[] = { { "definition_mm", quality.definitionMm },
                                                        { "consistency_mm", quality.consistencyMm },
                                                        { "derived_uv_mm", quality.derivedUvMm },
                                                        { "initial_work_tolerance_mm", quality.initialWorkToleranceMm },
                                                        { "maximum_work_tolerance_mm", quality.maximumWorkToleranceMm },
                                                        { "tolerance_growth_mm", quality.toleranceGrowthMm },
                                                        { "query_mm", quality.queryMm },
                                                        { "area_mm2", quality.areaMm2 },
                                                        { "volume_mm3", quality.volumeMm3 } };
    for (const auto& [name, value] : fields)
    {
        w.Add(",");
        w.String(name);
        w.Add(":");
        FractionJson(w, value);
    }
    w.Add("}");
    w.Add(",\"native_calls\":");
    w.Number(report.nativeCalls);
    w.Add(",\"candidate_discarded\":");
    w.Bool(report.candidateDiscarded);
    w.Add(",\"kernel_path\":");
    if (report.kernelPath.empty())
    {
        w.Add("null");
    }
    else
    {
        w.String(report.kernelPath);
    }
    w.Add(",\"model\":");
    if (status == Status::Accepted)
    {
        w.Add("{\"kind\":\"OwningReadOnlySnapshot\",\"role\":");
        w.String(report.native->shapeRole);
        w.Add("}");
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"native_measurements\":");
    if (report.native)
    {
        StatisticsJson(w, *report.native);
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"face_mappings\":[");
    for (std::size_t i = 0; i < report.faces.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& face = report.faces[i];
        w.Add("{\"source_face\":");
        w.String(face.faceId);
        w.Add(",\"source_plane\":");
        w.String(face.planeId);
        w.Add(",\"source_same_sense\":");
        w.Bool(face.sourceSameSense);
        w.Add(",\"measurements\":");
        StatisticsJson(w, face.native);
        w.Add("}");
    }
    w.Add("],\"edge_adjacency\":[");
    for (std::size_t i = 0; i < report.adjacency.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& edge = report.adjacency[i];
        w.Add("{\"source_edge\":");
        w.String(edge.edgeId);
        w.Add(",\"native_edgeuses\":");
        w.Number(edge.nativeUses);
        w.Add(",\"source_faces\":[");
        for (std::size_t j = 0; j < edge.faceIds.size(); ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            w.String(edge.faceIds[j]);
        }
        w.Add("]}");
    }
    w.Add("],\"material_census\":{\"solid_regions\":");
    if (report.solidRegions)
    {
        w.Number(*report.solidRegions);
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"void_cavities\":");
    if (report.voidCavities)
    {
        w.Number(*report.voidCavities);
    }
    else
    {
        w.Add("null");
    }
    w.Add("},\"volume_bound_mm3\":");
    Optional(w, report.volumeBoundMm3);
    w.Add(",\"volume_junction_bound_mm\":");
    Optional(w, report.volumeJunctionBoundMm);
    w.Add(",\"volume_gap_flux_bound_mm3\":");
    Optional(w, report.volumeGapFluxBoundMm3);
    w.Add(",\"volume_skipped_flux_bound_mm3\":");
    Optional(w, report.volumeSkippedFluxBoundMm3);
    w.Add(",\"volume_evidence_policy\":");
    w.String(
        report.volumeBoundMm3                                             ? "ActualOrientedFiniteSegmentsZGaugeGapAndTinyDuBounds" :
        report.sourceShape && report.sourceShape->role != "MaterialSolid" ? "NotApplicable" :
                                                                            "NotRun"
    );
    w.Add(",\"definition_bound_mm\":");
    Optional(w, report.definitionBoundMm);
    w.Add(",\"consistency_bound_mm\":");
    Optional(w, report.consistencyBoundMm);
    w.Add(",\"uv_bound_mm\":");
    Optional(w, report.uvBoundMm);
    w.Add(",\"validations\":{\"pointer_reports\":");
    if (report.pointerReports)
    {
        w.Number(*report.pointerReports);
    }
    else
    {
        w.Add("null");
    }
    w.Add(",\"level2_reports\":");
    if (report.deepReports)
    {
        w.Number(*report.deepReports);
    }
    else
    {
        w.Add("null");
    }
    w.Add("}");
    w.Add(",\"tolerances\":[");
    for (std::size_t i = 0; i < report.tolerances.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& t = report.tolerances[i];
        w.Add("{\"stage\":");
        w.String(t.stage);
        w.Add(",\"kind\":");
        w.String(t.kind);
        w.Add(",\"source_id\":");
        w.String(t.sourceId);
        w.Add(",\"value_mm\":");
        Number(w, t.valueMm);
        w.Add("}");
    }
    w.Add("],\"edge_mappings\":[");
    for (std::size_t i = 0; i < report.edges.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& e = report.edges[i];
        w.Add("{\"source_edge\":");
        w.String(e.edgeId);
        w.Add(",\"source_curve\":");
        w.String(e.curveId);
        w.Add(",\"source_start\":");
        w.String(e.startVertexId);
        w.Add(",\"source_end\":");
        w.String(e.endVertexId);
        w.Add(",\"native_min_vertex\":");
        w.String(e.nativeMinVertexId);
        w.Add(",\"native_max_vertex\":");
        w.String(e.nativeMaxVertexId);
        w.Add(",\"source_same_sense\":");
        w.Bool(e.sourceSameSense);
        w.Add(",\"source_start_coefficient\":");
        FractionJson(w, e.sourceStartCoefficient);
        w.Add(",\"source_end_coefficient\":");
        FractionJson(w, e.sourceEndCoefficient);
        w.Add(",\"source_direction_norm_squared\":");
        FractionJson(w, e.sourceDirectionNormSquared);
        w.Add(",\"native_interval\":[0,1],\"source_parameter\":\"coefficient/sqrt(direction_norm_squared)\"}");
    }
    w.Add("],\"loop_mappings\":[");
    for (std::size_t i = 0; i < report.loops.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& loop = report.loops[i];
        w.Add("{\"source_face\":");
        w.String(loop.faceId);
        w.Add(",\"source_bound\":");
        w.String(loop.boundId);
        w.Add(",\"source_loop\":");
        w.String(loop.loopId);
        w.Add(",\"outer\":");
        w.Bool(loop.outer);
        w.Add(",\"source_orientation\":");
        w.Bool(loop.sourceOrientation);
        w.Add(",\"uses\":[");
        for (std::size_t j = 0; j < loop.uses.size(); ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            const auto& use = loop.uses[j];
            w.Add("{\"source_oriented_edge\":");
            w.String(use.orientedEdgeId);
            w.Add(",\"source_edge\":");
            w.String(use.edgeId);
            w.Add(",\"start_vertex\":");
            w.String(use.startVertexId);
            w.Add(",\"end_vertex\":");
            w.String(use.endVertexId);
            w.Add(",\"forward_in_native_curve\":");
            w.Bool(use.forwardInNativeCurve);
            w.Add(",\"uv_owner_edgeuse\":\"SAME\",\"uv_owner_on_selected_side\":");
            w.Bool(use.uvOwnerOnSelectedSide);
            w.Add(",\"uv_start_mm\":[");
            Number(w, use.uvStartMm[0]);
            w.Add(",");
            Number(w, use.uvStartMm[1]);
            w.Add("],\"uv_end_mm\":[");
            Number(w, use.uvEndMm[0]);
            w.Add(",");
            Number(w, use.uvEndMm[1]);
            w.Add("]}");
        }
        w.Add("]}");
    }
    w.Add("],\"diagnostics\":[");
    for (std::size_t i = 0; i < report.diagnostics.size(); ++i)
    {
        if (i)
        {
            w.Add(",");
        }
        const auto& d = report.diagnostics[i];
        w.Add("{\"key\":");
        w.String(d.source.key);
        w.Add(",\"stage\":");
        w.String(d.source.stage);
        w.Add(",\"classification\":");
        w.String(d.classification);
        w.Add(",\"message\":");
        w.String(d.source.message);
        w.Add(",\"span\":");
        w.Range(d.source.primary);
        w.Add(",\"location\":");
        w.Position(source, d.source.primary.begin);
        w.Add(",\"record_id\":");
        w.String(d.source.recordId);
        w.Add(",\"component\":");
        w.String(d.source.component);
        w.Add(",\"target_id\":");
        w.String(d.source.targetId);
        w.Add(",\"related_spans\":[");
        for (std::size_t j = 0; j < d.source.related.size(); ++j)
        {
            if (j)
            {
                w.Add(",");
            }
            w.Range(d.source.related[j]);
        }
        w.Add("],\"field\":");
        w.String(d.field);
        w.Add(",\"parameter_path\":");
        w.Path(d.source.parameterPath);
        w.Add(",\"declared_in\":");
        w.String(d.declaredIn);
        w.Add(",\"declared_type\":");
        w.String(d.declaredType);
        w.Add(",\"expected\":");
        w.String(d.expected);
        w.Add(",\"actual\":");
        w.String(d.actual);
        w.Add(",\"responsibility\":");
        w.String(d.responsibility);
        w.Add("}");
    }
    w.Add("]}\n");
}
} // namespace
[[nodiscard]] Result<void> Serialize(
    Status status,
    const Report& report,
    const Quality& quality,
    const step_source::InspectOutcome& source,
    std::size_t limit,
    std::string& output
)
{
    const bool complete = Writer::Publish(
        limit,
        output,
        [&](Writer& writer)
        {
            WriteReportJson(writer, status, report, quality, source);
        }
    );
    if (!complete)
    {
        return tl::unexpected(
            Failure{ Status::ResourceLimit, "ImportReportByteLimit", "Mandatory import report cannot be published within the byte allowance", {} }
        );
    }
    return {};
}
Result<void> SerializeAssemblyFailure(
    Status status,
    const Report& report,
    const Quality& quality,
    const step_source::InspectOutcome& source,
    std::size_t limit,
    std::string& output
)
{
    if (IsAcceptedReportStatus(status))
    {
        output.clear();
        return tl::unexpected(Failure{ Status::BuildFailure, "AssemblyEvidenceMissing", "Accepted assembly requires its complete evidence", {} });
    }
    if (!Writer::Publish(
            limit,
            output,
            [&](Writer& writer)
            {
                WriteReportJson(writer, status, report, quality, source, false, nullptr, true);
            }
        ))
    {
        return tl::unexpected(
            Failure{ Status::ResourceLimit, "ImportReportByteLimit", "Mandatory assembly failure report exceeds its byte allowance", {} }
        );
    }
    return {};
}
Result<void> SerializeAssembly(
    Status status,
    const Report& report,
    const Quality& quality,
    const step_source::InspectOutcome& source,
    const AssemblyEvidence& assembly,
    std::size_t limit,
    std::string& output
)
{
    if (!report.generalBrep || !source.source || !report.source || report.sourceSha256 != source.source->Sha256() ||
        report.source->Sha256() != source.source->Sha256() || !assembly.instances.placements.source ||
        source.source->Sha256() != assembly.instances.placements.source->SourceFields().Source().Sha256())
    {
        output.clear();
        return tl::unexpected(
            Failure{ Status::BuildFailure,
                     "AssemblyReportSourceMismatch",
                     "Assembly evidence must bind the same captured source",
                     {},
                     "AssemblyReport" }
        );
    }
    if (IsAcceptedReportStatus(status))
    {
        bool complete = report.definitions.size() == assembly.definitions.size() && !assembly.definitions.empty();
        if (complete)
        {
            for (std::size_t i = 0; i < report.definitions.size(); ++i)
            {
                complete = complete && report.definitions[i].checksPassed && report.definitions[i].checks &&
                           report.definitions[i].sourceDefinitionRecord == assembly.definitions[i].definition;
            }
        }
        if (!complete)
        {
            output.clear();
            return tl::unexpected(
                Failure{ Status::BuildFailure,
                         "AssemblyReportDefinitionMismatch",
                         "Accepted assembly report requires complete matching definition observations",
                         {},
                         "AssemblyReport" }
            );
        }
    }
    if (!Writer::Publish(
            limit,
            output,
            [&](Writer& writer)
            {
                WriteReportJson(writer, status, report, quality, source, false, &assembly);
            }
        ))
    {
        return tl::unexpected(Failure{ Status::ResourceLimit, "ImportReportByteLimit", "Mandatory assembly report exceeds its byte allowance", {} });
    }
    return {};
}
} // namespace step_import::detail
