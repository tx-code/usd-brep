// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "../app/StepConicalApexArguments.h"
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <SmAssertArray.h>
#include <SmBrep.h>
#include <SmContext.h>
#include <SmNativeArchive.h>
#include <fstream>
#include <iostream>
#include <numbers>

namespace
{
using namespace step_import;
using step_import_test::Require;
Options Settings(const std::string& text)
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    ConicalApexRepairRequest request;
    request.sourceSha256 = step_source::InspectBytes(text, options.sourceLimits).sha256;
    request.uses.push_back({ "7", "21", "182", "174", "164", "122", "142" });
    request.maximumVertices = 1;
    request.maximumProofControls = 4096;
    request.maximumProofWork = 1000000;
    request.maximumDisplacementMm = request.maximumTotalDisplacementMm = Fraction(1, 1000);
    options.conicalApexRepair = std::move(request);
    return options;
}
void Refused(const std::string& text, const Options& options, Status status, const char* key = nullptr)
{
    const auto result = ImportBytes(text, options);
    Require(result.status == status && !result.model, "failed repair must withhold its complete candidate");
    if (key)
    {
        const auto found = std::any_of(
            result.report.diagnostics.begin(),
            result.report.diagnostics.end(),
            [&](const auto& d)
            {
                return d.source.key == key;
            }
        );
        if (!found)
        {
            std::cerr << "expected " << key << "; received";
            for (const auto& diagnostic : result.report.diagnostics)
            {
                std::cerr << ' ' << diagnostic.source.key;
            }
            std::cerr << '\n';
        }
        Require(found, "repair refusal retains its exact diagnostic");
    }
}
void NativeValid(SmBrep& brep)
{
    SmAssertArray pointers, deep;
    Require(brep.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "repaired native pointer validity");
    Require(brep.AssertValid(&deep, SM_LEVEL_2, SM_WALK) && deep.GetSize() == 0, "repaired full LEVEL2 validity");
    step_import_test::CheckNativeWorkBudget(brep, .001);
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public source fixture and private output folder required");
        std::ifstream input(argv[1], std::ios::binary);
        std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        Refused(text, Settings(text), Status::Unsupported, "RepairNotRequired");
        const auto at = text.find("(0.,0.,3.)");
        Require(at != std::string::npos, "public source apex");
        text.replace(at, std::string_view("(0.,0.,3.)").size(), "(0.00001,0.,3.)");
        auto options = Settings(text);
        auto strict = options;
        strict.conicalApexRepair.reset();
        Refused(text, strict, Status::PostcheckFailure, "GeneralRegionSeamUnestablished");
        auto result = ImportBytes(text, options);
        Require(result.status == Status::AcceptedWithRepairs && result.model, "explicit bounded repair has distinct qualification");
        const auto model = result.model;
        const auto nativeCalls = result.report.nativeCalls;
        const auto history = model->ConicalApexRepairs();
        Require(
            history && history->actions.size() == 1 && history->budget && history->budget->requestComplete && history->curvesAndSurfacesIdentical &&
                history->unrequestedVerticesIdentical && history->workingTolerancesIdentical,
            "owning complete repair receipt"
        );
        Require(
            history->actions[0].originalMm == Point{ .00001, 0, 3 } && history->actions[0].displacementUpperMm <= .001 &&
                history->actions[0].displacementUpperMm <= history->actions[0].localProtectionLowerMm / 100,
            "movement and protected-feature budgets are measured against the original point"
        );
        Require(
            model->Source()->Source().Bytes() == text && result.report.originalAttempt &&
                result.report.originalAttemptStatus == Status::PostcheckFailure && !history->originalDiagnostics.empty(),
            "original input and strict diagnostic remain intact"
        );
        Require(
            model->TopologyNormalization() && !model->TopologyNormalization()->sourceVerticesPreserved,
            "normalization cannot claim original XYZ survived an explicit repair"
        );
        Require(std::abs(*model->MaterialVolumeMm3() - 9 * std::numbers::pi) < 1e-3, "independent cone volume after repair");
        auto edited = model->CopyBrepForModeling();
        Require(edited.status == Status::Accepted && edited.model, "independent repaired modeling copy");
        edited.model->Edit(NativeValid);
        std::filesystem::create_directories(argv[2]);
        const auto archive = std::filesystem::path(argv[2]) / "repaired-cone.smb";
        model->WriteNative(archive);
        SmContext context;
        SmTArray<SmCurve*> curves;
        SmTArray<SmSurface*> surfaces;
        SmTArray<long> trees;
        SmTArray<SmBrep*> bodies;
        SmObjsDelete<SmCurve*> deleteCurves(&curves);
        SmObjsDelete<SmSurface*> deleteSurfaces(&surfaces);
        SmObjsDelete<SmBrep*> deleteBodies(&bodies);
        Require(
            SmNativeArchive::ReadPartFromFile(context, archive.c_str(), curves, surfaces, trees, bodies, SM_BINARY) == SM_SUCCESS &&
                bodies.GetSize() == 1 && curves.GetSize() == 0 && surfaces.GetSize() == 0,
            "complete repaired archive readback"
        );
        NativeValid(*bodies[0]);
        edited.model->Edit(
            [&](SmBrep& copy)
            {
                step_import_test::CompareNativeDefinitions(copy, *bodies[0]);
            }
        );
        result = {};
        Require(model->ConicalApexRepairs()->actions[0].originalMm == Point{ .00001, 0, 3 }, "report release cannot erase owning repair history");
        auto invalid = options;
        invalid.conicalApexRepair->sourceSha256.assign(64, '0');
        Refused(text, invalid, Status::Unsupported, "ConicalApexRepairSourceMismatch");
        invalid = options;
        invalid.conicalApexRepair->uses[0].vertexId = "120";
        Refused(text, invalid, Status::Unsupported, "ConicalApexRepairPointUseBinding");
        invalid = options;
        invalid.conicalApexRepair->maximumDisplacementMm = Fraction(1, 1000000);
        Refused(text, invalid, Status::Indeterminate, "ConicalApexDisplacementLimit");
        invalid = options;
        invalid.conicalApexRepair->maximumTotalDisplacementMm = Fraction(1, 1000000);
        Refused(text, invalid, Status::ResourceLimit, "ConicalApexRepairCumulativeMovementLimit");
        invalid = options;
        invalid.conicalApexRepair->maximumProofWork = history->budget->proofWork;
        Require(
            ImportBytes(text, invalid).status == Status::AcceptedWithRepairs,
            "exact proof capacity includes binding, guards and final comparison"
        );
        --invalid.conicalApexRepair->maximumProofWork;
        Refused(text, invalid, Status::ResourceLimit, "ConicalApexRepairProofWorkLimit");
        invalid = options;
        invalid.nativeLimits.nativeCalls = nativeCalls;
        Require(ImportBytes(text, invalid).status == Status::AcceptedWithRepairs, "whole-request exact native-call capacity includes both attempts");
        --invalid.nativeLimits.nativeCalls;
        Refused(text, invalid, Status::ResourceLimit);
        invalid = options;
        invalid.nativeLimits.reportBytes = 1;
        Refused(text, invalid, Status::ResourceLimit);
        for (const auto phase : { Phase::BeforeRepairProof, Phase::BeforeRepair, Phase::RepairReady, Phase::NativeReady, Phase::BeforeCommit })
        {
            auto stopped = options;
            stopped.execution.control = std::make_shared<ImportControl>();
            stopped.execution.progress = [&, phase](Progress p)
            {
                if (p.phase == phase)
                {
                    stopped.execution.control->RequestCancel();
                }
            };
            Refused(text, stopped, Status::Cancelled);
        }
        auto frozen = options;
        frozen.execution.progress = [&](Progress p)
        {
            if (p.phase == Phase::BeforeCapture)
            {
                frozen.conicalApexRepair->uses[0].vertexId = "120";
            }
        };
        Require(ImportBytes(text, frozen).status == Status::AcceptedWithRepairs, "callback cannot retarget the captured request");
        auto foreignUse = text;
        foreignUse.insert(foreignUse.rfind("ENDSEC;"), "#990=VERTEX_LOOP('',#122);\n");
        Refused(foreignUse, Settings(foreignUse), Status::Unsupported, "UnownedOrOrphanProfileContent");

        // Two independent definitions intentionally share the original geometry.
        // Each source use needs its own action, but the displacement/proof budget
        // belongs to the entire request. No first body may escape a later refusal.
        auto shared = text;
        shared.insert(
            shared.rfind("ENDSEC;"),
            "#900=PRODUCT('second','independent definition',$,(#3));\n"
            "#901=PRODUCT_DEFINITION_FORMATION('B',$,#900);\n"
            "#902=PRODUCT_DEFINITION('D2',$,#901,#6);\n"
            "#903=PRODUCT_DEFINITION_SHAPE('',$,#902);\n"
            "#904=SHAPE_DEFINITION_REPRESENTATION(#903,#920);\n"
            "#920=ADVANCED_BREP_SHAPE_REPRESENTATION('child',(#21),#12);\n"
        );
        auto grouped = Settings(shared);
        grouped.conicalApexRepair->maximumVertices = 2;
        grouped.conicalApexRepair->uses.push_back({ "902", "21", "182", "174", "164", "122", "142" });
        auto document = ImportDocumentBytes(shared, grouped);
        Require(
            document.status == Status::AcceptedWithRepairs && document.document && document.document->BodyCount() == 2,
            "complete shared-definition repair publication"
        );
        const auto budget = document.document->Body(0)->ConicalApexRepairs()->budget;
        Require(
            budget && budget->requestComplete && budget->correctedVertices == 2 && document.document->Body(1)->ConicalApexRepairs()->budget == budget,
            "all owning body receipts share the final cumulative request budget"
        );
        document = {};
        Require(budget->correctedVertices == 2 && budget->requestComplete, "released result cannot erase owning cumulative evidence");
        grouped.conicalApexRepair->maximumTotalDisplacementMm = Fraction(15, 1000000);
        const auto overBudget = ImportDocumentBytes(shared, grouped);
        Require(
            overBudget.status == Status::ResourceLimit && !overBudget.document &&
                overBudget.report.json.find("ConicalApexRepairCumulativeMovementLimit") != std::string::npos,
            "cumulative refusal releases both unpublished bodies without returning a partial document"
        );
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
