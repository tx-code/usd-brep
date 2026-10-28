// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSource.h"
#include "StepSourceReport.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
std::string File(const std::string& records)
{
    return "ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('test'),'2;1');\n"
           "FILE_NAME('x','2026-10-16',('a'),('b'),'p','s','');\nFILE_SCHEMA(('TEST'));\nENDSEC;\nDATA;\n" +
           records + "ENDSEC;\nEND-ISO-10303-21;\n";
}
void ReportWriterContract()
{
    using step_source::report::Writer;
    std::string raw = "prefix\"\\";
    raw.push_back('\0');
    raw += "\t\x7f\x80\xfftail";
    const std::string expected = "\"prefix\\\"\\\\\\u0000\\u0009\\u007f\\u0080\\u00fftail\"";
    std::string output;
    for (std::size_t limit = 0; limit <= expected.size() + 1; ++limit)
    {
        output.assign(8192, 's');
        const bool complete = Writer::Publish(
            limit,
            output,
            [&](Writer& writer)
            {
                writer.String(raw);
            }
        );
        Require(complete == (limit >= expected.size()), "Every emitted raw-byte budget boundary");
        Require(complete ? output == expected : output.empty(), "No stale or partial raw-byte report");
    }
    const std::string unicode = "\xe4\xb8\xad\xf0\x9f\x98\x80";
    const auto text = unicode + "\x7f\n";
    const auto escaped = "\"" + unicode + "\\u007f\\u000a\"";
    Require(
        Writer::Publish(
            escaped.size(),
            output,
            [&](Writer& writer)
            {
                writer.Utf8String(text);
            }
        ) && output == escaped,
        "Decoded UTF-8 keeps its literal bytes and escapes controls"
    );
    Require(
        !Writer::Publish(
            escaped.size() - 1,
            output,
            [&](Writer& writer)
            {
                writer.Utf8String(text);
            }
        ) && output.empty(),
        "UTF-8 budget failure after a prior complete report"
    );
    Require(
        Writer::Publish(
            2,
            output,
            [&](Writer& writer)
            {
                writer.String({});
            }
        ) && output == "\"\"",
        "Empty string report"
    );

    bool allocationPropagated = false;
    output.assign(8192, 's');
    try
    {
        Writer::Publish(
            10000,
            output,
            [&](Writer& writer)
            {
                Require(output.empty(), "Producer must observe no published output");
                writer.Add("partial");
                throw std::bad_alloc();
            }
        );
    }
    catch (const std::bad_alloc&)
    {
        allocationPropagated = true;
    }
    Require(allocationPropagated && output.empty(), "Allocation failure propagates without stale or partial output");

    bool producerPropagated = false;
    output.assign(8192, 's');
    try
    {
        Writer::Publish(
            10000,
            output,
            [&](Writer& writer)
            {
                Require(output.empty(), "Producer must observe empty output after another failure");
                writer.Add("partial");
                throw std::runtime_error("intentional producer failure");
            }
        );
    }
    catch (const std::runtime_error& failure)
    {
        producerPropagated = std::string_view(failure.what()) == "intentional producer failure";
    }
    Require(producerPropagated && output.empty(), "Other producer failures are not report byte limits");
    Require(
        Writer::Publish(
            2,
            output,
            [&](Writer& writer)
            {
                writer.Add("{}");
            }
        ) && output == "{}",
        "Success after producer failure"
    );
}
} // namespace

