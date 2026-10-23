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
    options.nativeLimits.nativeCalls = 100000;
    options.reportEncoding = ReportEncoding::CompactV2;
    return options;
}
Options Repair(const std::string& source)
{
    auto options = Config();
    const auto raw = step_source::InspectBytes(source, options.sourceLimits);
    Require(raw.source != nullptr, "synthetic repair source parsed");
    FaceOrientationRepairRequest request;
    request.sourceSha256 = raw.sha256;
    request.maximumFaceEdits = 1;
    request.maximumProofControls = 10000;
    request.maximumProofProducts = 1000000;
    request.edits.push_back({ "7", "21", "517", false, true });
    options.faceOrientationRepair = request;
    return options;
}
void Archive(const ModelSnapshot& model, const std::filesystem::path& path)
{
    model.WriteNative(path);
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
        "repaired native archive reload"
    );
    Require(bodies.GetSize() == 1 && curves.GetSize() == 0 && surfaces.GetSize() == 0 && trees.GetSize() == 0, "complete repaired archive");
    SmAssertArray pointers, deep;
    Require(
        bodies[0]->ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0 && bodies[0]->AssertValid(&deep, SM_LEVEL_2, SM_WALK) &&
            deep.GetSize() == 0,
        "repaired archive validity"
    );
    auto editable = model.CopyForModeling();
    Require(editable.status == Status::Accepted && editable.model, "repaired modeling copy");
    editable.model->Edit(
        [&](SmBrep& candidate)
        {
            step_import_test::CompareNativeDefinitions(candidate, *bodies[0]);
        }
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public roof-box fixture/output required");
        std::ifstream stream(argv[1], std::ios::binary);
        const std::string original{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        auto source = original;
        const std::string before = "#517=ADVANCED_FACE('',(#516),#514,.T.);";
        const auto at = source.find(before);
        Require(at != std::string::npos, "public Bernstein roof face");
        source.replace(at, before.size(), "#517=ADVANCED_FACE('',(#516),#514,.F.);");
        const auto strict = ImportBytes(source, Config());
        Require(strict.status == Status::PostcheckFailure && !strict.model, "default cannot repair wrong source sense");
        auto options = Repair(source);
        std::size_t captures = 0, repairBoundaries = 0;
        options.execution.progress = [&](Progress p)
        {
            captures += p.phase == Phase::BeforeCapture;
            repairBoundaries += p.phase == Phase::RepairReady;
        };
        auto result = ImportBytes(source, options);
        Require(
            result.status == Status::AcceptedWithRepairs && result.model && captures == 1 && repairBoundaries == 1,
            "one capture, explicit repaired qualification"
        );
        Require(
            result.report.source && result.report.source->Bytes() == source && result.model->Source()->Source().Bytes() == source,
            "repair retains actual immutable source bytes"
        );
        const auto history = result.model->Repairs();
        Require(
            history && history->actions.size() == 1 && history->definitionsIdentical && history->workingTolerancesIdentical &&
                !history->sourceInterpretationApplied && history->verticesCompared == 8 && history->edgesCompared == 12 &&
                history->surfacesCompared == 6,
            "source-aligned zero-geometry proof"
        );
        Require(
            history->actions[0].symmetricDeterminantLower > 0 && history->actions[0].continuousLiftBoundMm <= .001 &&
                history->originalDiagnostics.at(0).source.key == "GeneralPointerValidation",
            "repair owns its original failure and continuous evidence"
        );
        Require(
            result.report.originalAttempt && result.report.originalAttempt->candidateDiscarded && result.report.repairProofProducts > 0,
            "original failed candidate disposal and cumulative proof ledger"
        );
        Near(*result.model->MaterialVolumeMm3(), 6000 + 4000. / 9, .02, "independent Bernstein roof volume");
        Require(
            result.model->ClassifyMaterial({ 7, 7, 20 }) == MaterialPointClass::Inside &&
                result.model->ClassifyMaterial({ 100, 100, 100 }) == MaterialPointClass::Outside,
            "repaired material queries"
        );
        std::filesystem::create_directories(argv[2]);
        Archive(*result.model, std::filesystem::path(argv[2]) / "repair.smb");
        auto model = result.model;
        auto face = model->Face(history->actions[0].targetFace);
        const auto reportBytes = result.report.json.size();
        const auto firstCalls = result.report.originalAttempt->nativeCalls;
        result = {};
        Require(
            model->Repairs()->sourceSha256 == options.faceOrientationRepair->sourceSha256 && face.OrientationRepair() &&
                !face.OrientationRepair()->edit.expectedSourceSameSense && face.OrientationRepair()->edit.targetSameSense,
            "model and face own history after outcome/report disposal"
        );
        model.reset();
        Require(face.OrientationRepair().has_value(), "face alone keeps repair provenance");
        for (const auto stopAt : { std::size_t(0), std::size_t(1), std::size_t(7), firstCalls - 1 })
        {
            for (const bool cancel : { false, true })
            {
                auto stop = Repair(source);
                stop.execution.control = std::make_shared<ImportControl>();
                if (cancel)
                {
                    stop.execution.progress = [&](Progress p)
                    {
                        if (p.phase == Phase::BeforeNativeCall && p.nativeCalls == stopAt)
                        {
                            stop.execution.control->RequestCancel();
                        }
                    };
                }
                else
                {
                    stop.nativeLimits.nativeCalls = stopAt;
                }
                const auto failed = ImportBytes(source, stop);
                const auto expected = cancel ? Status::Cancelled : Status::ResourceLimit;
                const auto key = cancel ? "ImportCancelled" : "NativeCallLimit";
                Require(
                    failed.status == expected && !failed.model && failed.report.originalAttempt && failed.report.originalAttemptStatus == expected &&
                        failed.report.repairStage == "StrictCandidate",
                    "execution refusal during the original attempt cannot become RepairNotRequired or start a repair"
                );
                const auto& originalAttempt = *failed.report.originalAttempt;
                Require(
                    failed.report.nativeCalls == stopAt && originalAttempt.nativeCalls == stopAt &&
                        originalAttempt.candidateDiscarded == (stopAt > 0) && failed.report.repairProofProducts == 0 &&
                        originalAttempt.diagnostics.back().source.key == key && failed.report.diagnostics.back().source.key == key &&
                        !failed.report.json.empty(),
                    "strict-attempt refusal retains overlapping work, original diagnostics and complete failure report"
                );
            }
        }
        for (auto phase : { Phase::BeforeRepairProof, Phase::BeforeRepair, Phase::RepairReady, Phase::BeforeCommit })
        {
            auto stop = Repair(source);
            stop.execution.control = std::make_shared<ImportControl>();
            stop.execution.progress = [&](Progress p)
            {
                if (p.phase == phase)
                {
                    stop.execution.control->RequestCancel();
                }
            };
            const auto cancelled = ImportBytes(source, stop);
            Require(
                cancelled.status == Status::Cancelled && !cancelled.model && cancelled.report.candidateDiscarded,
                "repair cancellation never publishes either candidate"
            );
        }
        auto cap = Repair(source);
        cap.nativeLimits.nativeCalls = firstCalls;
        const auto capped = ImportBytes(source, cap);
        Require(capped.status == Status::ResourceLimit && !capped.model, "native ledger does not restart at repair");
        cap = Repair(source);
        cap.faceOrientationRepair->maximumProofProducts = 1;
        const auto proofCapped = ImportBytes(source, cap);
        Require(proofCapped.status == Status::ResourceLimit && !proofCapped.model, "proof exhaustion is not a repair pass");
        cap = Repair(source);
        cap.nativeLimits.reportBytes = reportBytes - 1;
        const auto overflow = ImportBytes(source, cap);
        Require(overflow.status == Status::ResourceLimit && !overflow.model, "mandatory repair report precedes publication");
        auto stale = Repair(source);
        stale.faceOrientationRepair->sourceSha256[0] = stale.faceOrientationRepair->sourceSha256[0] == '0' ? '1' : '0';
        const auto mismatch = ImportBytes(source, stale);
        Require(
            mismatch.status == Status::Unsupported && !mismatch.model && mismatch.report.nativeCalls == 0,
            "stale plan rejected before native work"
        );
        auto wrong = Repair(source);
        wrong.faceOrientationRepair->edits[0].definitionId = "999";
        const auto unselected = ImportBytes(source, wrong);
        Require(unselected.status != Status::AcceptedWithRepairs && !unselected.model, "unselected edit cannot become a partial success");
        auto validPlan = Repair(original);
        validPlan.faceOrientationRepair->edits[0].expectedSourceSameSense = true;
        validPlan.faceOrientationRepair->edits[0].targetSameSense = false;
        const auto valid = ImportBytes(original, validPlan);
        Require(valid.status == Status::Unsupported && !valid.model, "already valid definitions cannot drift under repair");
        auto shared = source;
        // The independent-product source path is qualified for this fixed
        // AP214 carrier, not for the closed N2635 singleton profile above.
        const std::string schema = "AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}";
        shared.replace(shared.find(schema), schema.size(), "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}");
        const std::string application = "'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011";
        shared.replace(shared.find(application), application.size(), "'automotive_design',2010");
        const auto closing = shared.rfind("ENDSEC;");
        Require(closing != std::string::npos, "public document carrier");
        shared.insert(
            closing,
            "#900=PRODUCT('second','independent definition',$,(#3));\n"
            "#901=PRODUCT_DEFINITION_FORMATION('B',$,#900);\n"
            "#902=PRODUCT_DEFINITION('D2',$,#901,#6);\n"
            "#903=PRODUCT_DEFINITION_SHAPE('',$,#902);\n"
            "#904=SHAPE_DEFINITION_REPRESENTATION(#903,#920);\n"
            "#920=ADVANCED_BREP_SHAPE_REPRESENTATION('child',(#21),#12);\n"
        );
        auto oneDefinition = Repair(shared);
        const auto incomplete = ImportDocumentBytes(shared, oneDefinition);
        Require(
            incomplete.status != Status::AcceptedWithRepairs && !incomplete.document && incomplete.report.candidateDiscarded,
            "repairing one shared definition never mutates or publishes its unrepaired sibling"
        );
        auto both = Repair(shared);
        both.faceOrientationRepair->maximumFaceEdits = 2;
        both.faceOrientationRepair->edits.push_back({ "902", "21", "517", false, true });
        const auto document = ImportDocumentBytes(shared, both);
        Require(
            document.status == Status::AcceptedWithRepairs && document.document && document.document->PartCount() == 2 &&
                document.document->Body(0)->Repairs()->actions[0].edit.definitionId == "7" &&
                document.document->Body(1)->Repairs()->actions[0].edit.definitionId == "902",
            "shared geometry repairs are definition-scoped under one complete publication"
        );
        Require(
            document.report.repairProofProducts > 0 && document.report.repairProofProducts <= both.faceOrientationRepair->maximumProofProducts,
            "document proof work is request cumulative"
        );
        oneDefinition.definitionSelection = DefinitionSelectionRequest{ oneDefinition.faceOrientationRepair->sourceSha256, { "7" } };
        const auto selected = ImportDocumentBytes(shared, oneDefinition);
        Require(
            selected.status == Status::AcceptedWithRepairs && selected.document && selected.document->PartCount() == 1 &&
                selected.document->Source()->PartCount() == 2 &&
                selected.document->Selection()->excludedDefinitionIds == std::vector<std::string>{ "902" } &&
                selected.document->Body(0)->Repairs()->actions[0].edit.definitionId == "7",
            "explicit selected request owns repair and not-requested sibling evidence separately"
        );
        both.definitionSelection = oneDefinition.definitionSelection;
        const auto outside = ImportDocumentBytes(shared, both);
        Require(
            outside.status == Status::Unsupported && !outside.document && outside.report.nativeCalls == 0 && !outside.report.definitionSelection,
            "repair plan cannot edit unselected definitions"
        );
        std::cout << "Explicit orientation repair contracts passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
