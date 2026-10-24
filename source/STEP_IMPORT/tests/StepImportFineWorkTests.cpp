// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <SmAssertArray.h>
#include <SmBrepData.h>
#include <SmNativeArchive.h>
#include <SmTol.h>
#include <SmVertex.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <tuple>

using namespace step_import;
using step_import_test::Require;
namespace
{
auto GeometryBudgets(const Quality& q)
{
    return std::tie(q.definitionMm, q.consistencyMm, q.derivedUvMm, q.queryMm, q.areaMm2, q.volumeMm3);
}
void VerifyNative(SmBrep& native)
{
    SmAssertArray pointers, deep;
    Require(
        native.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0 && native.AssertValid(&deep, SM_LEVEL_2, SM_WALK) &&
            deep.GetSize() == 0,
        "fine work native validity"
    );
    step_import_test::CheckNativeWorkBudget(native, 0.0001);
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public source fixture and new archive directory required");
        std::ifstream input(argv[1], std::ios::binary);
        std::string source{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        std::size_t start = 0, edits = 0;
        while ((start = source.find("(12.,", start)) != std::string::npos)
        {
            source.replace(start, 5, "(2.0005,");
            start += 8;
            ++edits;
        }
        Require(edits == 4, "independent synthetic thin box construction");
        auto options = Options::PlanarFixture();
        options.generalBrep = true;
        options.quality = Quality::GeneralDevelopment();
        const auto ordinary = ImportBytes(source, options);
        Require(!ordinary.model, "ordinary profile must not retry after its refusal");
        const auto geometry = options.quality;
        options.quality = Quality::GeneralFineWorkToleranceStudy();
        Require(
            GeometryBudgets(options.quality) == GeometryBudgets(geometry) && options.quality.initialWorkToleranceMm == Fraction(1, 10000) &&
                options.quality.maximumWorkToleranceMm == Fraction(1, 10000) && options.quality.toleranceGrowthMm == Fraction(),
            "separate unchanged geometry/query budgets and fixed finer work zone"
        );
        auto result = ImportBytes(source, options);
        Require(result.status == Status::Accepted && result.model, "explicit fine-work import");
        const auto reportBytes = result.report.json.size();
        const auto model = result.model;
        result = {};
        Require(
            model->Source()->Source().Bytes() == source && !model->Repairs() && !model->CylindricalBandRepairs(),
            "owning original source and no repair after report disposal"
        );
        step_import_test::Near(*model->MaterialVolumeMm3(), 0.3, 1e-5, "independent thin-box volume");
        step_import_test::Near(model->Info().areaMm2, 1200.05, 1e-4, "independent thin-box area");
        Require(model->ClassifyMaterial({ 2.00025, 7, 20 }) == MaterialPointClass::Inside, "thin-box material query");
        const auto closest = model->Closest({ 1, 7, 20 });
        step_import_test::Near(closest.distanceMm, 1, 1e-5, "thin-box closest point");
        std::filesystem::create_directories(argv[2]);
        const auto path = std::filesystem::path(argv[2]) / "fine-work.smb";
        model->WriteNative(path);
        SmContext context;
        SmTArray<SmCurve*> curves;
        SmTArray<SmSurface*> surfaces;
        SmTArray<long> trees;
        SmTArray<SmBrep*> bodies;
        SmObjsDelete<SmCurve*> dc(&curves);
        SmObjsDelete<SmSurface*> ds(&surfaces);
        SmObjsDelete<SmBrep*> db(&bodies);
        Require(
            SmNativeArchive::ReadPartFromFile(context, path.c_str(), curves, surfaces, trees, bodies, SM_BINARY) == SM_SUCCESS &&
                bodies.GetSize() == 1,
            "fine-work archive readback"
        );
        VerifyNative(*bodies[0]);
        auto copy = model->CopyBrepForModeling();
        Require(copy.status == Status::Accepted && copy.model, "independent editable copy");
        copy.model->Edit(
            [&](SmBrep& native)
            {
                VerifyNative(native);
                step_import_test::CompareNativeDefinitions(native, *bodies[0]);
            }
        );
        auto cap = options;
        cap.nativeLimits.reportBytes = reportBytes - 1;
        const auto overflow = ImportBytes(source, cap);
        Require(
            overflow.status == Status::ResourceLimit && !overflow.model && overflow.report.candidateDiscarded,
            "fine profile cannot skip its mandatory report"
        );
        auto cancelled = options;
        cancelled.execution.control = std::make_shared<ImportControl>();
        cancelled.execution.progress = [&](Progress p)
        {
            if (p.phase == Phase::BeforeCommit)
            {
                cancelled.execution.control->RequestCancel();
            }
        };
        const auto stopped = ImportBytes(source, cancelled);
        Require(stopped.status == Status::Cancelled && !stopped.model, "fine profile preserves final cancellation decision");
        options.quality.maximumWorkToleranceMm = Fraction(1, 1000);
        bool invalid = false;
        try
        {
            (void)ImportBytes(source, options);
        }
        catch (const std::invalid_argument&)
        {
            invalid = true;
        }
        Require(invalid, "frozen fine-work identity cannot be reused for a different budget");
        std::cout << "explicit fine-work quality contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