int main()
{
    using namespace step_source;
    static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<SourceSnapshot>().Records()[0])>>);
    const auto limits = Limits::PlanarFixtures();
    std::size_t checks = 0;
    try
    {
        const auto original = File(
            "#20=UNKNOWN((#999999999999999999999999999999,'a;#999',.U.),$,*);\n"
            "#999999999999999999999999999999=OTHER(TYPE(1.25E+100));\n"
        );
        auto bytes = original;
        auto outcome = InspectBytes(bytes, limits);
        Require(outcome.status == Status::Inspected && outcome.source, "source graph was not inspected");
        bytes.assign("changed caller buffer");
        Require(outcome.source->Bytes() == original, "caller mutation changed captured bytes");
        auto retained = outcome.source;
        outcome = {};
        Require(retained->Records().size() == 2, "snapshot did not own records after outcome release");
        Require(retained->Find("999999999999999999999999999999"), "sparse string ID lookup failed");
        Require(retained->Incoming("999999999999999999999999999999").size() == 1, "incoming reference index failed");
        Require(retained->References()[0].parameterPath == std::vector<std::size_t>({ 1, 1 }), "reference path lost");
        const auto* target = retained->Find("999999999999999999999999999999");
        Require(target->components[0].parameters[0].children[0].text == "1.25E+100", "exact numeric lexeme lost");
        ++checks;

        const auto path = std::filesystem::temp_directory_path() /
                          ("step-source-snapshot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".step");
        {
            std::ofstream file(path, std::ios::binary);
            file << original;
        }
        auto captured = InspectFile(path, limits);
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << "changed original path";
        }
        Require(captured.source && captured.source->Bytes() == original, "file modification changed snapshot");
        // Remove only this exact generated file, never recursively.
        std::filesystem::remove(path);
        Require(captured.source->Find("20"), "snapshot depended on original file lifetime");
        ++checks;

        const auto duplicate = InspectBytes(File("#01=A();\n#1=B();\n"), limits);
        Require(duplicate.status == Status::Rejected && !duplicate.source && duplicate.captured, "duplicate published a partial source");
        Require(
            duplicate.diagnostics[0].key == "DuplicateInstanceId" && duplicate.diagnostics[0].related.size() == 1,
            "duplicate did not locate both ID occurrences"
        );
        ++checks;

        const auto missing = InspectBytes(File("#1=A(TYPE((#999)));\n"), limits);
        Require(missing.status == Status::Rejected && !missing.source, "missing target published source");
        Require(
            missing.diagnostics[0].key == "UnresolvedRequiredReference" &&
                missing.diagnostics[0].parameterPath == std::vector<std::size_t>({ 1, 1, 1 }),
            "missing target path incorrect"
        );
        ++checks;

        const auto cycle = InspectBytes(File("#1=A(#2);\n#2=B(#1);\n"), limits);
        Require(cycle.source && cycle.source->References().size() == 2, "raw reference cycles must not be traversed or rejected");
        ++checks;

        auto exactLimit = limits;
        exactLimit.inputBytes = original.size();
        Require(InspectBytes(original, exactLimit).source != nullptr, "exact byte boundary failed");
        --exactLimit.inputBytes;
        const auto exceeded = InspectBytes(original, exactLimit);
        Require(exceeded.status == Status::ResourceLimit && !exceeded.captured && !exceeded.source, "byte limit pretended complete capture");
        ++checks;

        auto report = InspectBytes(original, limits);
        std::string json;
        Require(InspectJson(report, json), "normal report failed");
        // The configured limit is itself reported. Converge before testing the exact boundary.
        for (int i = 0; i < 3; ++i)
        {
            report.limits.reportBytes = json.size();
            Require(InspectJson(report, json), "exact report byte boundary failed");
        }
        Require(json.size() == report.limits.reportBytes, "report byte boundary did not converge");
        --report.limits.reportBytes;
        Require(!InspectJson(report, json) && json.empty(), "N-1 report limit accepted a partial document");
        report.limits.reportBytes = 0;
        json = "stale prior result";
        Require(!InspectJson(report, json) && json.empty(), "report limit retained truncated/stale success");
        ++checks;

        const auto empty = InspectBytes({}, limits);
        Require(empty.sha256 == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA256 empty vector");
        const auto abc = InspectBytes("abc", limits);
        Require(abc.sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 abc vector");
        ++checks;

        const auto plain = InspectBytes(File("#1=(A('it''s','path\\\\name') B($,*));\n"), limits);
        Require(plain.source && plain.source->Records()[0].complex, "complex form lost");
        const auto& p = plain.source->Records()[0].components[0].parameters;
        Require(p[0].text == "it's" && p[1].text == "path\\name", "string quote/reverse solidus decoding failed");
        ++checks;

        const auto encoded = File("#1=A('\\X2\\4E2D\\X0\\\\X4\\0001F600\\X0\\\r\n\\S\\D');\n");
        auto unicode = InspectBytes(encoded, limits);
        Require(unicode.source != nullptr, "Unicode source did not inspect");
        auto unicodeOwner = unicode.source;
        unicode = {};
        const auto& unicodeValue = unicodeOwner->Records()[0].components[0].parameters[0];
        Require(unicodeValue.text == "\xe4\xb8\xad\xf0\x9f\x98\x80\xc3\x84", "Decoded string is not exact owning UTF-8");
        Require(unicodeOwner->Bytes() == encoded, "Unicode decoding modified raw bytes");
        Require(
            unicodeOwner->Raw(unicodeValue.span) == "'\\X2\\4E2D\\X0\\\\X4\\0001F600\\X0\\\r\n\\S\\D'",
            "Unicode string span does not address original bytes"
        );
        ++checks;

        auto crlf = original;
        for (std::size_t i = 0; (i = crlf.find('\n', i)) != std::string::npos; i += 2)
        {
            crlf.insert(i, 1, '\r');
        }
        const auto windows = InspectBytes(crlf, limits);
        Require(windows.source && windows.source->Locate(windows.source->Records()[0].span.begin).line == 8, "CRLF source location incorrect");
        const auto last = windows.source->Records().back().span;
        Require(windows.source->Raw(last).back() == ';', "raw record span incorrect");
        bool spanRejected = false;
        try
        {
            windows.source->Raw({ 2, crlf.size() + 1 });
        }
        catch (const std::out_of_range&)
        {
            spanRejected = true;
        }
        Require(spanRejected, "invalid source span was accepted");
        ++checks;

        auto failed = InspectBytes("A\r\nB\rC\nD", limits);
        // Literal expectations include the LF byte inside CRLF, lone CR/LF and EOF.
        const Location positions[] = { { 1, 1 }, { 1, 2 }, { 1, 3 }, { 2, 1 }, { 2, 2 }, { 3, 1 }, { 3, 2 }, { 4, 1 }, { 4, 2 } };
        for (std::size_t offset = 0; offset < std::size(positions); ++offset)
        {
            failed.diagnostics[0].primary = { offset, offset };
            Require(InspectJson(failed, json), "Captured rejection location report");
            const auto expected = "\"location\":{\"line\":" + std::to_string(positions[offset].line) +
                                  ",\"column\":" + std::to_string(positions[offset].column) + "}";
            Require(json.find(expected) != std::string::npos, "Captured CR/LF/CRLF/EOF byte location incorrect");
        }
        Require(
            InspectJson(empty, json) && json.find("\"location\":{\"line\":1,\"column\":1}") != std::string::npos,
            "Empty capture has EOF location"
        );
        Require(InspectJson(exceeded, json) && json.find("\"location\":null") != std::string::npos, "Incomplete capture has no location");
        const auto lf = crlf.find('\n');
        const auto indexed = windows.source->Locate(lf);
        Require(indexed.line == 1 && indexed.column == lf + 1, "Indexed CRLF byte location differs from captured policy");
        ++checks;

        failed.diagnostics[0].message = std::string("\"\\\0\t\x7f\x80\xff", 7);
        Require(InspectJson(failed, json), "Diagnostic byte escaping report");
        Require(
            json.find("\"message\":\"\\\"\\\\\\u0000\\u0009\\u007f\\u0080\\u00ff\"") != std::string::npos && json.back() == '\n',
            "Diagnostic escaping must preserve arbitrary bytes and final LF"
        );
        ++checks;
        {
            auto legacy = File("#1=A('standard');\n");
            const std::string name = "'legacy-\x81\\''name.stp'";
            legacy.replace(legacy.find("'x'"), 3, name);
            const auto strictName = InspectBytes(legacy, limits);
            Require(!strictName.source && strictName.status == Status::Unsupported, "strict source must still reject raw high bytes");
            auto opaque = InspectBytes(legacy, limits, HeaderNamePolicy::PreserveOpaqueBytes);
            Require(opaque.status == Status::Inspected && opaque.source, "explicit opaque header name");
            auto owner = opaque.source;
            opaque = {};
            const auto& value = owner->Header()[1].parameters[0];
            Require(value.kind == ValueKind::OpaqueString && value.text.empty(), "opaque name must not fabricate Unicode text");
            Require(owner->Raw(value.span) == name && owner->Bytes() == legacy, "owning opaque name lost original bytes/span");
            Require(owner->Records()[0].components[0].parameters[0].text == "standard", "opaque name policy leaked to DATA");
            ++checks;
        }
        {
            const std::string raw = "'unknown-\x80\xfe'";
            const auto opaqueInput = File("#1=A(" + raw + ",#2);\n#2=B();\n");
            auto opaque = InspectBytes(opaqueInput, limits, HeaderNamePolicy::StrictAscii, DataStringPolicy::PreserveOpaqueBytes);
            Require(opaque.status == Status::Inspected && opaque.source, "explicit opaque DATA capture");
            auto owner = opaque.source;
            const auto& value = owner->Records()[0].components[0].parameters[0];
            Require(value.kind == ValueKind::OpaqueString && value.text.empty(), "unknown DATA text is unavailable");
            Require(
                owner->Raw(value.span) == raw && owner->Bytes() == opaqueInput && owner->References().size() == 1,
                "source bytes, token identity and references survive opaque text"
            );
            Require(InspectJson(opaque, json) && json.find("\"text\":null") != std::string::npos, "no fake empty decoded DATA text");
            opaque.limits.reportBytes = 1;
            Require(!InspectJson(opaque, json) && json.empty(), "opaque DATA report obeys output budget");
            opaque = {};
            Require(owner->Raw(value.span) == raw, "DATA token survives outcome release");
            const auto bad = InspectBytes(opaqueInput + "/*\x80*/", limits, HeaderNamePolicy::StrictAscii, DataStringPolicy::PreserveOpaqueBytes);
            Require(!bad.source && bad.status == Status::Unsupported, "DATA policy cannot publish high-byte comments");
            auto assumed = InspectBytes(opaqueInput, limits, HeaderNamePolicy::StrictAscii, DataStringPolicy::PreserveAsciiInvariantBytes);
            Require(assumed.source && assumed.source->HasAsciiInvariantDataAssumption(), "explicit DATA assumption captured");
            const auto& assumedValue = assumed.source->Records()[0].components[0].parameters[0];
            Require(assumed.source->IsAssumedNonAsciiData(assumedValue), "captured token excludes ASCII literals under assumption");
            Require(!owner->IsAssumedNonAsciiData(value), "ordinary preservation cannot acquire text interpretation");
            assumed.dataStringPolicy = DataStringPolicy::StrictAscii;
            Require(
                InspectJson(assumed, json) && json.find("PreserveAsciiInvariantBytes") != std::string::npos,
                "immutable assumption survives outcome annotation reset"
            );
            for (const auto& payload : { std::string("'ascii'"), std::string("'\x80\\trail'"), std::string("'\x80\x1b' ") })
            {
                const auto probe = InspectBytes(
                    File("#1=A(" + payload + ");\n"),
                    limits,
                    HeaderNamePolicy::StrictAscii,
                    DataStringPolicy::PreserveAsciiInvariantBytes
                );
                Require(!probe.source || !probe.source->HasAsciiInvariantDataAssumption(), "unqualified text has no assumption receipt");
            }
            bool bytesRejected = false, fileRejected = false;
            try
            {
                InspectBytes(opaqueInput, limits, HeaderNamePolicy::StrictAscii, static_cast<DataStringPolicy>(99));
            }
            catch (const std::invalid_argument&)
            {
                bytesRejected = true;
            }
            try
            {
                InspectFile({}, limits, HeaderNamePolicy::StrictAscii, static_cast<DataStringPolicy>(99));
            }
            catch (const std::invalid_argument&)
            {
                fileRejected = true;
            }
            Require(bytesRejected && fileRejected, "unknown DATA policy rejected before capture");
            ++checks;
        }
        {
            auto blank = File("#1=A();\n");
            blank.replace(blank.find("'2;1'"), 5, "'   '");
            auto assumedBlank = InspectBytes(
                blank,
                limits,
                HeaderNamePolicy::StrictAscii,
                DataStringPolicy::StrictAscii,
                CarrierInterpretation::BlankLevelAsCorrectedEncoding
            );
            Require(assumedBlank.source && assumedBlank.source->AppliedCarrierAssumption(), "explicit blank carrier receipt");
            auto retainedBlank = assumedBlank.source;
            const auto* blankReceipt = retainedBlank->AppliedCarrierAssumption();
            Require(
                blankReceipt->declaredLevel == "   " && retainedBlank->Raw(blankReceipt->declaredSpan) == "'   '",
                "blank declaration source identity"
            );
            assumedBlank = {};
            Require(retainedBlank->AppliedCarrierAssumption() == blankReceipt && retainedBlank->Bytes() == blank, "immutable blank receipt ownership");
            auto input = File("#1=A(LENGTH_MEASURE(+1.2500E-2),#1);\n");
            const auto at = input.find("'2;1'");
            Require(at != std::string::npos, "carrier fixture declaration");
            input.replace(at, 5, "'1'");
            const auto strict = InspectBytes(input, limits);
            Require(strict.status == Status::Unsupported && !strict.source, "level1 keeps strict refusal");
            auto assumed = InspectBytes(
                input,
                limits,
                HeaderNamePolicy::StrictAscii,
                DataStringPolicy::StrictAscii,
                CarrierInterpretation::Level1AsCorrectedEncoding
            );
            Require(assumed.status == Status::Inspected && assumed.source, "explicit carrier assumption admits existing syntax");
            auto owner = assumed.source;
            const auto* receipt = owner->AppliedCarrierAssumption();
            Require(receipt && receipt->declaredLevel == "1" && owner->Raw(receipt->declaredSpan) == "'1'", "source declaration preserved");
            Require(
                receipt->originalDiagnostics[0].key == strict.diagnostics[0].key &&
                    receipt->originalDiagnostics[0].primary.begin == strict.diagnostics[0].primary.begin,
                "original carrier refusal preserved"
            );
            assumed.carrierAssumption.reset();
            assumed.requestedCarrierInterpretation = CarrierInterpretation::DeclaredOnly;
            assumed.diagnostics.clear();
            Require(
                InspectJson(assumed, json) && json.find("InterpretedUnverified") != std::string::npos,
                "mutable outcome cannot erase source-owned qualification"
            );
            assumed = {};
            Require(owner->AppliedCarrierAssumption() == receipt && owner->Bytes() == input, "carrier receipt survives outcome destruction");
            const auto& typed = owner->Records()[0].components[0].parameters[0];
            Require(typed.text == "LENGTH_MEASURE" && typed.children[0].text == "+1.2500E-2", "carrier option cannot rewrite scalar data");
            auto canonical = File("#1=A();\n");
            auto normal = InspectBytes(canonical, limits);
            normal.carrierAssumption = std::make_shared<const CarrierAssumption>(*receipt);
            Require(
                InspectJson(normal, json) && json.find("carrier_interpretation") == std::string::npos,
                "outcome cannot forge an assumption on an ordinary snapshot"
            );
            auto bad = input;
            bad.replace(bad.find("#1);"), 4, "#99);");
            const auto carrierFailure = InspectBytes(
                bad,
                limits,
                HeaderNamePolicy::StrictAscii,
                DataStringPolicy::StrictAscii,
                CarrierInterpretation::Level1AsCorrectedEncoding
            );
            Require(
                !carrierFailure.source && carrierFailure.carrierAssumption && carrierFailure.captured,
                "later failure retains receipt without partial graph"
            );
            bool invalid = false;
            try
            {
                InspectFile(
                    "missing-carrier-fixture.step",
                    limits,
                    HeaderNamePolicy::StrictAscii,
                    DataStringPolicy::StrictAscii,
                    static_cast<CarrierInterpretation>(99)
                );
            }
            catch (const std::invalid_argument&)
            {
                invalid = true;
            }
            Require(invalid, "unknown carrier request rejected before IO");
            ++checks;
        }
        {
            auto input = File("#1=A('\\X2\\4e2d\\X0\\',+1.2500E-2);\n");
            input.replace(input.find("'2;1'"), 5, "'1'");
            auto interpreted = InspectBytes(
                input,
                limits,
                HeaderNamePolicy::StrictAscii,
                DataStringPolicy::StrictAscii,
                CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex
            );
            Require(interpreted.source && interpreted.status == Status::Inspected, "explicit lowercase payload admission");
            auto owner = interpreted.source;
            const auto receipt = owner->AppliedCarrierAssumption();
            Require(receipt && receipt->stringHexInterpretations.size() == 1, "source owns per-string interpretation evidence");
            const auto& evidence = receipt->stringHexInterpretations[0];
            const auto& value = owner->Records()[0].components[0].parameters[0];
            Require(
                value.text == "\xE4\xB8\xAD" && evidence.complete && evidence.lowercaseDigits == 2,
                "numeric hex interpretation has independent Unicode truth"
            );
            Require(
                owner->Raw(evidence.tokenSpan) == "'\\X2\\4e2d\\X0\\'" && owner->Raw(evidence.firstStrictSpan) == "\\X2\\4e",
                "whole original token and first strict refusal location retained"
            );
            interpreted.requestedCarrierInterpretation = CarrierInterpretation::DeclaredOnly;
            interpreted.carrierAssumption.reset();
            Require(
                InspectJson(interpreted, json) && json.find("string_hex_interpretations") != std::string::npos,
                "mutable outcome cannot hide source-owned string evidence"
            );
            interpreted = {};
            Require(owner->AppliedCarrierAssumption() == receipt && owner->Bytes() == input, "immutable string evidence lifetime");
            auto bad = input;
            bad.replace(bad.find("4e2d"), 4, "4e2dGGGG");
            const auto rejected = InspectBytes(
                bad,
                limits,
                HeaderNamePolicy::StrictAscii,
                DataStringPolicy::StrictAscii,
                CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex
            );
            Require(
                !rejected.source && rejected.carrierAssumption && rejected.carrierAssumption->stringHexInterpretations.size() == 1 &&
                    !rejected.carrierAssumption->stringHexInterpretations[0].complete,
                "later invalid payload keeps incomplete evidence without a partial graph"
            );
            ++checks;
        }
        {
            auto input = File("#1=UNKNOWN($);\n");
            const auto field = input.find("'p','s',''");
            Require(field != std::string::npos, "header metadata mutation");
            input.replace(field, std::string("'p','s',''").size(), "'p','s',$");
            const auto strict = InspectBytes(input, limits);
            Require(
                strict.status == Status::Rejected && !strict.source && strict.diagnostics.back().key == "InvalidHeader",
                "strict unset authorization rejection retained"
            );
            auto preserved = InspectBytes(
                input,
                limits,
                HeaderNamePolicy::StrictAscii,
                DataStringPolicy::StrictAscii,
                CarrierInterpretation::DeclaredOnly,
                HeaderMetadataPolicy::PreserveUnsetAuthorization
            );
            Require(preserved.status == Status::Inspected && preserved.source, "explicit header metadata continuation");
            const auto owner = preserved.source;
            const auto& value = owner->Header()[1].parameters[6];
            Require(
                value.kind == ValueKind::Unset && owner->Raw(value.span) == "$" && owner->Bytes() == input,
                "no fabricated authorization or rewritten bytes"
            );
            const auto& issues = owner->PreservedHeaderIssues();
            Require(
                issues.size() == 1 && issues[0].key == strict.diagnostics.back().key && issues[0].message == strict.diagnostics.back().message &&
                    issues[0].primary.begin == strict.diagnostics.back().primary.begin && issues[0].parameterPath == std::vector<std::size_t>{ 7 },
                "original error and parameter path retained"
            );
            preserved.preservedHeaderIssues.clear();
            preserved.headerMetadataPolicy = HeaderMetadataPolicy::Strict;
            std::string headerReport;
            Require(
                InspectJson(preserved, headerReport) && headerReport.find("KnownNonGeometricErrorPreserved") != std::string::npos,
                "owning receipt overrides mutable annotations"
            );
            auto exact = preserved;
            // The configured capacity is itself emitted in the source report.
            // Establish that fixed point before applying its N-1 boundary.
            for (unsigned iteration = 0; iteration < 3; ++iteration)
            {
                exact.limits.reportBytes = headerReport.size();
                Require(InspectJson(exact, headerReport), "header receipt report exact capacity");
            }
            Require(exact.limits.reportBytes == headerReport.size(), "header report capacity fixed point");
            --exact.limits.reportBytes;
            Require(!InspectJson(exact, headerReport) && headerReport.empty(), "header receipt report N-1 is atomic");
            for (unsigned mode = 0; mode < 3; ++mode)
            {
                auto bad = input;
                if (mode == 0)
                {
                    bad.replace(bad.find("'p','s',$"), std::string("'p','s',$").size(), "$,'s',$");
                }
                if (mode == 1)
                {
                    bad.replace(bad.find("FILE_SCHEMA(('TEST'))"), std::string("FILE_SCHEMA(('TEST'))").size(), "FILE_SCHEMA(($))");
                }
                if (mode == 2)
                {
                    bad.replace(bad.find("#1=UNKNOWN($)"), std::string("#1=UNKNOWN($)").size(), "#1=UNKNOWN(#999)");
                }
                const auto deniedHeader = InspectBytes(
                    bad,
                    limits,
                    HeaderNamePolicy::StrictAscii,
                    DataStringPolicy::StrictAscii,
                    CarrierInterpretation::DeclaredOnly,
                    HeaderMetadataPolicy::PreserveUnsetAuthorization
                );
                Require(!deniedHeader.source && deniedHeader.status == Status::Rejected, "metadata scope cannot hide another header or DATA failure");
            }
            preserved = {};
            Require(owner->PreservedHeaderIssues().size() == 1 && owner->Raw(value.span) == "$", "source metadata evidence lifetime");
            ++checks;
        }
        ReportWriterContract();
        ++checks;
        std::cout << "Passed " << checks << " native source contract groups\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Contract failure after " << checks << " groups: " << error.what() << '\n';
        return 1;
    }
}
