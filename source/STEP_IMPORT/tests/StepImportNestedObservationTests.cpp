// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace step_import;
namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    Require(bool(file), "synthetic input exists");
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}
// Only these public, single-line synthetic fixtures use this numbered layout.
// This constructs a new test source; it is not a STEP parser or source repair.
std::string NestedSource(const std::string& base)
{
    std::ostringstream result;
    std::vector<std::string> geometry;
    std::vector<unsigned long> representationItems;
    const std::regex reference("#([0-9]+)");
    const std::regex shapeItems("#20=ADVANCED_BREP_SHAPE_REPRESENTATION\\('[^']*',\\((#[0-9]+(?:,#[0-9]+)*)\\),#12\\);");
    std::istringstream input(base);
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.starts_with("FILE_SCHEMA("))
        {
            line = "FILE_SCHEMA(('AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}'));";
        }
        if (line.starts_with("#2="))
        {
            line = "#2=APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2010,#1);";
        }
        if (line.starts_with("#20="))
        {
            std::smatch items;
            Require(std::regex_match(line, items, shapeItems), "synthetic source representation item layout");
            const auto list = items[1].str();
            for (std::sregex_iterator i(list.begin(), list.end(), reference), end; i != end; ++i)
            {
                representationItems.push_back(std::stoul((*i)[1].str()));
            }
        }
        if (line == "END-ISO-10303-21;")
        {
            continue;
        }
        if (line == "ENDSEC;" && !geometry.empty())
        {
            break;
        }
        if (line.starts_with('#'))
        {
            const auto id = std::stoul(line.substr(1, line.find('=') - 1));
            if (id >= 900)
            {
                continue;
            }
            if (id >= 21)
            {
                geometry.push_back(line);
            }
        }
        result << line << '\n';
    }
    Require(!representationItems.empty() && representationItems.front() == 21, "synthetic representation starts with its material body");
    for (unsigned offset : { 10000u, 20000u, 30000u })
    {
        for (const auto& record : geometry)
        {
            std::size_t position = 0;
            for (std::sregex_iterator i(record.begin(), record.end(), reference), end; i != end; ++i)
            {
                const auto at = static_cast<std::size_t>(i->position());
                const auto id = std::stoul((*i)[1].str());
                Require(id >= 21, "synthetic geometry references only its own numbered graph");
                result << record.substr(position, at - position) << '#' << id + offset;
                position = at + static_cast<std::size_t>(i->length());
            }
            result << record.substr(position) << '\n';
        }
    }
    const auto copiedItems = [&](std::initializer_list<unsigned> offsets)
    {
        std::ostringstream items;
        bool first = true;
        for (const auto offset : offsets)
        {
            for (const auto id : representationItems)
            {
                Require(id >= 21 && id < 900, "synthetic representation items belong to the copied geometry graph");
                items << (first ? "" : ",") << '#' << id + offset;
                first = false;
            }
        }
        return items.str();
    };
    // The roof has an associated coordinate frame as a representation item.
    // Copy its membership along with the body: otherwise the copied frame and
    // directions become orphans and source checking correctly stops before the
    // cancellation boundary this test is meant to exercise.
    result << "#900=PRODUCT('second','second',$,(#3));\n"
              "#901=PRODUCT_DEFINITION_FORMATION('B',$,#900);\n"
              "#902=PRODUCT_DEFINITION('D2',$,#901,#6);\n"
              "#903=PRODUCT_DEFINITION_SHAPE('',$,#902);\n"
              "#904=SHAPE_DEFINITION_REPRESENTATION(#903,#920);\n"
              "#920=ADVANCED_BREP_SHAPE_REPRESENTATION('second',("
           << copiedItems({ 10000u, 20000u })
           << "),#12);\n"
              "#1900=PRODUCT('third','third',$,(#3));\n"
              "#1901=PRODUCT_DEFINITION_FORMATION('C',$,#1900);\n"
              "#1902=PRODUCT_DEFINITION('D3',$,#1901,#6);\n"
              "#1903=PRODUCT_DEFINITION_SHAPE('',$,#1902);\n"
              "#1904=SHAPE_DEFINITION_REPRESENTATION(#1903,#1920);\n"
              "#1920=ADVANCED_BREP_SHAPE_REPRESENTATION('third',("
           << copiedItems({ 30000u })
           << "),#12);\n"
              "ENDSEC;\nEND-ISO-10303-21;\n";
    return result.str();
}
Options Config()
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    options.reportEncoding = ReportEncoding::CompactV2;
    options.numericalIntegration = NumericalIntegrationSettings{ 1e-4 };
    return options;
}
using Events = std::vector<std::pair<Phase, std::size_t>>;
void Receipt(const std::filesystem::path& output, const char* name, const DocumentOutcome& outcome, const Events& events)
{
    std::ofstream json(output / (std::string(name) + ".json"), std::ios::binary);
    json << outcome.report.json;
    std::ofstream trace(output / (std::string(name) + ".progress"), std::ios::binary);
    for (const auto& [phase, calls] : events)
    {
        trace << PhaseName(phase) << ' ' << calls << '\n';
    }
    Require(bool(json) && bool(trace), "local receipt writes succeed");
}
DocumentOutcome Run(const std::string& text, Options options, Events& events)
{
    const auto observer = options.execution.progress;
    options.execution.progress = [&](Progress p)
    {
        events.emplace_back(p.phase, p.nativeCalls);
        if (observer)
        {
            observer(p);
        }
    };
    return ImportDocumentBytes(text, options);
}
void CheckPrefix(const DocumentOutcome& outcome)
{
    Require(!outcome.document && outcome.report.definitions.size() == 2, "failed request never enters the third definition");
    const auto& first = outcome.report.definitions[0];
    const auto& middle = outcome.report.definitions[1];
    Require(first.checksPassed && first.checks->candidateDiscarded, "passed earlier definition is discarded, not forgotten");
    Require(!middle.checksPassed && middle.checks->bodies.size() == 2, "middle definition retains its failed body observation");
    Require(
        middle.checks->bodies[0].checksPassed && middle.checks->bodies[0].checks->candidateDiscarded,
        "passed earlier body is discarded with the whole request"
    );
    Require(!middle.checks->bodies[1].checksPassed && outcome.report.candidateDiscarded, "failed body never qualifies a partial model");
    Require(
        outcome.report.nativeCalls == first.checks->nativeCalls + middle.checks->nativeCalls,
        "definition counters retain inclusive request work on failure"
    );
}
void SelectionControls(const std::string& text, const std::filesystem::path& output)
{
    auto options = Config();
    options.definitionSelection = DefinitionSelectionRequest{ step_source::InspectBytes(text, options.sourceLimits).sha256, { "902" } };
    Events selectedEvents;
    const auto selected = Run(text, options, selectedEvents);
    Receipt(output, "selected", selected, selectedEvents);
    Require(
        selected.status == Status::Accepted && selected.document && selected.document->PartCount() == 1 && selected.document->BodyCount() == 2 &&
            selected.document->SourceDefinitionId(0) == "902" && selected.document->SourceShapeId(0) == "10021" &&
            selected.document->SourceShapeId(1) == "20021",
        "explicit selection publishes the complete two-body definition in source order"
    );
    Require(
        selected.report.definitionSelection && selected.report.definitionSelection->selectedDefinitionIds == std::vector<std::string>{ "902" } &&
            selected.report.definitions.size() == 1 && selected.report.definitions[0].checksPassed,
        "selected publication owns the complete selection receipt"
    );
    for (const auto phase : { Phase::BeforeDefinitionSelection, Phase::DefinitionSelectionReady })
    {
        auto cancelled = options;
        cancelled.execution.control = std::make_shared<ImportControl>();
        cancelled.execution.progress = [&](Progress p)
        {
            if (p.phase == phase)
            {
                cancelled.execution.control->RequestCancel();
            }
        };
        Events trace;
        const auto stopped = Run(text, cancelled, trace);
        Receipt(output, phase == Phase::DefinitionSelectionReady ? "selected-ready-cancel" : "selected-before-cancel", stopped, trace);
        Require(
            stopped.status == Status::Cancelled && !stopped.document && stopped.report.nativeCalls == 0 && stopped.report.definitions.empty() &&
                stopped.report.lastBoundary == phase,
            "selection cancellation stops before constructing any native definition"
        );
        Require(
            stopped.report.definitionSelection.has_value() == (phase == Phase::DefinitionSelectionReady),
            "only a completed selection leaves its receipt before cancellation"
        );
        Require(
            stopped.report.diagnostics.back().source.key == "ImportCancelled" && !stopped.report.json.empty(),
            "selection cancellation retains its diagnostic and mandatory report"
        );
    }
    auto unavailable = options;
    unavailable.definitionSelection->definitionIds = { "900001" };
    Events unavailableEvents;
    const auto refused = Run(text, unavailable, unavailableEvents);
    Receipt(output, "selected-unavailable", refused, unavailableEvents);
    Require(
        refused.status == Status::Unsupported && !refused.document && refused.report.nativeCalls == 0 && !refused.report.definitionSelection &&
            refused.report.definitions.empty() && refused.report.diagnostics.back().source.key == "SelectedDefinitionUnavailable" &&
            refused.report.diagnostics.back().source.stage == "Selection",
        "unavailable definition returns its original selection failure without a partial receipt"
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 4, "box fixture, roof fixture and output directory required");
        const auto text = NestedSource(Read(argv[1]));
        const std::filesystem::path output(argv[3]);
        std::filesystem::create_directories(output);
        std::ofstream(output / "nested.step", std::ios::binary) << text;
        Events events;
        auto accepted = Run(text, Config(), events);
        Receipt(output, "accepted", accepted, events);
        Require(accepted.status == Status::Accepted && accepted.document, "three definitions and four bodies are accepted together");
        Require(accepted.document->PartCount() == 3 && accepted.document->BodyCount() == 4, "definition/body inventories stay distinct");
        const auto& reports = accepted.report.definitions;
        Require(reports.size() == 3 && reports[1].checks->bodies.size() == 2, "public report retains all three nesting levels");
        const auto& middle = *reports[1].checks;
        Require(
            middle.nativeCalls == middle.bodies[0].checks->nativeCalls + middle.bodies[1].checks->nativeCalls,
            "middle definition covers both bodies exactly once"
        );
        Require(
            accepted.report.nativeCalls == reports[0].checks->nativeCalls + middle.nativeCalls + reports[2].checks->nativeCalls,
            "root ledger covers every definition exactly once"
        );
        Require(
            accepted.report.numericalIntegration->completed && middle.numericalIntegration->completed,
            "integration observations aggregate through the intermediate definition"
        );
        const auto beforeLastBody = reports[0].checks->nativeCalls + middle.bodies[0].checks->nativeCalls;
        const auto reportBytes = accepted.report.json.size();
        Events againEvents;
        const auto again = Run(text, Config(), againEvents);
        Require(again.report.json == accepted.report.json && againEvents == events, "report and progress are deterministic without memory sampling");
        auto retained = accepted.document;
        accepted = {};
        Require(
            retained->Source()->Source().Bytes() == text && retained->BodyPartIndex(2) == 1,
            "owning snapshot keeps source and body correspondence after outcome disposal"
        );

        auto cancel = Config();
        cancel.execution.control = std::make_shared<ImportControl>();
        cancel.execution.progress = [&](Progress p)
        {
            if (p.phase == Phase::BeforeNativeCall && p.nativeCalls == beforeLastBody)
            {
                cancel.execution.control->RequestCancel();
            }
        };
        Events cancelledEvents;
        const auto cancelled = Run(text, cancel, cancelledEvents);
        Receipt(output, "cancelled", cancelled, cancelledEvents);
        Require(cancelled.status == Status::Cancelled, "cancelled before the middle definition's second body");
        CheckPrefix(cancelled);
        Require(
            cancelled.report.nativeCalls == beforeLastBody && cancelled.report.definitions[1].checks->bodies[1].checks->nativeCalls == 0,
            "cancel checkpoint precedes native-call charging"
        );

        auto limited = Config();
        limited.nativeLimits.nativeCalls = beforeLastBody + 1;
        Events limitedEvents;
        const auto exhausted = Run(text, limited, limitedEvents);
        Receipt(output, "limited", exhausted, limitedEvents);
        Require(
            exhausted.status == Status::ResourceLimit && exhausted.report.nativeCalls == beforeLastBody + 1,
            "one cumulative request budget, including intermediate nesting"
        );
        CheckPrefix(exhausted);
        for (auto bytes : { reportBytes, reportBytes - 1 })
        {
            auto bounded = Config();
            bounded.nativeLimits.reportBytes = bytes;
            Events trace;
            const auto result = Run(text, bounded, trace);
            Require(
                bytes == reportBytes ? result.status == Status::Accepted && bool(result.document) :
                                       result.status == Status::ResourceLimit && !result.document,
                "exact report capacity succeeds and one byte less prevents publication"
            );
            Receipt(output, bytes == reportBytes ? "exact-capacity" : "short-capacity", result, trace);
        }

        auto roof = NestedSource(Read(argv[2]));
        const std::string face = "#20517=ADVANCED_FACE('',(#20516),#20514,.T.);";
        const auto faceAt = roof.find(face);
        Require(faceAt != std::string::npos, "synthetic middle-body repair identity");
        roof.replace(faceAt, face.size(), "#20517=ADVANCED_FACE('',(#20516),#20514,.F.);");
        auto repair = Config();
        FaceOrientationRepairRequest plan;
        plan.sourceSha256 = step_source::InspectBytes(roof, repair.sourceLimits).sha256;
        plan.maximumFaceEdits = 1;
        plan.maximumProofControls = 10000;
        plan.maximumProofProducts = 1000000;
        plan.edits.push_back({ "902", "20021", "20517", false, true });
        repair.faceOrientationRepair = plan;
        repair.execution.control = std::make_shared<ImportControl>();
        repair.execution.progress = [&](Progress p)
        {
            if (p.phase == Phase::BeforeRepairProof)
            {
                repair.execution.control->RequestCancel();
            }
        };
        Events repairEvents;
        const auto stopped = Run(roof, repair, repairEvents);
        Receipt(output, "repair-cancelled", stopped, repairEvents);
        Require(stopped.status == Status::Cancelled, "repair witness precedes proof-stage cancellation");
        CheckPrefix(stopped);
        const auto& failedBody = *stopped.report.definitions[1].checks->bodies[1].checks;
        Require(
            failedBody.nativeCalls == 0 && failedBody.originalAttempt && failedBody.originalAttempt->nativeCalls > 0,
            "repair witness counts are overlapping observations, not additive report-tree children"
        );
        SelectionControls(text, output);
        std::cout << "Nested observation contracts passed; complete report bytes=" << reportBytes << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
