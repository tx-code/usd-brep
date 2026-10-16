// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSource.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
        std::cout << "Passed " << checks << " native source contract groups\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Contract failure after " << checks << " groups: " << error.what() << '\n';
        return 1;
    }
}
