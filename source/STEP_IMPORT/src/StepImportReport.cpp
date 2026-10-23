// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportReport.h"

#include "StepSemanticReport.h"
#include "StepSourceReport.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

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
void StatisticsJson(Writer& w, const Statistics& n)
{
    w.Add("{\"faces\":");
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
void WriteReportJson(
    Writer& w,
    Status status,
    const Report& report,
    const Quality& quality,
    const step_source::InspectOutcome& source,
    bool nestedBody = false
)
{
    if (report.generalBrep)
    {
        // Separate scope: do not serialize unexecuted planar proofs as
        // qualification for analytic/NURBS models or complete AP214 rules.
        w.Add(
            nestedBody ?
                (report.sourceEvidenceScope == "RepairParent" ?
                     "{\"format_version\":\"step-repair-original-attempt.v1\",\"scope\":\"WithinRepairRequest\",\"status\":" :
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
        if (report.cylindricalBandRepairRequest || report.cylindricalBandRepairs)
        {
            w.Add(
                ",\"cylindrical_band_repair\":{\"configuration\":\"ExactOrientedCylindricalBand-0.1\",\"source_region_proof\":\"ExactCoaxialEqualRadiusOrientedWholeCircles\",\"effective_outer_markers\":\"None\",\"geometry_displacement_allowance_mm\":0,\"tolerance_growth_allowance_mm\":0"
            );
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
        w.String(report.cylindricalBandRepairs ? "ExactOrientedCylindricalBand-0.1" : report.repairs ? "FaceOrientationZeroGeometry-0.1" : "None");
        w.Add(",\"quality_id\":");
        w.String(quality.id);
        if (report.numericalIntegration)
        {
            const auto& integration = *report.numericalIntegration;
            w.Add(
                ",\"numerical_integration\":{\"method\":\"SMLibFusedFaceProperties\",\"control_scope\":\"DimensionlessNumericalStoppingOnly\",\"requested_relative_accuracy\":"
            );
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
        w.Add(
            ",\"mass_integration_origin\":\"AxisAlignedBoundsMidpoint\",\"absolute_area_volume_error_bound\":\"NotEstablished\",\"global_definition_error_bound\":\"NotEstablished\",\"global_curve_surface_error_bound\":\"NotEstablished\",\"final_trim_measurement\":\"PairedParameter8SubintervalsPerUnionKnotSpan\"}"
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
        if (report.productDocument)
        {
            w.Add(",\"product_document\":{\"complete\":");
            w.Bool(IsAcceptedReportStatus(status));
            w.Add(",\"selected_definition_count\":");
            w.Number(
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
            if (d.classification == "NativeFaceLoopOrientationFailure")
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
            StatisticsJson(w, *report.native);
        }
        else
        {
            w.Add("null");
        }
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
        w.Add("],\"representation_normalizations\":[");
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
            w.Add(",\"method\":\"NativePlanarCurveIntersectionContainment\",\"source_rewritten\":false}");
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
Result<void> Serialize(
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
} // namespace step_import::detail
