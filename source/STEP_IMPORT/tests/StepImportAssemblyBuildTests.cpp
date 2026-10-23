// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportAssemblyBuild.h"
#include "StepImportAssemblyEvidence.h"
#include "StepImportExecution.h"
#include "StepImportNativeData.h"

#include <SmAssertArray.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
using namespace step_import;
using namespace step_import::detail;
void Require(bool b, const char* message)
{
    if (!b)
    {
        throw std::runtime_error(message);
    }
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2 || argc == 3, "pass synthetic assembly and optional local JSON output");
        std::ifstream file(argv[1], std::ios::binary);
        Require(bool(file), "fixture exists");
        std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
        auto source = step_semantics::CheckAssemblySource(raw, {}, step_semantics::SourcePolicy::Strict);
        Require(bool(source.document), "complete source assembly");
        auto options = Options::PlanarFixture();
        options.generalBrep = true;
        options.quality = Quality::GeneralDevelopment();
        options.numericalIntegration = NumericalIntegrationSettings{};
        Report report;
        report.source = raw.source;
        report.sourceSha256 = raw.sha256;
        report.generalBrep = true;
        report.numericalIntegration = NumericalIntegrationObservation{};
        Execution execution(options, report);
        std::lock_guard lock(NativeMutex());
        auto native = BuildAssembly(source.document, options, report, execution, 2, 3, 1);
        if (!native)
        {
            throw std::runtime_error(native.error().key);
        }
        Require(
            native->definitions.size() == 2 && native->definitions[0].bodies.size() == 1 && native->definitions[1].bodies.empty(),
            "material definition built once, wrapper owns no fabricated body"
        );
        Require(native->instances.instances.size() == 3, "both occurrences and root retained");
        const auto& body = native->definitions[0].bodies[0];
        Require(
            body->info.materialVolumeMm3 && std::abs(*body->info.materialVolumeMm3 - 6000) < 1e-6 && body->info.faces == 6,
            "independent box volume and face count"
        );
        SmAssertArray pointers, deep;
        Require(
            body->brep->ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0 && body->brep->AssertValid(&deep, SM_LEVEL_2, SM_WALK) &&
                deep.GetSize() == 0,
            "actual constructed native body validity"
        );
        Require(
            report.definitions.size() == 2 && report.definitions[0].checksPassed && report.definitions[1].checksPassed &&
                report.definitions[1].checks->nativeCalls == 0 && report.nativeCalls == report.definitions[0].checks->nativeCalls,
            "wrapper does no native work; shared execution ledger is not multiplied by instances"
        );
        Require(
            report.numericalIntegration->completed && !report.definitions[1].checks->numericalIntegration,
            "container has no fake integral and does not invalidate completed body integration"
        );
        auto evidence = CaptureAssemblyEvidence(*native);
        std::string json;
        Require(
            bool(SerializeAssembly(Status::Accepted, report, options.quality, raw, *evidence, 16 * 1024 * 1024, json)),
            "complete bounded assembly report"
        );
        Require(
            json.find("step-assembly-development.v1") != std::string::npos && json.find("\"parent_instance\":0") != std::string::npos &&
                json.find("\"source_usage\":\"910\"") != std::string::npos && json.find("\"source_usage\":\"916\"") != std::string::npos &&
                json.find("\"length_to_mm\":") != std::string::npos && json.find("\"scope\":\"WithinAssembly\"") != std::string::npos,
            "required occurrence, unit and definition evidence serialized"
        );
        std::string repeated;
        Require(
            SerializeAssembly(Status::Accepted, report, options.quality, raw, *evidence, json.size(), repeated) && repeated == json,
            "exact assembly report budget succeeds deterministically"
        );
        auto shortReport = SerializeAssembly(Status::Accepted, report, options.quality, raw, *evidence, json.size() - 1, repeated);
        Require(
            !shortReport && repeated.empty() && shortReport.error().key == "ImportReportByteLimit",
            "short budget cannot publish a truncated assembly report"
        );
        if (argc == 3)
        {
            std::ofstream(argv[2], std::ios::binary) << json;
        }
        auto incomplete = report;
        incomplete.definitions.pop_back();
        auto missingDefinition = SerializeAssembly(Status::Accepted, incomplete, options.quality, raw, *evidence, 16 * 1024 * 1024, repeated);
        Require(
            !missingDefinition && repeated.empty() && missingDefinition.error().key == "AssemblyReportDefinitionMismatch",
            "accepted report cannot omit a checked definition"
        );
        auto differentRaw = step_source::InspectBytes(text + "\n", step_source::Limits::PlanarFixtures());
        auto mismatched = SerializeAssembly(Status::Accepted, report, options.quality, differentRaw, *evidence, 16 * 1024 * 1024, repeated);
        Require(
            !mismatched && repeated.empty() && mismatched.error().key == "AssemblyReportSourceMismatch",
            "report cannot borrow another source capture"
        );
        std::weak_ptr<ModelData> nativeLifetime = body;
        auto owner = std::move(*native);
        source = {};
        raw = {};
        Require(
            owner.source->Definition(0)->Source().Bytes() == text && owner.definitions[0].bodies[0]->source,
            "candidate owns original source and definition bodies"
        );
        auto limitedOptions = options;
        limitedOptions.nativeLimits.nativeCalls = 1;
        Report failed;
        Execution limited(limitedOptions, failed);
        auto refusal = BuildAssembly(owner.source, limitedOptions, failed, limited, 2, 3, 1);
        Require(!refusal && failed.candidateDiscarded && failed.nativeCalls == 1, "native budget failure releases unpublished assembly candidate");
        auto cancelOptions = options;
        auto control = std::make_shared<ImportControl>();
        cancelOptions.execution.control = control;
        cancelOptions.execution.progress = [control](Progress progress)
        {
            if (progress.phase == Phase::NativeReady)
            {
                control->RequestCancel();
            }
        };
        Report cancelledReport;
        Execution cancelledExecution(cancelOptions, cancelledReport);
        auto cancelled = BuildAssembly(owner.source, cancelOptions, cancelledReport, cancelledExecution, 2, 3, 1);
        Require(
            !cancelled && cancelled.error().status == Status::Cancelled && cancelledReport.candidateDiscarded &&
                cancelledReport.definitions.size() == 2 && cancelledReport.definitions[0].checksPassed &&
                cancelledReport.definitions[0].checks->candidateDiscarded,
            "final native-ready cancellation discards every already built definition"
        );
        std::string multiple = text;
        const auto items = multiple.find("'body',(#21,#931),#12");
        Require(items != std::string::npos, "body items marker");
        multiple.replace(items, std::string("'body',(#21,#931),#12").size(), "'body',(#21,#950,#931),#12");
        multiple.insert(multiple.find("ENDSEC;\nEND-ISO"), "#950=MANIFOLD_SOLID_BREP('',#22);\n");
        auto multipleRaw = step_source::InspectBytes(multiple, step_source::Limits::PlanarFixtures());
        auto multipleSource = step_semantics::CheckAssemblySource(multipleRaw, {}, step_semantics::SourcePolicy::Strict);
        Require(bool(multipleSource.document), "multiple-body source preserves both shape identities");
        auto lateOptions = options;
        lateOptions.nativeLimits.nativeCalls = report.nativeCalls;
        Report lateReport;
        Execution late(lateOptions, lateReport);
        auto lateFailure = BuildAssembly(multipleSource.document, lateOptions, lateReport, late, 2, 3, 1);
        Require(
            !lateFailure && lateReport.candidateDiscarded && lateReport.definitions.size() == 1 &&
                lateReport.definitions[0].checks->bodies.size() == 2 && lateReport.definitions[0].checks->bodies[0].checksPassed &&
                lateReport.definitions[0].checks->bodies[0].checks->candidateDiscarded,
            "later body failure discards earlier successful body with truthful observations"
        );
        owner = {};
        Require(
            nativeLifetime.expired() && evidence->instances.placements.source->SourceFields().Source().Bytes() == text,
            "report evidence retains source metadata without retaining native candidates"
        );
        std::cout << "Native assembly definition candidate contracts passed; no final publication\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
