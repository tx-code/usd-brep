// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPlan.h"
#include "StepSourceReport.h"

#include <charconv>
#include <cmath>
#include <limits>

namespace step_import::detail
{
namespace
{
using step_source::report::Writer;
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
} // namespace
void Serialize(const Outcome& outcome, const Quality& quality, const step_source::InspectOutcome& source, std::size_t limit, std::string& output)
{
    const auto& report = outcome.report;
    const bool complete = Writer::Publish(
        limit,
        output,
        [&](Writer& w)
        {
            w.Add("{\"format_version\":\"step-native-import.v2\",\"scope\":\"PlanarNativeImport\",\"status\":");
            w.String(StatusName(outcome.status));
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
            if (outcome.status == Status::Accepted)
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
    );
    if (!complete)
    {
        throw Failure{ Status::ResourceLimit, "ImportReportByteLimit", "Mandatory import report cannot be published within the byte allowance", {} };
    }
}
} // namespace step_import::detail
