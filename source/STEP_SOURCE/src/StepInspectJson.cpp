// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSource.h"

#include <algorithm>
#include <stdexcept>

namespace step_source
{
namespace
{
class Writer
{
public:

    explicit Writer(std::size_t maximum) : m_maximum(maximum)
    {
    }
    std::string text;
    void Add(std::string_view value)
    {
        if (value.size() > m_maximum - text.size())
        {
            throw std::length_error("ReportByteLimit");
        }
        text.append(value);
    }
    void Number(std::size_t value)
    {
        Add(std::to_string(value));
    }
    void String(std::string_view value)
    {
        Add("\"");
        const char hex[] = "0123456789abcdef";
        for (unsigned char ch : value)
        {
            if (ch == '"')
            {
                Add("\\\"");
            }
            else if (ch == '\\')
            {
                Add("\\\\");
            }
            else if (ch < 32 || ch >= 127)
            {
                const char escaped[] = { '\\', 'u', '0', '0', hex[ch >> 4], hex[ch & 15] };
                Add(std::string_view(escaped, 6));
            }
            else
            {
                Add(std::string_view(reinterpret_cast<const char*>(&ch), 1));
            }
        }
        Add("\"");
    }
    void Range(Span span)
    {
        Add("[");
        Number(span.begin);
        Add(",");
        Number(span.end);
        Add("]");
    }
    void Path(const std::vector<std::size_t>& path)
    {
        Add("[");
        for (std::size_t i = 0; i < path.size(); ++i)
        {
            if (i)
            {
                Add(",");
            }
            Number(path[i]);
        }
        Add("]");
    }
    void Position(Location location)
    {
        Add("{\"line\":");
        Number(location.line);
        Add(",\"column\":");
        Number(location.column);
        Add("}");
    }

private:

