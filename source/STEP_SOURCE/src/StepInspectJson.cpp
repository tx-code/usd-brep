// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSourceReport.h"

namespace step_source
{
namespace
{
using report::Writer;

void ValueJson(Writer& out, const Value& value, const SourceSnapshot& source)
{
    out.Add("{\"kind\":");
    out.String(ValueKindName(value.kind));
    out.Add(",\"span\":");
    out.Range(value.span);
    out.Add(",\"raw\":");
    out.String(source.Raw(value.span));
    out.Add(",\"text\":");
    if (value.kind == ValueKind::OpaqueString)
    {
        const auto& header = source.Header();
        const bool headerName = header.size() > 1 && !header[1].parameters.empty() && &value == &header[1].parameters[0];
        const bool fileNameText = header.size() > 1 && value.span.begin >= header[1].span.begin && value.span.end <= header[1].span.end;
        out.Add("null,\"interpretation\":\"Unestablished\",\"notice\":");
        // Preserve the existing HEADER notice bytes; DATA tokens cannot inherit
        // its filename provenance merely because they share an opaque ValueKind.
        out.String(
            headerName ? "Nonstandard HEADER filename retained as original bytes; no code page inferred" :
            fileNameText ? "Nonstandard FILE_NAME metadata retained as original bytes; no code page inferred" :
                         "Nonstandard DATA string retained as original bytes; no code page inferred"
        );
    }
    else
    {
        out.Utf8String(value.text);
    }
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
void DiagnosticJson(Writer& out, const InspectOutcome& result, const Diagnostic& diagnostic)
{
    out.Add("{\"key\":");
    out.String(diagnostic.key);
    out.Add(",\"stage\":");
    out.String(diagnostic.stage);
    out.Add(",\"message\":");
    out.String(diagnostic.message);
    out.Add(",\"span\":");
    out.Range(diagnostic.primary);
    out.Add(",\"location\":");
    out.Position(result, diagnostic.primary.begin);
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
        out.Position(result, diagnostic.related[j].begin);
        out.Add("}");
    }
    out.Add("]}");
}
} // namespace

void report::WriteJson(Writer& out, const InspectOutcome& result)
{
    out.Add("{\"format_version\":\"step-source.v1\",\"scope\":\"SyntaxIndexReferences\",\"status\":");
    out.String(StatusName(result.status));
    if (result.headerNamePolicy == HeaderNamePolicy::PreserveOpaqueBytes)
    {
        out.Add(",\"header_name_policy\":\"PreserveOpaqueBytes\"");
    }
    else if (result.headerNamePolicy == HeaderNamePolicy::PreserveFileNameText)
    {
        out.Add(",\"header_name_policy\":\"PreserveFileNameText\"");
    }
    if (result.dataStringPolicy == DataStringPolicy::PreserveOpaqueBytes)
    {
        out.Add(",\"data_string_policy\":\"PreserveOpaqueBytes\"");
    }
    // A completed snapshot is the qualification owner. Resettable outcome
    // annotations cannot hide its assumption; failures retain their own receipt.
    const auto* assumption = result.source ? result.source->AppliedCarrierAssumption() : result.carrierAssumption.get();
    if (result.requestedCarrierInterpretation != CarrierInterpretation::DeclaredOnly || assumption)
    {
        out.Add(",\"carrier_interpretation\":{\"requested\":");
        out.String(CarrierInterpretationName(result.requestedCarrierInterpretation));
        out.Add(",\"applied\":");
        out.Add(assumption ? "true" : "false");
        out.Add(",\"status\":");
        out.String(assumption ? "InterpretedUnverified" : "NotApplied");
        out.Add(",\"interpretation\":");
        out.String(CarrierInterpretationName(assumption ? assumption->interpretation : CarrierInterpretation::DeclaredOnly));
        out.Add(",\"declared_level\":");
        if (assumption)
        {
            out.String(assumption->declaredLevel);
        }
        else if (result.source)
        {
            out.String(result.source->Header()[0].parameters[1].text);
        }
        else
        {
            out.Add("null");
        }
        out.Add(",\"declaration_verified\":false,\"original_diagnostics\":[");
        if (assumption)
        {
            for (std::size_t i = 0; i < assumption->originalDiagnostics.size(); ++i)
            {
                if (i)
                {
                    out.Add(",");
                }
                DiagnosticJson(out, result, assumption->originalDiagnostics[i]);
            }
        }
        out.Add("]");
        if (assumption && !assumption->stringHexInterpretations.empty())
        {
            out.Add(",\"string_hex_interpretations\":[");
            for (std::size_t i = 0; i < assumption->stringHexInterpretations.size(); ++i)
            {
                if (i)
                {
                    out.Add(",");
                }
                const auto& evidence = assumption->stringHexInterpretations[i];
                out.Add("{\"token_span\":");
                out.Range(evidence.tokenSpan);
                out.Add(",\"first_strict_span\":");
                out.Range(evidence.firstStrictSpan);
                out.Add(",\"strict_key\":\"InvalidStringEncoding\",\"lowercase_digits\":");
                out.Number(evidence.lowercaseDigits);
                out.Add(",\"complete\":");
                out.Bool(evidence.complete);
                out.Add("}");
            }
            out.Add("]");
        }
        out.Add("}");
    }
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
        DiagnosticJson(out, result, diagnostic);
    }
    out.Add("]}\n");
}

bool InspectJson(const InspectOutcome& result, std::string& output)
{
    return report::Writer::Publish(
        result.limits.reportBytes,
        output,
        [&](report::Writer& out)
        {
            report::WriteJson(out, result);
        }
    );
}
} // namespace step_source
