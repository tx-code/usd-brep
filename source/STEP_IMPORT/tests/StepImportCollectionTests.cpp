// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <SmApiQueries.h>
#include <SmAssertArray.h>
#include <SmAxis2Placement.h>
#include <SmBrepData.h>
#include <SmNativeArchive.h>
#include <SmTopoTypes.h>
#include <fstream>
#include <iostream>
#include <numbers>
using step_import_test::Near;
using step_import_test::Require;
using namespace step_import;
std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}
Options Config()
{
    auto o = Options::PlanarFixture();
    o.generalBrep = true;
    o.quality = Quality::GeneralDevelopment();
    o.reportEncoding = ReportEncoding::CompactV2;
    return o;
}
void Validate(SmBrep& body)
{
    SmAssertArray pointers, deep;
    Require(body.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "archive pointers");
    Require(body.AssertValid(&deep, SM_LEVEL_2, SM_WALK) && deep.GetSize() == 0, "archive level2");
}
double Volume(SmBrep& body)
{
    double area = 0, volume = 0, mass = 0;
    SmPoint3d center;
    SmVector3d moments, products;
    Require(
        SmApiBrepComputeMassProperties(&body, 1e-4, 1, SmPoint3d(0, 0, 0), area, volume, mass, center, moments, products) == SM_SUCCESS,
        "archive mass query"
    );
    return volume;
}
void RoundTrip(const MaterialCollectionSnapshot& collection, const std::filesystem::path& path)
{
    collection.WriteNative(path);
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    SmObjsDelete<SmCurve*> deleteCurves(&curves);
    SmObjsDelete<SmSurface*> deleteSurfaces(&surfaces);
    SmObjsDelete<SmBrep*> deleteBreps(&breps);
    Require(SmNativeArchive::ReadPartFromFile(context, path.c_str(), curves, surfaces, trees, breps, SM_BINARY) == SM_SUCCESS, "archive reload");
    Require(
        breps.GetSize() == collection.BodyCount() && curves.GetSize() == 0 && surfaces.GetSize() == 0 && trees.GetSize() == 0,
        "ordered independent bodies, no CSG union"
    );
    for (std::size_t i = 0; i < collection.BodyCount(); ++i)
    {
        auto& loaded = *breps[static_cast<ULONG>(i)];
        Validate(loaded);
        auto model = collection.Body(i);
        // Same numerical integration agreement screen as the single-body contract;
        // exact curve/surface coefficients and installed UV are checked below.
        Near(Volume(loaded), *model->MaterialVolumeMm3(), std::max(1e-6, *model->MaterialVolumeMm3() * 2e-4), "ordered volume");
        auto copy = model->CopyForModeling();
        Require(copy.status == Status::Accepted && copy.model, "modeling copy");
        copy.model->Edit(
            [&](SmBrep& original)
            {
                step_import_test::CompareNativeDefinitions(original, loaded);
            }
        );
    }
}
void Controls(const std::string& source, const std::filesystem::path& output)
{
    auto result = ImportMaterialCollectionBytes(source, Config());
    Require(result.status == Status::Accepted && result.collection && result.collection->BodyCount() == 2, "whole two-box collection");
    Require(result.report.bodies.size() == 2 && result.report.bodies[0].checksPassed && result.report.bodies[1].checksPassed, "all body checks");
    const auto firstCalls = result.report.bodies[0].checks->nativeCalls;
    Require(result.report.nativeCalls == firstCalls + result.report.bodies[1].checks->nativeCalls, "shared call ledger");
    auto first = result.collection->Body(0), second = result.collection->Body(1);
    Require(first->Source() == second->Source() && first->Source()->Source().Bytes() == source, "one unchanged source owner");
    Require(result.collection->SourceShapeId(0) == "21" && result.collection->SourceShapeId(1) == "10021", "source identities/order");
    Near(*first->MaterialVolumeMm3(), 6000, 1e-6, "first analytic volume");
    Near(*second->MaterialVolumeMm3(), 6000, 1e-6, "second analytic volume");
    Near(first->Info().areaMm2, 2200, 1e-6, "analytic area");
    Near(second->Info().centroidMm[0], 107, 1e-6, "source translation");
    Require(
        first->ClassifyMaterial({ 7, 7, 20 }) == MaterialPointClass::Inside && second->ClassifyMaterial({ 7, 7, 20 }) == MaterialPointClass::Outside,
        "per-body queries"
    );
    auto edited = first->CopyForModeling();
    edited.model->Edit(
        [](SmBrep& body)
        {
            Require(
                body.Transform(SmAxis2Placement(SmPoint3d(1000, 0, 0), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0)), nullptr, FALSE) == SM_SUCCESS,
                "copy transform"
            );
        }
    );
    Require(
        first->ClassifyMaterial({ 7, 7, 20 }) == MaterialPointClass::Inside && second->ClassifyMaterial({ 107, 7, 20 }) == MaterialPointClass::Inside,
        "copy mutation isolation"
    );
    RoundTrip(*result.collection, output / "boxes.smb");
    result = {};
    Require(
        first->ClassifyMaterial({ 7, 7, 20 }) == MaterialPointClass::Inside && first->Source()->Source().Bytes() == source,
        "body outlives result"
    );
    for (const auto phase : { Phase::NativeReady, Phase::BeforeCommit })
    {
        auto o = Config();
        o.execution.control = std::make_shared<ImportControl>();
        o.execution.progress = [&](Progress p)
        {
            if (p.phase == phase)
            {
                o.execution.control->RequestCancel();
            }
        };
        auto r = ImportMaterialCollectionBytes(source, o);
        Require(r.status == Status::Cancelled && !r.collection && r.report.candidateDiscarded, "late cancellation cleanup");
    }
    auto between = Config();
    between.execution.control = std::make_shared<ImportControl>();
    between.execution.progress = [&](Progress p)
    {
        if (p.phase == Phase::BeforeNativeCall && p.nativeCalls >= firstCalls)
        {
            between.execution.control->RequestCancel();
        }
    };
    const auto interrupted = ImportMaterialCollectionBytes(source, between);
    Require(
        interrupted.status == Status::Cancelled && !interrupted.collection && interrupted.report.bodies.size() == 2 &&
            interrupted.report.bodies[0].checksPassed && interrupted.report.candidateDiscarded,
        "cancellation between bodies releases prior candidate"
    );
    auto budget = Config();
    budget.nativeLimits.nativeCalls = firstCalls + 1;
    const auto exhausted = ImportMaterialCollectionBytes(source, budget);
    Require(
        exhausted.status == Status::ResourceLimit && !exhausted.collection && exhausted.report.nativeCalls == firstCalls + 1 &&
            exhausted.report.bodies.size() == 2 && exhausted.report.bodies[0].checksPassed && exhausted.report.candidateDiscarded,
        "cumulative budget across bodies"
    );
    auto limited = Config();
    limited.nativeLimits.reportBytes = 512;
    const auto noReport = ImportMaterialCollectionBytes(source, limited);
    Require(
        noReport.status == Status::ResourceLimit && !noReport.collection && noReport.report.json.empty() && noReport.report.candidateDiscarded,
        "mandatory report cleanup"
    );
    auto broken = source;
    const std::string token = "#10100=CARTESIAN_POINT('',(102.0,-3.,5.))";
    auto at = broken.find(token);
    Require(at != std::string::npos, "second-body defect insertion");
    broken.replace(at, token.size(), "#10100=CARTESIAN_POINT('',(103.0,-3.,5.))");
    const auto failed = ImportMaterialCollectionBytes(broken, Config());
    Require(
        !failed.collection && failed.report.bodies.size() == 2 && failed.report.bodies[0].checksPassed && !failed.report.bodies[1].checksPassed &&
            failed.report.candidateDiscarded,
        "invalid second body cannot publish first"
    );
    const auto single = ImportBytes(source, Config());
    Require(single.status == Status::Unsupported && !single.model && single.report.nativeCalls == 0, "legacy API remains singular");
}
void Development(const std::filesystem::path& input, const std::filesystem::path& output, Options options)
{
    options.sourceLimits = { 67108864, 1000000, 2000000, 2000000, 128, 1048576, 16777216, 268435456, 1024 };
    options.semanticLimits.dependencyVisits = 10000000;
    options.semanticLimits.preservedDiagnostics = 16384;
    options.nativeLimits.nativeCalls = 100000;
    options.nativeLimits.reportBytes = 268435456;
    std::filesystem::create_directories(output);
    auto result = ImportMaterialCollectionFile(input, options);
    std::ofstream report(output / "import.json", std::ios::binary);
    report << result.report.json;
    report.close();
    std::ofstream status(output / "status.json", std::ios::binary);
    status << "{\"status\":\"" << StatusName(result.status) << "\",\"native_calls\":" << result.report.nativeCalls << "}\n";
    status.close();
    Require(
        (result.status == Status::Accepted || result.status == Status::AcceptedUnderInterpretation) && result.collection,
        "complete collection import"
    );
    auto collection = result.collection;
    Require(collection->BodyCount() == collection->Source()->PartDefinition().shapes.size(), "complete selected inventory");
    for (std::size_t i = 0; i < collection->BodyCount(); ++i)
    {
        auto body = collection->Body(i);
        const auto info = body->Info();
        const auto& binding = collection->Source()->PartDefinition().shapes[i];
        const auto faces = body->Source()->Field(binding.shell, "cfs_faces").ReferenceTargets();
        for (const auto& face : body->Faces())
        {
            Require(
                std::any_of(
                    faces.begin(),
                    faces.end(),
                    [&](auto record)
                    {
                        return body->Source()->Source().Records()[record].id == face.SourceId();
                    }
                ),
                "source face belongs to this body"
            );
        }
        Point outside = info.boundsMm[0];
        const auto span = std::hypot(info.boundsMm[1][0] - outside[0], info.boundsMm[1][1] - outside[1], info.boundsMm[1][2] - outside[2]);
        outside[0] -= span + 1;
        const auto closest = body->Closest(outside);
        Require(std::isfinite(closest.distanceMm) && closest.distanceMm >= 0, "finite whole-body query");
        Require(body->ClassifyMaterial(outside) == MaterialPointClass::Outside, "exterior classification");
        Require(body->Source() == collection->Source(), "shared original source owner");
    }
    RoundTrip(*collection, output / "collection.smb");
    std::ofstream checks(output / "checks.json", std::ios::binary);
    checks
        << "{\"status\":\"Passed\",\"body_count\":" << collection->BodyCount()
        << ",\"all_bodies_checked\":true,\"source_membership_checked\":true,\"defining_coefficients_and_uv_checked\":true,\"archive_and_modeling_copies_checked\":true}\n";
    Require(bool(checks), "collection checks receipt");
}

