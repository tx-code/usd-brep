// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <SmAssertArray.h>
#include <SmBrepData.h>
#include <SmNativeArchive.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>
using namespace step_import;
using step_import_test::Require;
namespace
{
Options Config(const std::string& source)
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    const auto raw = step_source::InspectBytes(source, options.sourceLimits);
    Require(bool(raw.source), "public original source captures");
    options.cylindricalBandRepair = CylindricalBandRepairRequest{ raw.sha256, 1, 2, 100000, { { "7", "21", "182", { "172", "173" } } } };
    return options;
}
void Replace(std::string& text, const std::string& old, const std::string& value)
{
    const auto at = text.find(old);
    Require(at != std::string::npos, "public mutation token");
    text.replace(at, old.size(), value);
}
void Archive(const ModelSnapshot& model, const std::filesystem::path& path)
{
    model.WriteNative(path);
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> bodies;
    SmObjsDelete<SmCurve*> dc(&curves);
    SmObjsDelete<SmSurface*> ds(&surfaces);
    SmObjsDelete<SmBrep*> db(&bodies);
    Require(
        SmNativeArchive::ReadPartFromFile(context, path.c_str(), curves, surfaces, trees, bodies, SM_BINARY) == SM_SUCCESS && bodies.GetSize() == 1,
        "band archive complete"
    );
    SmAssertArray pointers, deep;
    Require(
        bodies[0]->ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0 && bodies[0]->AssertValid(&deep, SM_LEVEL_2, SM_WALK) &&
            deep.GetSize() == 0,
        "band archive L2"
    );
    auto copy = model.CopyBrepForModeling();
    Require(copy.status == Status::Accepted && copy.model, "band modeling copy");
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
        Require(argc == 3, "public fixture and native archive directory required");
        std::ifstream input(argv[1], std::ios::binary);
        const std::string source{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        auto options = Config(source);
        auto strict = options;
        strict.cylindricalBandRepair.reset();
        const auto rejected = ImportBytes(source, strict);
        Require(
            rejected.status == Status::Rejected && !rejected.model && rejected.report.nativeCalls == 0,
            "default source FACE.WR2 rejection preserved"
        );
        auto result = ImportBytes(source, options);
        Require(result.status == Status::AcceptedWithRepairs && result.model, "explicit source-bound band repair");
        auto model = result.model;
        const auto reportSize = result.report.json.size();
        result = {};
        const auto history = model->CylindricalBandRepairs();
        Require(
            history && history->actions.size() == 1 && history->definitionsIdentical && history->workingTolerancesIdentical &&
                history->originalSourceDiagnostics.at(0).source.key == "SourceRuleViolation:FACE.WR2" && !history->sourceInterpretationApplied &&
                model->Source()->Source().Bytes() == source,
            "owning original rejection and exact identity after report disposal"
        );
        const auto originalBound = model->Source()->Source().Find("172");
        Require(
            originalBound &&
                model->Source()->IsA(static_cast<std::size_t>(originalBound - model->Source()->Source().Records().data()), "FACE_OUTER_BOUND"),
            "source marker is not rewritten"
        );
        step_import_test::Near(
            *model->MaterialVolumeMm3(),
            63 * std::numbers::pi,
            63 * std::numbers::pi * 2e-4,
            "independent analytic cylinder volume"
        );
        step_import_test::Near(model->Info().areaMm2, 60 * std::numbers::pi, 60 * std::numbers::pi * 2e-4, "independent analytic cylinder area");
        Require(
            model->ClassifyMaterial({ 0, 0, 3 }) == MaterialPointClass::Inside && model->ClassifyMaterial({ 5, 0, 3 }) == MaterialPointClass::Outside,
            "band native queries"
        );
        std::filesystem::create_directories(argv[2]);
        Archive(*model, std::filesystem::path(argv[2]) / "repaired.smb");
        auto shared = source;
        shared.insert(
            shared.rfind("ENDSEC;"),
            "#900=PRODUCT('second','independent definition',$,(#3));\n"
            "#901=PRODUCT_DEFINITION_FORMATION('B',$,#900);\n"
            "#902=PRODUCT_DEFINITION('D2',$,#901,#6);\n"
            "#903=PRODUCT_DEFINITION_SHAPE('',$,#902);\n"
            "#904=SHAPE_DEFINITION_REPRESENTATION(#903,#920);\n"
            "#920=ADVANCED_BREP_SHAPE_REPRESENTATION('child',(#21),#12);\n"
        );
        auto sharedOptions = Config(shared);
        const auto incomplete = ImportDocumentBytes(shared, sharedOptions);
        Require(!incomplete.document && incomplete.report.nativeCalls == 0, "one planned source use cannot release a shared unplanned definition");
        sharedOptions.cylindricalBandRepair->maximumFaces = 2;
        sharedOptions.cylindricalBandRepair->maximumBoundEdits = 4;
        sharedOptions.cylindricalBandRepair->faces.push_back({ "902", "21", "182", { "172", "173" } });
        const auto sharedResult = ImportDocumentBytes(shared, sharedOptions);
        Require(
            sharedResult.status == Status::AcceptedWithRepairs && sharedResult.document && sharedResult.document->PartCount() == 2,
            "all shared definition uses are repaired under one complete publication"
        );
        for (std::size_t i = 0; i < 2; ++i)
        {
            const auto scoped = sharedResult.document->Body(i)->CylindricalBandRepairs();
            Require(
                scoped && scoped->actions.size() == 1 && scoped->originalSourceDiagnostics.size() == 1 &&
                    scoped->actions[0].edit.definitionId == (i == 0 ? "7" : "902"),
                "shared entity failures remain owning evidence for each scoped action"
            );
        }
        auto nested = source;
        Replace(nested, "#152=ORIENTED_EDGE('',*,*,#130,.T.)", "#152=ORIENTED_EDGE('',*,*,#150,.T.)");
        const auto nestedResult = ImportBytes(nested, Config(nested));
        Require(
            nestedResult.status == Status::Indeterminate && !nestedResult.model && nestedResult.report.nativeCalls == 0 &&
                nestedResult.report.json.find("CylindricalBandWholeCircleRequired") != std::string::npos,
            "unsupported nested edge stays within the structured report contract"
        );
        auto metadata = source;
        Replace(metadata, "PRODUCT_DEFINITION_CONTEXT('part definition'", "PRODUCT_DEFINITION_CONTEXT('unrecognized caption'");
        auto metadataOptions = Config(metadata);
        metadataOptions.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
        const auto metadataResult = ImportBytes(metadata, metadataOptions);
        Require(
            metadataResult.status == Status::AcceptedWithRepairs && metadataResult.model &&
                metadataResult.report.generalSource->part == "DeferredExplicitRepair",
            "preserved metadata cannot hide the pending defining source repair"
        );
        for (const auto change : { 0, 1, 2, 3 })
        {
            auto broken = source;
            if (change == 0)
            {
                Replace(broken, "CYLINDRICAL_SURFACE('',#106,3.)", "CYLINDRICAL_SURFACE('',#106,3.001)");
            }
            if (change == 1)
            {
                Replace(broken, "#154=ORIENTED_EDGE('',*,*,#131,.F.)", "#154=ORIENTED_EDGE('',*,*,#131,.T.)");
            }
            if (change == 2)
            {
                Replace(broken, "#101=CARTESIAN_POINT('',(0.,0.,7.))", "#101=CARTESIAN_POINT('',(0.001,0.,7.))");
            }
            if (change == 3)
            {
                Replace(broken, "#173=FACE_OUTER_BOUND", "#173=FACE_BOUND");
            }
            const auto failed = ImportBytes(broken, Config(broken));
            Require(
                !failed.model && failed.report.nativeCalls == 0,
                "wrong radius/winding/coaxiality or unnecessary plan does not enter native build"
            );
        }
        auto stale = options;
        stale.cylindricalBandRepair->sourceSha256 = std::string(64, '0');
        const auto mismatch = ImportBytes(source, stale);
        Require(mismatch.status == Status::Unsupported && !mismatch.model && mismatch.report.nativeCalls == 0, "stale source repair rejected");
        auto cap = options;
        cap.cylindricalBandRepair->maximumProofOperations = 1;
        const auto exhausted = ImportBytes(source, cap);
        Require(
            exhausted.status == Status::ResourceLimit && !exhausted.model && exhausted.report.nativeCalls == 0,
            "exact proof budget cannot fall back to sampled evidence"
        );
        auto report = options;
        report.nativeLimits.reportBytes = reportSize - 1;
        const auto overflow = ImportBytes(source, report);
        Require(
            overflow.status == Status::ResourceLimit && !overflow.model && overflow.report.candidateDiscarded,
            "mandatory role history must fit before publication"
        );
        for (const auto phase : { Phase::BeforeRepairProof, Phase::BeforeRepair, Phase::BeforeNativeCall, Phase::RepairReady, Phase::BeforeCommit })
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
            const auto stopped = ImportBytes(source, cancel);
            Require(stopped.status == Status::Cancelled && !stopped.model, "repair cancellation leaves no candidate");
        }
        std::cout << "exact oriented cylindrical-band source repair contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
