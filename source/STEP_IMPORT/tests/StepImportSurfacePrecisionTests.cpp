// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"
#include "StepImportReport.h"
#include "StepImportSurfacePrecision.h"

#include <SmAssertArray.h>
#include <SmBrepData.h>
#include <SmNativeArchive.h>
#include <SmSurfOfRevolution.h>
#include <SmSurfaceWorkPrecision.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

using namespace step_import;
using step_import_test::Require;
namespace
{
Options Config()
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralPcurveCarrierPrecisionStudy();
    options.numericalIntegration = NumericalIntegrationSettings{ 1e-8 };
    return options;
}
std::shared_ptr<TargetSurfacePrecisionRequest> Request(const std::string& source)
{
    const auto inspected = step_source::InspectBytes(source, step_source::Limits::PlanarFixtures());
    Require(bool(inspected.source), "public source identity");
    auto request = std::make_shared<TargetSurfacePrecisionRequest>();
    request->sourceSha256 = inspected.source->Sha256();
    request->maximumFaces = 1;
    request->workResolutionMm = { 1, 100000000000 };
    request->faces.push_back({ "7", "21", "182", "142" });
    return request;
}
void VerifyNative(SmBrep& brep)
{
    SmAssertArray pointers, findings;
    Require(brep.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "complete precision model pointers");
    Require(brep.AssertValid(&findings, SM_LEVEL_2, SM_WALK) && findings.GetSize() == 0, "unfiltered complete native LEVEL2, including surface Test5");
    step_import_test::CheckNativeWorkBudget(brep, .001);
    SmTArray<SmFace*> faces;
    brep.GetFaces(faces);
    Require(faces.GetSize() == 1 && faces[0]->GetTolerance() == .001, "unchanged topology face zone");
    auto* surface = faces[0]->GetSurface();
    Require(surface && surface->GetType() == SmSurfOfRevolution_TYPE && surface->GetOwner() == faces[0], "actual owned original revolution");
    double precision = 0;
    Require(SmSurfaceWorkPrecision::Get(*surface, precision) == SM_SUCCESS && precision == 1e-11, "exact persisted geometry precision scalar");
    double effective = 0;
    Require(SmSurfaceWorkPrecision::GetEffective(*surface, effective) == SM_SUCCESS && effective == 1e-11, "numerical degeneracy resolution narrowed");
    Require(double(SmTol::GetZoneTol3d(surface)) == .001, "geometry coincidence zone unchanged");
    SmBSplineCurve* generator = nullptr;
    SmPoint3d origin;
    SmVector3d axis;
    SmBoolean swapped = TRUE;
    Require(
        static_cast<SmSurfOfRevolution*>(surface)->GetCanonical(generator, origin, axis, &swapped) == SM_SUCCESS && generator && !swapped,
        "original generator and parameter convention"
    );
    SmTArray<SmPoint3d> controls;
    SmTArray<double> weights;
    Require(
        generator->GetControlPolygon(controls, weights) == SM_SUCCESS && controls.GetSize() == 2 && controls[1].x == 0 && controls[1].y == .000001 &&
            controls[1].z == 7,
        "original small nonzero end never collapsed to an exact pole"
    );
    ULONG approximate = 0;
    Require(
        surface->GetSingularities(nullptr, nullptr, &approximate) == 0 && approximate == 0,
        "native classifiers recomputed at explicit geometry resolution, no flags overwritten"
    );
}
void Check(const std::string& source, const std::filesystem::path& directory)
{
    auto options = Config();
    const auto baseline = ImportBytes(source, options);
    Require(
        baseline.status == Status::Deferred && !baseline.model && !baseline.report.targetSurfacePrecision,
        "default near-pole admission restriction unchanged"
    );
    auto request = Request(source);
    options.targetSurfacePrecision = request;
    auto result = ImportBytes(source, options);
    Require(result.status == Status::AcceptedUnderInterpretation && result.model, "only explicitly source-bound precision publishes");
    auto model = result.model;
    auto receipt = model->TargetSurfacePrecision();
    Require(
        receipt && receipt->requestComplete && receipt->plans.size() == 1 && receipt->actions.size() == 1 && receipt->actions[0].nativeChecksPassed &&
            receipt->actions[0].requestedMm == 1e-11 && receipt->actions[0].effectiveMm == 1e-11 &&
            receipt->actions[0].topologyFaceToleranceMm == .001,
        "complete request-bound owning precision receipt"
    );
    Require(
        model->Info().shapeRole == "OpenSurfaceModel" && !model->MaterialVolumeMm3() && !model->Repairs() && !model->CylindricalBandRepairs(),
        "source surface role and None repair retained"
    );
    Require(model->Source()->Source().Bytes() == source, "immutable source bytes including original UV retained");
    // Independent analytic conical-sector area: angle * average radius *
    // slant length. The full support's 1e-6mm end remains outside this trim.
    // This integral observation is not a continuous evaluator error bound.
    const double area = .9 * (3 + 1.5000005) / 2 * std::hypot(1.4999995, 3.5);
    step_import_test::Near(model->Info().areaMm2, area, .001, "independent conical-sector area observation");
    auto editable = model->CopyForModeling();
    Require(editable.status == Status::Accepted && editable.model, "independent modeling copy with precision metadata");
    editable.model->Edit(VerifyNative);
    for (const auto format : { SM_BINARY, SM_ASCII })
    {
        const auto path = directory / (format == SM_BINARY ? "surface-precision.smb" : "surface-precision.smt");
        editable.model->Edit(
            [&](SmBrep& brep)
            {
                SmTArray<SmCurve*> curves;
                SmTArray<SmSurface*> surfaces;
                SmTArray<long> trees;
                SmTArray<SmBrep*> bodies;
                bodies.Add(&brep);
                Require(
                    SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, bodies, format, TRUE, FALSE) == SM_SUCCESS,
                    "precision archive write"
                );
            }
        );
        SmContext context;
        SmTArray<SmCurve*> curves;
        SmTArray<SmSurface*> surfaces;
        SmTArray<long> trees;
        SmTArray<SmBrep*> bodies;
        SmObjsDelete<SmCurve*> dc(&curves);
        SmObjsDelete<SmSurface*> ds(&surfaces);
        SmObjsDelete<SmBrep*> db(&bodies);
        Require(
            SmNativeArchive::ReadPartFromFile(context, path.c_str(), curves, surfaces, trees, bodies, format) == SM_SUCCESS && bodies.GetSize() == 1,
            "complete binary/text native archive read"
        );
        VerifyNative(*bodies[0]);
    }
    model->WriteNative(directory / "surface-precision-linked.smb");
    const auto inspected = step_source::InspectBytes(source, options.sourceLimits);
    const auto expectBadReceipt = [&](std::shared_ptr<const TargetSurfacePrecisionReceipt> bad)
    {
        auto malformed = result.report;
        malformed.targetSurfacePrecision = std::move(bad);
        std::string output = "unchanged";
        const auto failed = detail::Serialize(
            Status::AcceptedUnderInterpretation,
            malformed,
            options.quality,
            inspected,
            options.nativeLimits.reportBytes,
            output
        );
        Require(
            !failed && failed.error().key == "TargetSurfacePrecisionReceiptInvalid" && output == "unchanged",
            "accepted report refuses incomplete/invalid precision evidence atomically"
        );
    };
    auto bad = std::make_shared<TargetSurfacePrecisionReceipt>(*receipt);
    bad->requestComplete = false;
    expectBadReceipt(bad);
    bad = std::make_shared<TargetSurfacePrecisionReceipt>(*receipt);
    bad->actions[0].nativeChecksPassed = false;
    expectBadReceipt(bad);
    bad = std::make_shared<TargetSurfacePrecisionReceipt>(*receipt);
    bad->actions[0].effectiveMm = std::numeric_limits<double>::quiet_NaN();
    expectBadReceipt(bad);
    bad = std::make_shared<TargetSurfacePrecisionReceipt>(*receipt);
    bad->actions[0].requestedMm = .001;
    expectBadReceipt(bad);
    bad = std::make_shared<TargetSurfacePrecisionReceipt>(*receipt);
    bad->actions[0].topologyFaceToleranceMm = 0;
    expectBadReceipt(bad);
    expectBadReceipt({});
    // Caller releases/mutates its request and report; the snapshot owns the
    // accepted original receipt. Native modeling copies do not inherit it.
    result = {};
    options.targetSurfacePrecision.reset();
    request->faces.clear();
    Require(
        model->TargetSurfacePrecision() == receipt && receipt->plans.size() == 1 && receipt->requestComplete,
        "snapshot evidence independent of mutable request/report lifetime"
    );
}
void Refusals(const std::string& source, const std::string& exactControl)
{
    auto options = Config();
    auto request = Request(source);
    options.targetSurfacePrecision = request;
    // A separate public near-tip triangle keeps its old pole vertex and
    // meridians while only its surface endpoint becomes nonzero. Finer
    // classification cannot waive the unchanged seam/source-vertex gate.
    auto nearTip = exactControl;
    const auto tipBegin = nearTip.find("#945=CARTESIAN_POINT");
    Require(tipBegin != std::string::npos, "public exact control tip found");
    const auto tipEnd = nearTip.find(';', tipBegin);
    Require(tipEnd != std::string::npos, "public exact control tip complete");
    nearTip.replace(tipBegin, tipEnd - tipBegin + 1, "#945=CARTESIAN_POINT('',(0.,0.000001,3.));");
    options.targetSurfacePrecision = Request(nearTip);
    auto failed = ImportBytes(nearTip, options);
    Require(failed.status == Status::PostcheckFailure && !failed.model, "fine resolution cannot waive a distinct near-tip seam failure");
    Require(
        std::any_of(
            failed.report.diagnostics.begin(),
            failed.report.diagnostics.end(),
            [](const auto& d)
            {
                return d.source.key == "GeneralRegionSeamUnestablished";
            }
        ),
        "near-tip refusal retains original seam diagnostic"
    );
    options.targetSurfacePrecision = request;
    auto wrong = std::make_shared<TargetSurfacePrecisionRequest>(*request);
    wrong->sourceSha256[0] = wrong->sourceSha256[0] == 'a' ? 'b' : 'a';
    options.targetSurfacePrecision = wrong;
    failed = ImportBytes(source, options);
    Require(failed.status == Status::Unsupported && !failed.model, "wrong immutable original never qualifies");
    wrong = std::make_shared<TargetSurfacePrecisionRequest>(*request);
    wrong->faces[0].surfaceId = "999";
    options.targetSurfacePrecision = wrong;
    failed = ImportBytes(source, options);
    Require(failed.status == Status::Unsupported && !failed.model, "wrong source surface binding refuses");
    auto unused = Request(exactControl);
    unused->faces[0].faceId = "999";
    options.targetSurfacePrecision = unused;
    failed = ImportBytes(exactControl, options);
    Require(failed.status == Status::Unsupported && !failed.model, "an unused plan prevents otherwise valid complete publication");
    options.targetSurfacePrecision = request;
    for (const auto phase : { Phase::BeforeCapture, Phase::BeforeNativeCall, Phase::NativeReady, Phase::BeforeCommit })
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
        failed = ImportBytes(source, stopped);
        Require(failed.status == Status::Cancelled && !failed.model, "cancellation never publishes a precision model");
    }
    auto limited = options;
    limited.nativeLimits.nativeCalls = 2;
    failed = ImportBytes(source, limited);
    Require(failed.status == Status::ResourceLimit && !failed.model, "native work limits remain enforced");
    const auto expectInvalid = [&](const std::shared_ptr<TargetSurfacePrecisionRequest>& bad)
    {
        auto invalid = Config();
        invalid.targetSurfacePrecision = bad;
        bool threw = false;
        try
        {
            static_cast<void>(ImportBytes(source, invalid));
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        Require(threw, "invalid precision request retains caller error contract");
    };
    wrong = std::make_shared<TargetSurfacePrecisionRequest>(*request);
    wrong->maximumFaces = 0;
    expectInvalid(wrong);
    wrong = std::make_shared<TargetSurfacePrecisionRequest>(*request);
    wrong->maximumFaces = 2;
    wrong->faces.push_back(wrong->faces[0]);
    expectInvalid(wrong);
    wrong = std::make_shared<TargetSurfacePrecisionRequest>(*request);
    wrong->workResolutionMm = { 0, 1 };
    expectInvalid(wrong);
    wrong = std::make_shared<TargetSurfacePrecisionRequest>(*request);
    wrong->workResolutionMm = { 1, 0 };
    expectInvalid(wrong);
    wrong = std::make_shared<TargetSurfacePrecisionRequest>(*request);
    wrong->workResolutionMm = { 1, 1 };
    expectInvalid(wrong);
    auto mutableRequest = Request(source);
    options = Config();
    options.targetSurfacePrecision = mutableRequest;
    options.execution.progress = [mutableRequest](Progress p)
    {
        if (p.phase == Phase::BeforeCapture)
        {
            mutableRequest->faces[0].surfaceId = "999";
            mutableRequest->workResolutionMm = { 1, 1 };
        }
    };
    const auto frozen = ImportBytes(source, options);
    Require(
        frozen.status == Status::AcceptedUnderInterpretation && frozen.model &&
            frozen.model->TargetSurfacePrecision()->workResolutionMm[1] == 100000000000,
        "request deep-copied before callback mutation"
    );
    options = Config();
    options.targetSurfacePrecision = Request(source);
    const auto document = ImportDocumentBytes(source, options);
    Require(
        document.status == Status::AcceptedUnderInterpretation && document.document && document.document->BodyCount() == 1 &&
            document.document->Body(0)->TargetSurfacePrecision() == document.report.targetSurfacePrecision &&
            document.report.targetSurfacePrecision->requestComplete &&
            document.report.json.find("\"receipt_scope\":\"RequestRoot\"") != std::string::npos,
        "nested complete definition uses the one owning root precision receipt"
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "fixture and output directory required");
        const auto path = std::filesystem::path(argv[1]);
        const auto directory = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(directory);
        std::ifstream input(path);
        std::string source((std::istreambuf_iterator<char>(input)), {});
        std::ifstream exact(path.parent_path() / "source_pcurve_revolution_pole_driven_patch.step");
        const std::string exactControl((std::istreambuf_iterator<char>(exact)), {});
        Require(!source.empty() && !exactControl.empty(), "public fixtures readable");
        Check(source, directory);
        Refusals(source, exactControl);
        std::cout << "Source-bound target-surface precision contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