int main(int argc, char** argv)
{
    try
    {
        if (argc >= 5 && std::string_view(argv[1]) == "--development-file")
        {
            auto options = Config();
            options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
            const std::string_view quality(argv[4]);
            options.quality = quality == "Q-AP214-development-mm-01"          ? Quality::GeneralDevelopment() :
                              quality == "Q-AP214-tolerance-study-mm-01"      ? Quality::GeneralToleranceStudy() :
                              quality == "Q-AP214-work-tolerance-study-mm-01" ? Quality::GeneralWorkToleranceStudy() :
                                                                                throw std::invalid_argument("Unknown quality");
            options.headerNamePolicy = step_source::HeaderNamePolicy::PreserveOpaqueBytes;
            options.dataStringPolicy = step_source::DataStringPolicy::PreserveOpaqueBytes;
            for (int i = 5; i < argc; ++i)
            {
                const std::string_view flag(argv[i]);
                if (flag == "--preserve-opaque-file-name-text")
                {
                    options.headerNamePolicy = step_source::HeaderNamePolicy::PreserveFileNameText;
                }
                else if (flag == "--assume-ascii-invariant-data")
                {
                    options.dataStringPolicy = step_source::DataStringPolicy::PreserveAsciiInvariantBytes;
                }
                else if (flag == "--interpret-ap214-name-only-as-2010")
                {
                    options.schemaInterpretation = step_semantics::SchemaInterpretation::Ap214NameOnlyAs2010;
                }
                else if (flag == "--interpret-level1-with-lowercase-hex")
                {
                    options.carrierInterpretation = step_source::CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex;
                }
                else
                {
                    throw std::invalid_argument("Unknown explicit collection flag");
                }
            }
            Development(argv[2], argv[3], options);
            return 0;
        }
        Require(argc == 4, "fixtures and output required");
        std::filesystem::create_directories(argv[3]);
        Controls(Read(argv[1]), argv[3]);
        auto mixed = ImportMaterialCollectionBytes(Read(argv[2]), Config());
        Require(
            mixed.status == Status::Accepted && mixed.collection && mixed.collection->BodyCount() == 2,
            "overlapping analytic bodies stay separate"
        );
        Near(*mixed.collection->Body(1)->MaterialVolumeMm3(), 63 * std::numbers::pi, 0.001, "analytic cylinder volume");
        RoundTrip(*mixed.collection, std::filesystem::path(argv[3]) / "mixed.smb");
        std::cout << "material collection contracts passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
