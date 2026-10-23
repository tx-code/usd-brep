// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <SmAssertArray.h>
#include <SmNativeArchive.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>

namespace
{
using namespace step_import;
using step_import_test::Require;
Options Config()
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    options.restrictUnusedSupportDomains = true;
    return options;
}
void CheckOwned(const std::string& source, const std::filesystem::path& file)
{
    auto result = ImportBytes(source, Config());
    Require(result.status == Status::Accepted && result.model, "public regular spindle qualifies with explicit normalization");
    Require(result.report.surfaceNormalizations.size() == 1, "mandatory report retains the original-basis restriction evidence");
    auto exact = Config();
    exact.nativeLimits.reportBytes = result.report.json.size();
    const auto bounded = ImportBytes(source, exact);
    Require(
        bounded.status == Status::Accepted && bounded.model && bounded.report.json == result.report.json,
        "normalization evidence retains deterministic exact report-byte acceptance"
    );
    --exact.nativeLimits.reportBytes;
    const auto shortReport = ImportBytes(source, exact);
    Require(
        shortReport.status == Status::ResourceLimit && !shortReport.model && shortReport.report.candidateDiscarded,
        "one byte short cannot publish a normalized target without its mandatory report"
    );
    auto model = result.model;
    result.report.surfaceNormalizations[0].sourceSurfaceId = "caller mutation";
    result = {};
    auto evidence = model->SurfaceNormalizations();
    Require(
        evidence.size() == 1 && evidence[0].sourceFaceId == "182" && evidence[0].sourceSurfaceId == "142",
        "normalization evidence belongs to the model"
    );
    Require(
        evidence[0].continuousDifferenceUpperMm <= 1e-9 && evidence[0].minimumAbsoluteGeneratorRadiusMm > 0 && evidence[0].tensorPatches > 0,
        "owning continuous native restriction and nonaxis generator evidence"
    );
    evidence.clear();
    Require(
        model->SurfaceNormalizations().size() == 1 && model->Source()->Source().Bytes() == source && !model->Repairs() &&
            !model->CylindricalBandRepairs(),
        "caller copies cannot erase original source or change None repair"
    );
    const auto info = model->Info();
    Require(
        info.faces == 3 && info.edges == 3 && info.vertices == 2 && info.shapeRole == "MaterialSolid",
        "complete source topology and material role"
    );
    step_import_test::Near(
        info.areaMm2,
        2 * std::numbers::pi * std::numbers::pi + 18 * std::numbers::pi,
        0.02,
        "independent analytic spindle cap area"
    );
    step_import_test::Near(
        *model->MaterialVolumeMm3(),
        22 * std::numbers::pi / 3 + 2 * std::numbers::pi * std::numbers::pi,
        0.01,
        "independent analytic spindle cap volume"
    );
    Require(
        model->ClassifyMaterial({ 0, 0, 1 }) == MaterialPointClass::Inside && model->ClassifyMaterial({ 5, 0, 1 }) == MaterialPointClass::Outside,
        "independent material point controls"
    );
    model->WriteNative(file);
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
        "complete target archive readback"
    );
    auto copy = model->CopyBrepForModeling();
    Require(copy.status == Status::Accepted && copy.model, "independent complete modeling copy");
    model.reset();
    copy.model->Edit(
        [&](SmBrep& brep)
        {
            SmAssertArray pointers, deep;
            Require(
                brep.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0 && brep.AssertValid(&deep, SM_LEVEL_2, SM_WALK) &&
                    deep.GetSize() == 0,
                "copy remains valid after original context release"
            );
            step_import_test::CompareNativeDefinitions(brep, *bodies[0]);
        }
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "pass public synthetic fixture and output directory");
        std::ifstream input(argv[1], std::ios::binary);
        Require(bool(input), "public fixture opens");
        const std::string source{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        std::filesystem::create_directories(argv[2]);
        CheckOwned(source, std::filesystem::path(argv[2]) / "spindle.smb");
        for (const auto phase : { Phase::NativeReady, Phase::BeforeReport, Phase::ReportReady, Phase::BeforeCommit })
        {
            auto options = Config();
            options.execution.control = std::make_shared<ImportControl>();
            options.execution.progress = [&](Progress p)
            {
                if (p.phase == phase)
                {
                    options.execution.control->RequestCancel();
                }
            };
            const auto cancelled = ImportBytes(source, options);
            Require(cancelled.status == Status::Cancelled && !cancelled.model, "late cancellation cannot publish a normalized support");
        }
        auto limited = Config();
        limited.nativeLimits.nativeCalls = 1;
        const auto budget = ImportBytes(source, limited);
        Require(budget.status == Status::ResourceLimit && !budget.model, "normalization keeps the cumulative native work cap");
        std::cout << "Toroidal original-basis ownership, query, copy/archive and publication contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
