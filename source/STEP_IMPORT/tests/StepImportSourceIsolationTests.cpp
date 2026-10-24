// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"
#include "StepImportDefinitionSelection.h"
#include "StepImportExecution.h"

#include <SmAssertArray.h>
#include <SmNativeArchive.h>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace step_import;
using step_import_test::Require;
namespace
{
void Replace(std::string& text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    Require(at != std::string::npos, "public mutation token");
    text.replace(at, from.size(), to);
}
Options Config(const std::string& text)
{
    auto result = Options::PlanarFixture();
    result.generalBrep = true;
    result.quality = Quality::GeneralDevelopment();
    result.reportEncoding = ReportEncoding::CompactV2;
    const auto raw = step_source::InspectBytes(text, result.sourceLimits);
    result.definitionSelection = DefinitionSelectionRequest{ raw.sha256, { "7" } };
    result.sourceErrorIsolation = SourceErrorIsolationLimits{ 4, 100000 };
    return result;
}
void Archive(const DocumentSnapshot& doc, const std::filesystem::path& file)
{
    doc.WriteNative(file);
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> bodies;
    SmObjsDelete<SmCurve*> dc(&curves);
    SmObjsDelete<SmSurface*> ds(&surfaces);
    SmObjsDelete<SmBrep*> db(&bodies);
    Require(
        SmNativeArchive::ReadPartFromFile(context, file.c_str(), curves, surfaces, trees, bodies, SM_BINARY) == SM_SUCCESS && bodies.GetSize() == 1,
        "selected document archive contains only the requested body"
    );
    SmAssertArray pointers, deep;
    Require(
        bodies[0]->ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0 && bodies[0]->AssertValid(&deep, SM_LEVEL_2, SM_WALK) &&
            deep.GetSize() == 0,
        "selected archive native validity"
    );
    auto copy = doc.Body(0)->CopyForModeling();
    Require(copy.status == Status::Accepted && copy.model, "independent selected modeling copy");
    copy.model->Edit(
        [&](SmBrep& native)
        {
            step_import_test::CompareNativeDefinitions(native, *bodies[0]);
        }
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public distinct fixture and local output required");
        std::ifstream in(argv[1], std::ios::binary);
        const std::string original{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        auto text = original;
        Replace(text, "#10507=ADVANCED_FACE('',(#10506)", "#10507=ADVANCED_FACE('',(#10506,#990000)");
        text.insert(text.rfind("ENDSEC;"), "#990000=FACE_OUTER_BOUND('',#10505,.T.);\n");
        auto options = Config(text);
        auto completeSource = options;
        completeSource.sourceErrorIsolation.reset();
        const auto refused = ImportDocumentBytes(text, completeSource);
        Require(
            refused.status == Status::Rejected && !refused.document && refused.report.nativeCalls == 0,
            "old native-only selection cannot skip a source error"
        );
        auto result = ImportDocumentBytes(text, options);
        Require(
            result.status == Status::AcceptedWithSourceIsolation && result.document && result.document->PartCount() == 1 &&
                result.document->BodyCount() == 1 && result.document->SourceDefinitionId(0) == "7",
            "complete selected native publication with distinct isolation status"
        );
        auto doc = result.document;
        const auto bytes = result.report.json.size();
        const auto singleCalls = result.report.nativeCalls;
        result = {};
        auto staleSelection = options;
        staleSelection.definitionSelection->definitionIds = { "902" };
        Report separateReport;
        separateReport.sourceSha256 = doc->Source()->Source().Sha256();
        detail::Execution selectionExecution(staleSelection, separateReport);
        const auto mismatched = detail::SelectNativeDefinitions(*doc->Source(), staleSelection, separateReport, selectionExecution);
        Require(
            !mismatched && mismatched.error().status == Status::Unsupported && mismatched.error().key == "SourceSelectionReceiptMismatch" &&
                mismatched.error().stage == "Selection" && separateReport.nativeCalls == 0,
            "owning source receipt returns an error value for a different native request"
        );
        auto model = doc->Body(0);
        step_import_test::Near(*model->MaterialVolumeMm3(), 6000, 1e-6, "independent exact box volume");
        step_import_test::Near(model->Info().areaMm2, 2200, 1e-6, "independent exact box area");
        Require(
            model->ClassifyMaterial({ 7, 7, 20 }) == MaterialPointClass::Inside &&
                model->ClassifyMaterial({ 200, 0, 0 }) == MaterialPointClass::Outside,
            "selected exact material queries"
        );
        Require(
            doc->Selection()->configuration == step_semantics::SourceFaceClauseSelectionProfile &&
                doc->Selection()->sourceScope == "SelectedDefinitionsOnly" && doc->Selection()->sourceDefinitionIds.size() == 2 &&
                model->Source()->Source().Bytes() == text && model->Source()->SourceSelection()->failures.size() == 1,
            "owning original input, excluded failure and full declaration inventory survive report disposal"
        );
        std::filesystem::create_directories(argv[2]);
        Archive(*doc, std::filesystem::path(argv[2]) / "selected.smb");
        auto surfaceText = text;
        Replace(surfaceText, "#21=MANIFOLD_SOLID_BREP('',#22)", "#21=SHELL_BASED_SURFACE_MODEL('',(#22))");
        Replace(surfaceText, "#20=ADVANCED_BREP_SHAPE_REPRESENTATION", "#20=MANIFOLD_SURFACE_SHAPE_REPRESENTATION");
        const auto surface = ImportDocumentBytes(surfaceText, Config(surfaceText));
        // Source isolation and native construction retain the selected surface
        // role. Geometric closure grants neither material volume nor InsideSolid.
        Require(
            surface.status == Status::AcceptedWithSourceIsolation && surface.document && surface.document->BodyCount() == 1 &&
                surface.report.generalSource && surface.report.generalSource->document &&
                surface.report.generalSource->document->Part(0)->PartDefinition().shapeRole == "ClosedSurfaceModel" &&
                surface.document->Body(0)->Info().shapeRole == "ClosedSurfaceModel" && !surface.document->Body(0)->MaterialVolumeMm3() &&
                !surface.document->Body(0)->ClassifyMaterial({ 7, 7, 20 }) &&
                surface.report.definitions.at(0).checks->nativeRole->materialRegions == 0 &&
                surface.report.definitions.at(0).checks->nativeRole->boundedVoidRegions == 1,
            "selected closed surface retains its source role without material promotion"
        );
        auto three = text;
        three.insert(
            three.rfind("ENDSEC;"),
            "#1900=PRODUCT('third','shared definition',$,(#3));\n"
            "#1901=PRODUCT_DEFINITION_FORMATION('C',$,#1900);\n"
            "#1902=PRODUCT_DEFINITION('D3',$,#1901,#6);\n"
            "#1903=PRODUCT_DEFINITION_SHAPE('',$,#1902);\n"
            "#1904=SHAPE_DEFINITION_REPRESENTATION(#1903,#1920);\n"
            "#1920=ADVANCED_BREP_SHAPE_REPRESENTATION('third',(#21),#12);\n"
        );
        auto multiple = Config(three);
        multiple.definitionSelection->definitionIds = { "1902", "7" };
        const auto allSelected = ImportDocumentBytes(three, multiple);
        Require(
            allSelected.status == Status::AcceptedWithSourceIsolation && allSelected.document && allSelected.document->PartCount() == 2 &&
                allSelected.document->BodyCount() == 2 && allSelected.document->SourceDefinitionId(0) == "7" &&
                allSelected.document->SourceDefinitionId(1) == "1902",
            "all requested independent definitions publish in source order without deduplicating shared geometry"
        );
        multiple.nativeLimits.nativeCalls = singleCalls + 1;
        const auto laterFailure = ImportDocumentBytes(three, multiple);
        Require(
            laterFailure.status == Status::ResourceLimit && !laterFailure.document && laterFailure.report.definitions.size() == 2 &&
                laterFailure.report.definitions[0].checksPassed && laterFailure.report.candidateDiscarded,
            "failure in a later selected definition never publishes the first completed body"
        );
        auto wrong = options;
        wrong.definitionSelection->definitionIds = { "902" };
        const auto selectedBad = ImportDocumentBytes(text, wrong);
        Require(
            selectedBad.status == Status::Rejected && !selectedBad.document && selectedBad.report.nativeCalls == 0,
            "selected failed source remains blocked"
        );
        auto proof = options;
        proof.sourceErrorIsolation->maximumInfluenceVisits = 1;
        const auto limited = ImportDocumentBytes(text, proof);
        Require(
            limited.status == Status::ResourceLimit && !limited.document && limited.report.nativeCalls == 0,
            "influence failure precedes every native allocation"
        );
        auto budget = options;
        budget.nativeLimits.nativeCalls = 1;
        const auto stopped = ImportDocumentBytes(text, budget);
        Require(
            stopped.status == Status::ResourceLimit && !stopped.document && stopped.report.candidateDiscarded,
            "native failure releases unpublished selected candidates"
        );
        auto report = options;
        report.nativeLimits.reportBytes = bytes - 1;
        const auto overflow = ImportDocumentBytes(text, report);
        Require(
            overflow.status == Status::ResourceLimit && !overflow.document && overflow.report.candidateDiscarded,
            "required isolation receipt precedes publication"
        );
        for (const auto phase : { Phase::SemanticsChecked, Phase::NativeReady, Phase::BeforeCommit })
        {
            auto cancel = options;
            cancel.execution.control = std::make_shared<ImportControl>();
            cancel.execution.progress = [&](Progress p)
            {
                if (p.phase == phase)
                {
                    cancel.execution.control->RequestCancel();
                }
            };
            const auto cancelled = ImportDocumentBytes(text, cancel);
            Require(
                cancelled.status == Status::Cancelled && !cancelled.document,
                "selected isolation obeys the existing cancellation commit boundary"
            );
        }
        const auto clean = ImportDocumentBytes(original, Config(original));
        Require(
            clean.status == Status::Accepted && clean.document && clean.document->Selection()->isolatedSourceFailureCount == 0,
            "no-op source policy invents no isolated-error qualification"
        );
        std::cout << "native source isolation contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
