// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepPlanar.h"
#include "StepSemanticReport.h"

namespace step_planar
{
namespace
{
using step_source::report::Writer;

void Fraction(Writer& json, Rational value)
{
    json.Add("{\"kind\":\"rational\",\"num\":");
    json.String(std::to_string(value.Numerator()));
    json.Add(",\"den\":");
    json.String(std::to_string(value.Denominator()));
    json.Add("}");
}
void Vec(Writer& json, Vector values)
{
    json.Add("[");
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (i)
        {
            json.Add(",");
        }
        Fraction(json, values[i]);
    }
    json.Add("]");
}
void Id(Writer& json, const Evidence& evidence, std::size_t index)
{
    json.String(evidence.source->Source().Records().at(index).id);
}
void Ids(Writer& json, const Evidence& evidence, const std::vector<std::size_t>& indices)
{
    json.Add("[");
    bool comma = false;
    for (const auto i : indices)
    {
        if (comma)
        {
            json.Add(",");
        }
        comma = true;
        Id(json, evidence, i);
    }
    json.Add("]");
}
void Fractions(Writer& json, const std::map<std::string, Rational>& values)
{
    json.Add("{");
    bool comma = false;
    for (const auto& item : values)
    {
        if (comma)
        {
            json.Add(",");
        }
        comma = true;
        json.String(item.first);
        json.Add(":");
        Fraction(json, item.second);
    }
    json.Add("}");
}
} // namespace
bool InspectJson(const step_source::InspectOutcome& source, const step_semantics::Outcome& semantics, const Outcome& planar, std::string& output)
{
    return Writer::Publish(
        source.limits.reportBytes,
        output,
        [&](Writer& json)
        {
            json.Add("{\"format_version\":\"step-planar.v1\",\"scope\":\"PlanarSourceRulesAndRegions\",\"status\":");
            json.String(StatusName(planar.status));
            json.Add(",\"source_semantics\":");
            step_semantics::report::WriteJson(json, source, semantics);
            json.Add(",\"quality\":{\"id\":");
            json.String(planar.quality.id);
            json.Add(",\"consistency_mm\":");
            Fraction(json, planar.quality.consistencyMm);
            json.Add(",\"production_default\":false,\"repair\":\"None\"},\"limits\":{\"work\":");
            json.Number(planar.limits.work);
            json.Add("},\"work\":");
            json.Number(planar.work);
            json.Add(",\"checks\":{\"local_rules\":");
            json.String(planar.localRules);
            json.Add(",\"global_rules_47\":");
            json.String(planar.globalRules);
            json.Add(",\"planar_regions\":");
            json.String(planar.geometry);
            json.Add(
                ",\"conversion_units\":\"NotRun\",\"native_build\":\"NotRun\",\"full_import\":\"NotRun\","
                "\"target_uv\":\"NotRun\",\"tolerance_setting\":\"NotRun\",\"shell_self_intersection\":\"NotRun\",\"material_volume\":\"NotRun\"}"
            );
            json.Add(",\"deferred_reason\":");
            if (planar.status == Status::Deferred)
            {
                json.String("ConversionLengthInterpretationPendingTicket09");
            }
            else
            {
                json.Add("null");
            }
            json.Add(",\"model\":null,\"qualified_source_evidence\":");
            json.Bool(planar.status == Status::Checked);
            json.Add(
                ",\"arithmetic\":\"Reduced signed-63-bit rational; all decisions exact, capacity exhaustion Indeterminate; radicals retained algebraically\""
            );
            json.Add(",\"rules\":[");
            bool comma = false;
            const auto& evidence = planar.evidence;
            for (const auto& rule : evidence.rules)
            {
                if (comma)
                {
                    json.Add(",");
                }
                comma = true;
                json.Add("{\"name\":");
                json.String(rule.name);
                json.Add(",\"kind\":");
                json.String(rule.kind);
                json.Add(",\"schema_line\":");
                json.Number(rule.schemaLine);
                json.Add(",\"applicability\":");
                json.String(rule.applicability);
                json.Add(",\"population_scope\":");
                json.String(rule.owner ? "OwnerInstance" : "FullFile");
                json.Add(",\"owner_id\":");
                if (rule.owner)
                {
                    Id(json, evidence, *rule.owner);
                }
                else
                {
                    json.Add("null");
                }
                json.Add(",\"evaluation\":");
                json.String(rule.evaluation);
                json.Add(",\"logical_value\":");
                if (rule.value)
                {
                    json.String(LogicalName(*rule.value));
                }
                else
                {
                    json.Add("null");
                }
                json.Add(",\"proof\":");
                json.String(rule.proof);
                json.Add(",\"witness_ids\":");
                Ids(json, evidence, rule.witnesses);
                json.Add(",\"populations\":{");
                bool pair = false;
                for (const auto& item : rule.populations)
                {
                    if (pair)
                    {
                        json.Add(",");
                    }
                    pair = true;
                    json.String(item.first);
                    json.Add(":");
                    Ids(json, evidence, item.second);
                }
                json.Add("},\"facts\":{");
                pair = false;
                for (const auto& item : rule.facts)
                {
                    if (pair)
                    {
                        json.Add(",");
                    }
                    pair = true;
                    json.String(item.first);
                    json.Add(":");
                    json.String(item.second);
                }
                json.Add("}}");
            }
            json.Add("],\"metrics\":[");
            comma = false;
            for (const auto& metric : evidence.metrics)
            {
                if (comma)
                {
                    json.Add(",");
                }
                comma = true;
                json.Add("{\"kind\":");
                json.String(metric.kind);
                json.Add(",\"owner_id\":");
                Id(json, evidence, metric.owner);
                json.Add(",\"witness_ids\":");
                Ids(json, evidence, metric.witnesses);
                json.Add(",\"exact\":");
                Fractions(json, metric.exact);
                json.Add("}");
            }
            json.Add("],\"edges\":[");
            comma = false;
            for (const auto& edge : evidence.edges)
            {
                if (comma)
                {
                    json.Add(",");
                }
                comma = true;
                json.Add("{\"id\":");
                Id(json, evidence, edge.edge);
                json.Add(",\"line_id\":");
                Id(json, evidence, edge.line);
                json.Add(",\"start_id\":");
                Id(json, evidence, edge.start);
                json.Add(",\"end_id\":");
                Id(json, evidence, edge.end);
                json.Add(",\"same_sense\":");
                json.Bool(edge.sameSense);
                json.Add(",\"t_start_coefficient\":");
                Fraction(json, edge.tStartCoefficient);
                json.Add(",\"t_end_coefficient\":");
                Fraction(json, edge.tEndCoefficient);
                json.Add(",\"t_divisor_squared\":");
                Fraction(json, edge.directionNormSquared);
                json.Add(",\"length_squared_mm\":");
                Fraction(json, edge.lengthSquaredMm);
                json.Add("}");
            }
            json.Add("],\"faces\":[");
            comma = false;
            for (const auto& face : evidence.faces)
            {
                if (comma)
                {
                    json.Add(",");
                }
                comma = true;
                json.Add("{\"id\":");
                Id(json, evidence, face.face);
                json.Add(",\"plane_id\":");
                Id(json, evidence, face.plane);
                json.Add(",\"placement_id\":");
                Id(json, evidence, face.placement);
                json.Add(",\"same_sense\":");
                json.Bool(face.sameSense);
                json.Add(",\"source_length_scale_mm\":");
                Fraction(json, face.sourceLengthScaleMm);
                json.Add(",\"origin_mm\":");
                Vec(json, face.originMm);
                json.Add(",\"basis_x\":");
                Vec(json, face.x);
                json.Add(",\"basis_y\":");
                Vec(json, face.y);
                json.Add(",\"basis_normal\":");
                Vec(json, face.normal);
                json.Add(",\"x_norm_squared\":");
                Fraction(json, face.xNormSquared);
                json.Add(",\"y_norm_squared\":");
                Fraction(json, face.yNormSquared);
                json.Add(",\"metric_squared\":");
                Fraction(json, face.metricSquared);
                json.Add(",\"projected_region_area_squared_mm\":");
                Fraction(json, face.areaSquaredMm);
                json.Add(",\"projected_region_centroid_mm\":");
                Vec(json, face.centroidMm);
                json.Add(",\"projected_region_bounds_mm\":[");
                Vec(json, face.minimumMm);
                json.Add(",");
                Vec(json, face.maximumMm);
                json.Add("]");
                json.Add(",\"source_uv_formula\":\"u=projected[0]*sqrt(x_norm_squared)/source_length_scale_mm; v analogously with y\"");
                json.Add(",\"loops\":[");
                bool loopComma = false;
                for (const auto& loop : face.loops)
                {
                    if (loopComma)
                    {
                        json.Add(",");
                    }
                    loopComma = true;
                    json.Add("{\"bound_id\":");
                    Id(json, evidence, loop.bound);
                    json.Add(",\"loop_id\":");
                    Id(json, evidence, loop.loop);
                    json.Add(",\"outer\":");
                    json.Bool(loop.outer);
                    json.Add(",\"orientation\":");
                    json.Bool(loop.orientation);
                    json.Add(",\"oriented_edge_ids\":");
                    Ids(json, evidence, loop.orientedEdges);
                    json.Add(",\"edge_ids\":");
                    Ids(json, evidence, loop.edges);
                    json.Add(",\"vertex_ids\":");
                    Ids(json, evidence, loop.vertices);
                    json.Add(",\"edge_orientations\":[");
                    for (std::size_t i = 0; i < loop.edgeOrientations.size(); ++i)
                    {
                        if (i)
                        {
                            json.Add(",");
                        }
                        json.Bool(loop.edgeOrientations[i]);
                    }
                    json.Add("],\"signed_area_coefficient\":");
                    Fraction(json, loop.signedAreaCoefficient);
                    json.Add(",\"projected\":[");
                    bool pointComma = false;
                    for (const auto point : loop.projected)
                    {
                        if (pointComma)
                        {
                            json.Add(",");
                        }
                        pointComma = true;
                        json.Add("[");
                        Fraction(json, point[0]);
                        json.Add(",");
                        Fraction(json, point[1]);
                        json.Add("]");
                    }
                    json.Add("]}");
                }
                json.Add("]}");
            }
            json.Add("],\"diagnostics\":[");
            comma = false;
            for (const auto& d : planar.diagnostics)
            {
                if (comma)
                {
                    json.Add(",");
                }
                comma = true;
                json.Add("{\"key\":");
                json.String(d.source.key);
                json.Add(",\"stage\":");
                json.String(d.source.stage);
                json.Add(",\"classification\":");
                json.String(d.classification);
                json.Add(",\"record_id\":");
                json.String(d.source.recordId);
                json.Add(",\"component\":");
                json.String(d.source.component);
                json.Add(",\"field\":");
                json.String(d.field);
                json.Add(",\"declared_in\":");
                json.String(d.declaredIn);
                json.Add(",\"declared_type\":");
                json.String(d.declaredType);
                json.Add(",\"parameter_path\":");
                json.Path(d.source.parameterPath);
                json.Add(",\"span\":");
                json.Range(d.source.primary);
                json.Add(",\"location\":");
                json.Position(source, d.source.primary.begin);
                json.Add(",\"responsibility\":");
                json.String(d.responsibility);
                json.Add("}");
            }
            json.Add("]}\n");
        }
    );
}
} // namespace step_planar
