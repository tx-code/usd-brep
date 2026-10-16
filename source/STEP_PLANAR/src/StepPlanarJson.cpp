// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepPlanar.h"

#include <stdexcept>

namespace step_planar
{
namespace
{
class Json
{
public:

    explicit Json(std::size_t maximum) : maximum(maximum)
    {
    }
    std::string data;
    void Add(std::string_view text)
    {
        if (text.size() > maximum - data.size())
        {
            throw std::length_error("ReportByteLimit");
        }
        data.append(text);
    }
    void String(std::string_view text)
    {
        Add("\"");
        const char hex[] = "0123456789abcdef";
        for (const unsigned char c : text)
        {
            if (c == '"')
            {
                Add("\\\"");
            }
            else if (c == '\\')
            {
                Add("\\\\");
            }
            else if (c < 32 || c >= 127)
            {
                const char escape[] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 15] };
                Add(std::string_view(escape, 6));
            }
            else
            {
                const char value = static_cast<char>(c);
                Add(std::string_view(&value, 1));
            }
        }
        Add("\"");
    }
    void Number(std::size_t n)
    {
        Add(std::to_string(n));
    }
    void Bool(bool value)
    {
        Add(value ? "true" : "false");
    }
    void Fraction(Rational value)
    {
        Add("{\"kind\":\"rational\",\"num\":");
        String(std::to_string(value.numerator));
        Add(",\"den\":");
        String(std::to_string(value.denominator));
        Add("}");
    }
    void Vec(Vector values)
    {
        Add("[");
        for (std::size_t i = 0; i < 3; ++i)
        {
            if (i)
            {
                Add(",");
            }
            Fraction(values[i]);
        }
        Add("]");
    }
    void Span(step_source::Span span)
    {
        Add("[");
        Number(span.begin);
        Add(",");
        Number(span.end);
        Add("]");
    }
    void Id(const Evidence& evidence, std::size_t index)
    {
        String(evidence.source->Source().Records().at(index).id);
    }
    void Ids(const Evidence& evidence, const std::vector<std::size_t>& indices)
    {
        Add("[");
        bool comma = false;
        for (const auto i : indices)
        {
            if (comma)
            {
                Add(",");
            }
            comma = true;
            Id(evidence, i);
        }
        Add("]");
    }
    void Fractions(const std::map<std::string, Rational>& values)
    {
        Add("{");
        bool comma = false;
        for (const auto& item : values)
        {
            if (comma)
            {
                Add(",");
            }
            comma = true;
            String(item.first);
            Add(":");
            Fraction(item.second);
        }
        Add("}");
    }

private:

    std::size_t maximum;
};
} // namespace
bool InspectJson(const step_source::InspectOutcome& source, const step_semantics::Outcome& semantics, const Outcome& planar, std::string& output)
{
    output.clear();
    try
    {
        std::string upstream;
        if (!step_semantics::InspectJson(source, semantics, upstream))
        {
            return false;
        }
        Json json(source.limits.reportBytes);
        json.Add("{\"format_version\":\"step-planar.v1\",\"scope\":\"PlanarSourceRulesAndRegions\",\"status\":");
        json.String(StatusName(planar.status));
        json.Add(",\"source_semantics\":");
        json.Add(upstream);
        json.Add(",\"quality\":{\"id\":");
        json.String(planar.quality.id);
        json.Add(",\"consistency_mm\":");
        json.Fraction(planar.quality.consistencyMm);
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
                json.Id(evidence, *rule.owner);
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
            json.Ids(evidence, rule.witnesses);
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
                json.Ids(evidence, item.second);
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
            json.Id(evidence, metric.owner);
            json.Add(",\"witness_ids\":");
            json.Ids(evidence, metric.witnesses);
            json.Add(",\"exact\":");
            json.Fractions(metric.exact);
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
            json.Id(evidence, edge.edge);
            json.Add(",\"line_id\":");
            json.Id(evidence, edge.line);
            json.Add(",\"start_id\":");
            json.Id(evidence, edge.start);
            json.Add(",\"end_id\":");
            json.Id(evidence, edge.end);
            json.Add(",\"same_sense\":");
            json.Bool(edge.sameSense);
            json.Add(",\"t_start_coefficient\":");
            json.Fraction(edge.tStartCoefficient);
            json.Add(",\"t_end_coefficient\":");
            json.Fraction(edge.tEndCoefficient);
            json.Add(",\"t_divisor_squared\":");
            json.Fraction(edge.directionNormSquared);
            json.Add(",\"length_squared_mm\":");
            json.Fraction(edge.lengthSquaredMm);
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
            json.Id(evidence, face.face);
            json.Add(",\"plane_id\":");
            json.Id(evidence, face.plane);
            json.Add(",\"placement_id\":");
            json.Id(evidence, face.placement);
            json.Add(",\"same_sense\":");
            json.Bool(face.sameSense);
            json.Add(",\"source_length_scale_mm\":");
            json.Fraction(face.sourceLengthScaleMm);
            json.Add(",\"origin_mm\":");
            json.Vec(face.originMm);
            json.Add(",\"basis_x\":");
            json.Vec(face.x);
            json.Add(",\"basis_y\":");
            json.Vec(face.y);
            json.Add(",\"basis_normal\":");
            json.Vec(face.normal);
            json.Add(",\"x_norm_squared\":");
            json.Fraction(face.xNormSquared);
            json.Add(",\"y_norm_squared\":");
            json.Fraction(face.yNormSquared);
            json.Add(",\"metric_squared\":");
            json.Fraction(face.metricSquared);
            json.Add(",\"projected_region_area_squared_mm\":");
            json.Fraction(face.areaSquaredMm);
            json.Add(",\"projected_region_centroid_mm\":");
            json.Vec(face.centroidMm);
            json.Add(",\"projected_region_bounds_mm\":[");
            json.Vec(face.minimumMm);
            json.Add(",");
            json.Vec(face.maximumMm);
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
                json.Id(evidence, loop.bound);
                json.Add(",\"loop_id\":");
                json.Id(evidence, loop.loop);
                json.Add(",\"outer\":");
                json.Bool(loop.outer);
                json.Add(",\"orientation\":");
                json.Bool(loop.orientation);
                json.Add(",\"oriented_edge_ids\":");
                json.Ids(evidence, loop.orientedEdges);
                json.Add(",\"edge_ids\":");
                json.Ids(evidence, loop.edges);
                json.Add(",\"vertex_ids\":");
                json.Ids(evidence, loop.vertices);
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
                json.Fraction(loop.signedAreaCoefficient);
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
                    json.Fraction(point[0]);
                    json.Add(",");
                    json.Fraction(point[1]);
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
            json.Add(",\"parameter_path\":[");
            for (std::size_t i = 0; i < d.source.parameterPath.size(); ++i)
            {
                if (i)
                {
                    json.Add(",");
                }
                json.Number(d.source.parameterPath[i]);
            }
            json.Add("],\"span\":");
            json.Span(d.source.primary);
            json.Add(",\"location\":");
            if (source.source)
            {
                const auto p = source.source->Locate(d.source.primary.begin);
                json.Add("{\"line\":");
                json.Number(p.line);
                json.Add(",\"column\":");
                json.Number(p.column);
                json.Add("}");
            }
            else
            {
                json.Add("null");
            }
            json.Add(",\"responsibility\":");
            json.String(d.responsibility);
            json.Add("}");
        }
        json.Add("]}\n");
        output = std::move(json.data);
        return true;
    }
    catch (const std::length_error&)
    {
        output.clear();
        return false;
    }
}
} // namespace step_planar