    std::size_t m_maximum;
};

Location LocateCaptured(const std::string& bytes, std::size_t offset)
{
    Location position;
    for (std::size_t i = 0; i < std::min(offset, bytes.size()); ++i)
    {
        if (bytes[i] == '\r' || bytes[i] == '\n')
        {
            if (bytes[i] == '\r' && i + 1 < offset && bytes[i + 1] == '\n')
            {
                ++i;
            }
            ++position.line;
            position.column = 1;
        }
        else
        {
            ++position.column;
        }
    }
    return position;
}

void ValueJson(Writer& out, const Value& value, const SourceSnapshot& source)
{
    out.Add("{\"kind\":");
    out.String(ValueKindName(value.kind));
    out.Add(",\"span\":");
    out.Range(value.span);
    out.Add(",\"raw\":");
    out.String(source.Raw(value.span));
    out.Add(",\"text\":");
    out.String(value.text);
    out.Add(",\"children\":[");
    for (std::size_t i = 0; i < value.children.size(); ++i)
    {
        if (i)
        {
            out.Add(",");
        }
        ValueJson(out, value.children[i], source);
    }
    out.Add("]}");
}

void ComponentJson(Writer& out, const Component& component, const SourceSnapshot& source)
{
    out.Add("{\"name\":");
    out.String(component.name);
    out.Add(",\"span\":");
    out.Range(component.span);
    out.Add(",\"raw\":");
    out.String(source.Raw(component.span));
    out.Add(",\"parameters\":[");
    for (std::size_t i = 0; i < component.parameters.size(); ++i)
    {
        if (i)
        {
            out.Add(",");
        }
        ValueJson(out, component.parameters[i], source);
    }
    out.Add("]}");
}
} // namespace

bool InspectJson(const InspectOutcome& result, std::string& output)
{
    output.clear();
    try
    {
        Writer out(result.limits.reportBytes);
        out.Add("{\"format_version\":\"step-source.v1\",\"scope\":\"SyntaxIndexReferences\",\"status\":");
        out.String(StatusName(result.status));
        out.Add(",\"source_complete\":");
        out.Add(result.source ? "true" : "false");
        out.Add(",\"schema_validation\":\"NotRun\",\"native_build\":\"NotRun\",\"input\":{\"capture_complete\":");
        out.Add(result.captured ? "true" : "false");
        out.Add(",\"byte_length\":");
        if (result.captured)
        {
            out.Number(result.captured->size());
        }
        else
        {
            out.Add("null");
        }
        out.Add(",\"sha256\":");
        if (result.captured)
        {
            out.String(result.sha256);
        }
        else
        {
            out.Add("null");
        }
        out.Add(",\"raw\":");
        if (result.source)
        {
            out.String(result.source->Bytes());
        }
        else
        {
            out.Add("null");
        }
        out.Add("},\"limits\":{");
        const auto& limits = result.limits;
        const std::pair<const char*, std::size_t> fields[] = {
            { "input_bytes", limits.inputBytes },   { "records", limits.records },          { "values", limits.values },
            { "references", limits.references },    { "nesting", limits.nesting },          { "string_bytes", limits.stringBytes },
            { "record_bytes", limits.recordBytes }, { "report_bytes", limits.reportBytes }, { "id_digits", limits.idDigits }
        };
        bool first = true;
        for (const auto& field : fields)
        {
            if (!first)
            {
                out.Add(",");
            }
            first = false;
            out.String(field.first);
            out.Add(":");
            out.Number(field.second);
        }
        const auto& stats = result.statistics;
        out.Add("},\"statistics\":{\"data_records\":");
        out.Number(stats.records);
        out.Add(",\"values_including_header\":");
        out.Number(stats.values);
        out.Add(",\"references\":");
        out.Number(stats.references);
        out.Add(",\"value_depth\":");
        out.Number(stats.nesting);
        out.Add(",\"max_string_bytes\":");
        out.Number(stats.maxStringBytes);
        out.Add(",\"max_record_bytes\":");
        out.Number(stats.maxRecordBytes);
        out.Add(",\"complete\":");
        out.Add(result.source ? "true" : "false");
        out.Add("},\"header\":");
        if (!result.source)
        {
            out.Add("null,\"records\":null,\"references\":null");
        }
        else
        {
            const auto& source = *result.source;
            out.Add("[");
            for (std::size_t i = 0; i < source.Header().size(); ++i)
            {
                if (i)
                {
                    out.Add(",");
                }
                ComponentJson(out, source.Header()[i], source);
            }
            out.Add("],\"records\":[");
            for (std::size_t i = 0; i < source.Records().size(); ++i)
            {
                const auto& record = source.Records()[i];
                if (i)
                {
                    out.Add(",");
                }
                out.Add("{\"record_occurrence\":");
                out.Number(i + 1);
                out.Add(",\"id\":");
                out.String(record.id);
                out.Add(",\"id_span\":");
                out.Range(record.idSpan);
                out.Add(",\"span\":");
                out.Range(record.span);
                out.Add(",\"location\":");
                out.Position(source.Locate(record.span.begin));
                out.Add(",\"raw\":");
                out.String(source.Raw(record.span));
                out.Add(",\"complex\":");
                out.Add(record.complex ? "true" : "false");
                out.Add(",\"components\":[");
                for (std::size_t j = 0; j < record.components.size(); ++j)
                {
                    if (j)
                    {
                        out.Add(",");
                    }
                    ComponentJson(out, record.components[j], source);
                }
                out.Add("]}");
            }
            out.Add("],\"references\":[");
            for (std::size_t i = 0; i < source.References().size(); ++i)
            {
                const auto& reference = source.References()[i];
                if (i)
                {
                    out.Add(",");
                }
                out.Add("{\"target_id\":");
                out.String(reference.targetId);
                out.Add(",\"span\":");
                out.Range(reference.span);
                out.Add(",\"record_index\":");
                out.Number(reference.recordIndex);
                out.Add(",\"component_index\":");
                out.Number(reference.componentIndex);
                out.Add(",\"parameter_path\":");
                out.Path(reference.parameterPath);
                out.Add(",\"target_record_index\":");
                out.Number(reference.targetRecordIndex);
                out.Add("}");
            }
            out.Add("]");
        }
        out.Add(",\"diagnostics\":[");
        for (std::size_t i = 0; i < result.diagnostics.size(); ++i)
        {
            const auto& diagnostic = result.diagnostics[i];
            if (i)
            {
                out.Add(",");
            }
            out.Add("{\"key\":");
            out.String(diagnostic.key);
            out.Add(",\"stage\":");
            out.String(diagnostic.stage);
            out.Add(",\"message\":");
            out.String(diagnostic.message);
            out.Add(",\"span\":");
            out.Range(diagnostic.primary);
            out.Add(",\"location\":");
            if (result.captured)
            {
                out.Position(LocateCaptured(*result.captured, diagnostic.primary.begin));
            }
            else
            {
                out.Add("null");
            }
            out.Add(",\"record_id\":");
            out.String(diagnostic.recordId);
            out.Add(",\"component\":");
            out.String(diagnostic.component);
            out.Add(",\"parameter_path\":");
            out.Path(diagnostic.parameterPath);
            out.Add(",\"target_id\":");
            out.String(diagnostic.targetId);
            out.Add(",\"related\":[");
            for (std::size_t j = 0; j < diagnostic.related.size(); ++j)
            {
                if (j)
                {
                    out.Add(",");
                }
                out.Add("{\"span\":");
                out.Range(diagnostic.related[j]);
                out.Add(",\"location\":");
                if (result.captured)
                {
                    out.Position(LocateCaptured(*result.captured, diagnostic.related[j].begin));
                }
                else
                {
                    out.Add("null");
                }
                out.Add("}");
            }
            out.Add("]}");
        }
        out.Add("]}\n");
        output = std::move(out.text);
        return true;
    }
    catch (const std::length_error&)
    {
        return false;
    }
}
} // namespace step_source
