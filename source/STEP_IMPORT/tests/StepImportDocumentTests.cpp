// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <SmAssertArray.h>
#include <SmBrepData.h>
#include <SmNativeArchive.h>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace step_import;
using step_import_test::Near;
using step_import_test::Require;
namespace
{
Options Config()
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    options.reportEncoding = ReportEncoding::CompactV2;
    return options;
}
void Archive(const DocumentSnapshot& document, const std::filesystem::path& path)
{
    document.WriteNative(path);
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> bodies;
    SmObjsDelete<SmCurve*> deleteCurves(&curves);
    SmObjsDelete<SmSurface*> deleteSurfaces(&surfaces);
    SmObjsDelete<SmBrep*> deleteBodies(&bodies);
    Require(
        SmNativeArchive::ReadPartFromFile(context, path.c_str(), curves, surfaces, trees, bodies, SM_BINARY) == SM_SUCCESS,
        "document archive reload"
    );
    Require(
        bodies.GetSize() == document.BodyCount() && curves.GetSize() == 0 && surfaces.GetSize() == 0 && trees.GetSize() == 0,
        "ordered bodies without Boolean union"
    );
    for (std::size_t i = 0; i < document.BodyCount(); ++i)
    {
        auto& loaded = *bodies[static_cast<ULONG>(i)];
        SmAssertArray pointers, deep;
        Require(
            loaded.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0 && loaded.AssertValid(&deep, SM_LEVEL_2, SM_WALK) &&
                deep.GetSize() == 0,
            "reloaded body validity"
        );
        auto copy = document.Body(i)->CopyForModeling();
        Require(copy.status == Status::Accepted && copy.model, "independent body modeling copy");
        copy.model->Edit(
            [&](SmBrep& original)
            {
                step_import_test::CompareNativeDefinitions(original, loaded);
            }
        );
    }
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 4, "synthetic fixtures and output required");
        std::ifstream stream(argv[1], std::ios::binary);
        const std::string text{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        auto options = Config();
        std::size_t captures = 0;
        options.execution.progress = [&](Progress p)
        {
            captures += p.phase == Phase::BeforeCapture;
        };
        auto result = ImportDocumentBytes(text, options);
        Require(result.status == Status::Accepted && result.document && captures == 1, "all independent definitions from one source capture");
        auto document = result.document;
        Require(document->PartCount() == 2 && document->BodyCount() == 2, "definitions and bodies retained");
        Require(
            document->SourceDefinitionId(0) == "7" && document->SourceDefinitionId(1) == "902" && document->SourceShapeId(0) == "21" &&
                document->SourceShapeId(1) == "21",
            "definition identity is separate from shared source geometry"
        );
        Require(document->BodyPartIndex(0) == 0 && document->BodyPartIndex(1) == 1, "body to definition correspondence");
        Near(*document->Body(0)->MaterialVolumeMm3(), 6000, 1e-6, "first box independent volume");
        Near(*document->Body(1)->MaterialVolumeMm3(), 6000000, 1e-3, "centimetre context physical volume");
        Near(document->Body(0)->Info().areaMm2, 2200, 1e-6, "first analytic area");
        Near(document->Body(1)->Info().areaMm2, 220000, 1e-4, "second analytic area");
        Require(
            document->Body(0)->Source()->UnitContext().lengthUnit != document->Body(1)->Source()->UnitContext().lengthUnit,
            "units remain definition-scoped"
        );
        Require(&document->Body(0)->Source()->Source() == &document->Body(1)->Source()->Source(), "one immutable source/type-binding owner");
        const auto firstCalls = result.report.definitions.at(0).checks->nativeCalls;
        Require(result.report.nativeCalls == firstCalls + result.report.definitions.at(1).checks->nativeCalls, "request-wide native call ledger");
        const auto bytes = result.report.json.size();
        result = {};
        Require(document->Source()->Source().Bytes() == text, "document owns source after outcome disposal");
        std::filesystem::create_directories(argv[2]);
        Archive(*document, std::filesystem::path(argv[2]) / "document.smb");
        auto edited = document->Body(0)->CopyForModeling();
        Require(edited.status == Status::Accepted && edited.model, "document modeling copy");
        edited.model->Edit(
            [](SmBrep& body)
            {
                Require(
                    body.Transform(SmAxis2Placement(SmPoint3d(1000, 0, 0), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0)), nullptr, FALSE) == SM_SUCCESS,
                    "document copy transform"
                );
            }
        );
        Require(
            document->Body(0)->ClassifyMaterial({ 7, 7, 20 }) == MaterialPointClass::Inside &&
                document->Body(1)->ClassifyMaterial({ 70, 70, 200 }) == MaterialPointClass::Inside,
            "document definition copy mutation isolation"
        );
        const auto legacy = ImportPartBytes(text, Config());
        Require(
            legacy.status == Status::Unsupported && std::holds_alternative<std::monostate>(legacy.part),
            "legacy singular request never drops a definition"
        );
        auto cap = Config();
        cap.nativeLimits.nativeCalls = firstCalls;
        const auto capped = ImportDocumentBytes(text, cap);
        Require(
            capped.status == Status::ResourceLimit && !capped.document && capped.report.candidateDiscarded,
            "later-body budget releases the whole document"
        );
        auto between = Config();
        between.execution.control = std::make_shared<ImportControl>();
        between.execution.progress = [&](Progress p)
        {
            if (p.phase == Phase::BeforeNativeCall && p.nativeCalls == firstCalls)
            {
                between.execution.control->RequestCancel();
            }
        };
        const auto interrupted = ImportDocumentBytes(text, between);
        Require(
            interrupted.status == Status::Cancelled && !interrupted.document && interrupted.report.definitions[0].checksPassed,
            "cancellation between definitions releases unpublished earlier bodies"
        );
        auto reporting = Config();
        reporting.nativeLimits.reportBytes = bytes - 1;
        const auto exhausted = ImportDocumentBytes(text, reporting);
        Require(exhausted.status == Status::ResourceLimit && !exhausted.document, "mandatory report precedes document publication");
        for (auto phase : { Phase::NativeReady, Phase::BeforeCommit })
        {
            auto cancel = Config();
            cancel.execution.control = std::make_shared<ImportControl>();
            cancel.execution.progress = [&](Progress p)
            {
                if (p.phase == phase)
                {
                    cancel.execution.control->RequestCancel();
                }
            };
            const auto stopped = ImportDocumentBytes(text, cancel);
            Require(
                stopped.status == Status::Cancelled && !stopped.document && stopped.report.candidateDiscarded,
                "late cancellation publishes no document"
            );
        }
        auto tiny = text;
        const std::string prefix = "SI_UNIT(.CENTI.,.METRE.)";
        const auto at = tiny.find(prefix);
        Require(at != std::string::npos, "second context fixture");
        tiny.replace(at, prefix.size(), "SI_UNIT(.ATTO.,.METRE.)");
        const auto later = ImportDocumentBytes(tiny, Config());
        Require(
            later.status != Status::Accepted && !later.document && later.report.definitions.size() == 2 && later.report.definitions[0].checksPassed &&
                later.report.candidateDiscarded,
            "later numerical failure cannot deliver earlier definitions"
        );
        std::ifstream distinctStream(argv[3], std::ios::binary);
        const std::string distinctText{ std::istreambuf_iterator<char>(distinctStream), std::istreambuf_iterator<char>() };
        auto distinct = ImportDocumentBytes(distinctText, Config());
        Require(
            distinct.status == Status::Accepted && distinct.document && distinct.document->BodyCount() == 2 &&
                distinct.document->SourceShapeId(0) == "21" && distinct.document->SourceShapeId(1) == "10021",
            "each definition selects its actual source geometry"
        );
        Near(distinct.document->Body(1)->Info().boundsMm[0][0], 102, 1e-8, "second definition retains its independent origin");
        Archive(*distinct.document, std::filesystem::path(argv[2]) / "distinct.smb");
        auto mixedText = distinctText;
        const std::string items = "'body',(#21)";
        mixedText.replace(mixedText.find(items), items.size(), "'body',(#21,#10021)");
        auto mixed = ImportDocumentBytes(mixedText, Config());
        Require(
            mixed.status == Status::Accepted && mixed.document && mixed.document->PartCount() == 2 && mixed.document->BodyCount() == 3 &&
                mixed.document->BodyPartIndex(0) == 0 && mixed.document->BodyPartIndex(1) == 0 && mixed.document->BodyPartIndex(2) == 1,
            "document combines complete per-definition material collections without flattening identity"
        );
        Archive(*mixed.document, std::filesystem::path(argv[2]) / "mixed.smb");
        std::cout << "independent document contracts passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
